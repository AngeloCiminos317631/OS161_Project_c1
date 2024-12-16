#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <coremap.h>
#include <vmc1.h>
#include <swapfile.h>
#include <syscall.h>
#include <statistics.h>

// Variabile globale statica che tiene traccia dell'indice della prossima vittima TLB
static unsigned int current_victim;

/*
 * Seleziona un entry TLB da sostituire usando la strategia Round-Robin.
 * 
 * La strategia Round-Robin sceglie gli slot della TLB in ordine ciclico,
 * evitando complessità computazionali e rendendo il processo deterministico.
 *
 * Restituisce:
 *  - L'indice della entry TLB da sovrascrivere.
 */
static unsigned int tlb_get_rr_victim(void) {
    unsigned int victim;

    // Il valore corrente di current_victim è il prossimo da utilizzare come vittima
    victim = current_victim;

    // Aggiorna l'indice per la prossima chiamata.
    // Incrementa di 1 e torna a 0 se raggiunge NUM_TLB (numero totale di slot TLB).
    current_victim = (current_victim + 1) % NUM_TLB;

    // Restituisce l'indice della vittima selezionata
    return victim;
}
/* 
 * Funzione che chiama coremap_init() per inizializzare la coremap.
 */
void vm_bootstrap(void) {
    coremap_init();
    current_victim = 0; // È inizializzata a 0 e mantiene il suo valore tra le chiamate alla funzione.
    init_statistics(); // Inizializza il sistema di statistiche
}

/* 
 * Funzione che richiama coremap_shutdown() liberando tutte le risorse 
 * allocate per la coremap.
 */
void vm_shutdown(void) {
    coremap_shutdown();
    swap_shutdown(); // Chiude il file di swap
    print_all_statistics(); // Stampa tutte le statistiche raccolte
}

/* 
 * Funzione che controlla se il sistema è in uno stato sicuro per andare in 
 * modalità wait o sleep.
 */
void vm_can_sleep(void) { 
    if(CURCPU_EXISTS()) { 
        KASSERT(curcpu -> c_spinlocks == 0); 
        KASSERT(curthread -> t_in_interrupt == 0); 
    }
}

/**
 * Gestisce un page fault per un processo.
 * Questa funzione viene invocata quando si verifica un page fault durante
 * l'esecuzione di un programma. La funzione determina il tipo di fault,
 * recupera l'indirizzo fisico corrispondente tramite la page table, e se
 * necessario, alloca un nuovo frame fisico per la pagina mancante. Infine,
 * aggiorna la TLB con la mappatura tra l'indirizzo virtuale e fisico.
 *
 * @param fault_type Il tipo di fault che si è verificato. Può essere:
 *                  - VM_FAULT_READONLY: accesso in scrittura a una pagina di sola lettura.
 *                  - VM_FAULT_READ: accesso in lettura a una pagina.
 *                  - VM_FAULT_WRITE: accesso in scrittura a una pagina.
 * @param fault_addr L'indirizzo virtuale che ha causato il page fault.
 *
 * @return Un codice di errore (EFAULT o EINVAL) o 0 se il fault è stato gestito con successo.
 */
