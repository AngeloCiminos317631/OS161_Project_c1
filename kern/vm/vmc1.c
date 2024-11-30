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
}

/* 
 * Funzione che richiama coremap_shutdown() liberando tutte le risorse 
 * allocate per la coremap.
 */
void vm_shutdown(void) {
    coremap_shutdown();
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
    int i, spl, found, new_page, result;
    unsigned int victim;
    uint32_t ehi, elo;
    struct addrspace *as;
    paddr_t pa;
    struct segment * seg;
    vaddr_t pageallign_va;

    //TODO
    // fault_addr &= PAGE_FRAME; //Per eliminare l'offset e lavorare con l'indirizzo base della pagina
    pageallign_va = fault_addr & PAGE_FRAME;

    // Gestione dei diversi tipi di fault
    switch (fault_type) {
        case VM_FAULT_READONLY:
            // Se la fault è su una pagina di sola lettura, restituiamo EACCES
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

    // Cerchiamo l'indirizzo fisico corrispondente nel page table
    pa = pt_get_pa(as->pt, fault_addr);

    // Se non esiste, dobbiamo allocare un nuovo frame
    if(pa == PFN_NOT_USED) {
        // Richiesta di un nuovo frame fisico alla Coremap
        pa = page_alloc(pageallign_va);

        // Aggiornamento della pagetable, associando all'indirizzo virtuale il frame fisico appena allocato
        KASSERT((pa & PAGE_FRAME) == pa);
        pt_set_pa(as->pt, fault_addr, pa);
        if (seg->p_permission == PF_S)
        {
            zero(PADDR_TO_KVADDR(pa), PAGE_SIZE);
        }
        new_page = 1;
    }

    // TODO: Implementazione delL' aggiornamento della TLB
    // Aggiornare o modificare il codice
    if(new_page == 1 && seg->p_permission != PF_S) {
        result = seg_load_page(seg, fault_addr, pa); 
        if (result)
            return EFAULT;
    }    

    // Disabilita le interruzioni per gestire la TLB in modo sicuro
    spl = splhigh();

    // Cerca se l'indirizzo virtuale fault_addr è già presente nella TLB
    found = tlb_probe(fault_addr, 0);
    if (found < 0) {
        // Se l'indirizzo non è trovato nella TLB (found < 0), bisogna cercare una posizione libera per la traduzione

        // Itera su tutte le entry della TLB
        for (i = 0; i < NUM_TLB; i++) {
            // Legge la coppia (ehi, elo) dall'entry i della TLB
            tlb_read(&ehi, &elo, i);

            // Verifica se l'entry è valida (se contiene una traduzione)
            if (elo & TLBLO_VALID) {
                // Se l'entry è valida, salta questa entry e prova con la successiva
                continue;
            }

            // Se l'entry non è valida, imposta ehi (indirizzo virtuale) con fault_addr (l'indirizzo che ha causato il page fault)
            ehi = pageallign_va;

            // Imposta elo con l'indirizzo fisico pa, segnando la pagina come dirty (potenzialmente modificata) e valida
            elo = pa | TLBLO_DIRTY | TLBLO_VALID;

            // Stampa un messaggio di debug che mostra la traduzione dall'indirizzo virtuale a quello fisico
            DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", fault_addr, pa);

            // Scrive i valori ehi e elo nella TLB all'indice i
            tlb_write(ehi, elo, i);

            // Ripristina il livello di interruzione (sblocca il livello di interruzione precedente)
            splx(spl);

            // Restituisce 0 indicando che il page fault è stato gestito correttamente
            return 0;
        }

        // Se non si è trovata alcuna entry libera (tutte le entry sono valide), bisogna scegliere una "vittima" per la sostituzione

        // Imposta ehi con fault_addr (l'indirizzo virtuale)
        ehi = fault_addr;

        // Imposta elo con l'indirizzo fisico pa e i flag per marcarlo come dirty e valido
        elo = pa | TLBLO_DIRTY | TLBLO_VALID;

        // Sceglie una "vittima" dalla TLB utilizzando la politica Round-Robin (funzione tlb_get_rr_victim)
        victim = tlb_get_rr_victim();

        // Sostituisce l'entry della TLB alla posizione della vittima con la nuova traduzione
        tlb_write(ehi, elo, victim);

        // Restituisce 0 indicando che la gestione del page fault è stata completata correttamente
        return 0;
    }


    // Se non ci sono voci libere nella TLB, stampa un errore
    kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
    splx(spl);  // Ripristina le interruzioni


    return EFAULT;  // Restituisci un errore
}

    //TODO - Per il momento solo una copia di quello che veniva fatto con DUMBVM
    void
    vm_tlbshootdown(const struct tlbshootdown *ts)
    {
        (void)ts;
        panic("dumbvm tried to do tlb shootdown?!\n");
    }
