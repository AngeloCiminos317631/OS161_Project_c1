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
#include <coremap.h>
#include <vmc1.h>

/* 
 * Funzione che chiama coremap_init() per inizializzare la coremap.
 */
void vm_bootstrap(void) {
    coremap_init();
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
    int i, spl;
    uint32_t ehi, elo;
    struct addrspace *as;
    paddr_t pa;

    //TODO
    fault_addr &= PAGE_FRAME; //Per eliminare l'offset e lavorare con l'indirizzo base della pagina

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

    // Cerchiamo l'indirizzo fisico corrispondente nel page table
    pa = pt_get_pa(as->pt, fault_addr);

    // Se non esiste, dobbiamo allocare un nuovo frame
    if(pa == PFN_NOT_USED) {
        // Richiesta di un nuovo frame fisico alla Coremap
        pa = page_alloc(faultaddress);

        // Aggiornamento della pagetable, associando all'indirizzo virtuale il frame fisico appena allocato
        KASSERT((pa & PAGE_FRAME) == pa);
        pt_set_pa(as->pt, faultaddress, pa);
    }

    // TODO: Implementazione delL' aggiornamento della TLB
    // Aggiornare o modificare il codice


    // Disabilita le interruzioni per gestire la TLB in modo sicuro
    spl = splhigh();

    // Cerca una voce libera nella TLB per scrivere la mappatura
    for (i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i); // Leggi la voce dalla TLB
        if (elo & TLBLO_VALID) {
            continue; // Se la voce è già valida, saltala
        }
        
        // Se trovi una voce libera, aggiorna la TLB
        ehi = fault_addr;  // Imposta l'indirizzo virtuale
        elo = pa | TLBLO_DIRTY | TLBLO_VALID;  // Imposta l'indirizzo fisico, marcando la voce come valida e "sporca" (modificata)
        DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", fault_addr, pa); // Debug per tracciare la mappatura
        tlb_write(ehi, elo, i);  // Scrivi la mappatura nella TLB

        splx(spl);  // Ripristina le interruzioni
        return 0;  // La fault è stata gestita con successo
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