int vm_fault(int fault_type, vaddr_t fault_addr)
{
    int spl, new_page, result; //i, found
    unsigned int victim;
    uint32_t ehi, elo, victim_ehi, victim_elo;;
    struct addrspace *as;
    paddr_t pa;
    struct segment * seg;
    vaddr_t pageallign_va;
    off_t swap_offset; // Offset della pagina nello swap file
    off_t result_swap_in; // Risultato della funzione swap_in
    
    //TODO
    // fault_addr &= PAGE_FRAME; //Per eliminare l'offset e lavorare con l'indirizzo base della pagina
    pageallign_va = fault_addr & PAGE_FRAME;

    // Gestione dei diversi tipi di fault
    switch (fault_type) {
        case VM_FAULT_READONLY:
            // Se la fault è su una pagina di sola lettura, restituiamo EACCES
            sys__exit(1); // Termina il processo
            return EACCES; // Restituisce un errore di accesso per permessi mancanti (in questo caso in scrittura)
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break; // Se la fault è di lettura o scrittura, proseguiamo
        default:
            return EINVAL; // Se il tipo di fault non è supportato, ritorna EINVAL
    }

    // Verifica se non c'è alcun processo corrente
    if (curproc == NULL) {
        /*
         * Se non c'è un processo (probabilmente un errore kernel durante l'avvio),
         * restituiamo EFAULT per evitare un ciclo infinito di fault.
         */
        return EFAULT;
    }
    

    // Ottieni l'address space del processo corrente
    as = proc_getas();
    if (as == NULL) {
        /*
         * Se il processo non ha un address space (probabilmente errore kernel),
         * restituiamo EFAULT.
         */
        return EFAULT;
    }

    new_page = -1;
    seg = as_get_segment(as, fault_addr);
    if (seg == NULL)
    {
        return EFAULT;
    }

    // Determina lo stato da assegnare all'entry TLB in base ai permessi della sezione di memoria.

    // Cerchiamo l'indirizzo fisico corrispondente nel page table
    pa = pt_get_pa(as->pt, fault_addr);
    if (pa > 0) {
        increment_statistics(STATISTICS_TLB_RELOAD); // Incrementa il contatore delle ricariche TLB
    }
    // Verifichiamo se la pagina è stata swappata
    swap_offset = pt_get_offset(as->pt, fault_addr);

    // Se non esiste, dobbiamo allocare un nuovo frame
    if(pa == PFN_NOT_USED && swap_offset == -1) {
        // Richiesta di un nuovo frame fisico alla Coremap
        pa = page_alloc(pageallign_va);

        // Aggiornamento della pagetable, associando all'indirizzo virtuale il frame fisico appena allocato
        KASSERT((pa & PAGE_FRAME) == pa);
        pt_set_pa(as->pt, fault_addr, pa);
        if (seg->p_permission == PF_S) // Se il fault si verifica nel segmento dello stack, dobbiamo azzerare la pagina
        {   
            // In C, le variabili non inizializzate non sono garantite ad avere un valore specifico.
            // Pertanto, se una nuova pagina non viene azzerata prima di essere utilizzata, potrebbe contenere
            // dati arbitrari che potrebbero causare un comportamento imprevisto del programma.
            // Azzerando la pagina, garantiamo che sia inizializzata con uno stato noto di tutti zeri.

            bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE); // Azzeriamo la pagina alla sua indirizzo fisico
            increment_statistics(STATISTICS_PAGE_FAULT_ZERO); // Incrementa il contatore delle pagine azzerate
        }
        new_page = 1;
    }
    else if(swap_offset >= 0) {

        // Se la pagina è stata "swappata fuori", la carichiamo dalla swap
        pa = page_alloc(pageallign_va);  // Alloca una pagina fisica

        // Carica la pagina dal file di swap
        result_swap_in = swap_in(pa, swap_offset);  // Carica la pagina dal file di swap

        KASSERT(result_swap_in == 0);  // Verifica che il caricamento sia riuscito

        // Aggiorna lo stato della pagina nella page table
        // Imposta lo stato della pagina come in memoria
        pt_set_offset(as->pt, pageallign_va, -1);
        pt_set_pa(as->pt, pageallign_va, pa);
    }


    // TODO: Implementazione delL' aggiornamento della TLB
    // Aggiornare o modificare il codice
    if(new_page == 1 && seg->p_permission != PF_S) {
        result = seg_load_page(seg, fault_addr, pa); 
        if (result)
            return EFAULT;
    }    

    increment_statistics(STATISTICS_TLB_FAULT); // Incrementa il contatore dei page fault TLB
    // Disabilita le interruzioni per gestire la TLB in modo sicuro
    spl = splhigh();



    victim = tlb_get_rr_victim();

    ehi = pageallign_va;
    elo = pa | TLBLO_VALID;

    if (seg->p_permission == (PF_R | PF_W) || seg->p_permission == PF_S || seg || seg->p_permission == PF_W)
    {
        elo = elo | TLBLO_DIRTY;
    }

    tlb_read(&victim_ehi, &victim_elo, victim); // Legge la vittima corrente
    if ((victim_elo & TLBLO_VALID) == 1){ // Se la vittima è valida
        increment_statistics(STATISTICS_TLB_FAULT_REPLACE); // Incrementa il contatore dei page fault TLB sostituiti
    }else{
        increment_statistics(STATISTICS_TLB_FAULT_FREE); // Incrementa il contatore dei page fault TLB liberati
    }

    tlb_write(ehi, elo, victim);

    splx(spl);  // Ripristina le interruzioni


    return 0;  // Restituisci un errore
}

    //TODO - Per il momento solo una copia di quello che veniva fatto con DUMBVM
    void
    vm_tlbshootdown(const struct tlbshootdown *ts)
    {
        (void)ts;
        panic("dumbvm tried to do tlb shootdown?!\n");
    }
