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
 * Funzione che gestisce i page fault 
 * @param fault_type: tipo di fault (lettura, scrittura, ecc.) 
 * @param fault_addr: indirizzo della pagina che ha causato il fault
 */
void vm_fault_handler(int fault_type, vaddr_t fault_addr) {
    int i, spl;
    uint32_t ehi, elo;
    struct addrspace *as;
    paddr_t pa;

    // Verifica il tipo di errore di page fault
    if (fault_type == 0) { 
        return EFAULT; 
    }

    switch(fault_type) {
        case VM_FAULT_READONLY:
            // Non gestiamo page fault in caso di tentativi di scrittura su pagine di sola lettura
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            // Gestiamo i casi di lettura e scrittura
            break;
        default:
            // Tipo di fault non riconosciuto
            return EINVAL;
    }

    // Verifica che esista un processo corrente
    if (curproc == NULL) {
        return EFAULT;
    }

    // Recupera l'address space del processo corrente
    as = proc_getas();
    if (as == NULL) {
        // Se l'address space è NULL, probabilemente è un errore del kernel in fase di avvio
        return EFAULT;
    }

    // Cerca l'indirizzo fisico associato a fault_addr nella tabella delle pagine del processo
    pa = pt_get_pa(as->page_table, fault_addr);

    // Se l'indirizzo fisico non è presente, significa che la pagina non è ancora mappata
    if (pa == PFN_NOT_USED) {
        paddr_t new_pa = alloc_kpages(1);
        if (new_pa == 0) {
            // Errore di allocazione di memoria
            return ENOMEM;
        }

        // Mappa il nuovo frame all'indirizzo virtuale 'fault_addr'
        pt_set_pa(as->page_table, fault_addr, new_pa);
        pa = new_pa;
    }

    // Verifica che l'indirizzo fisico sia allineato a una pagina
    KASSERT((pa & PAGE_FRAME) == pa);

    // Disattiva le interruzioni per evitare problemi di concorrenza durante la modifica della TLB
    spl = splhigh();

    // Cerca un entry non valida nella TLB in cui inserire la nuova mappatura
    for (i = 0; i < TLB_NUM; i++) {
        tlb_read(&ehi, &elo, i);
        if (elo & TLBLO_VALID) {
            continue;
        }

        // Configura i campi 'ehi' e 'elo' per la nuova mappatura
        ehi = fault_addr;
        elo = pa | TLBLO_VALID | (fault_type == VM_FAULT_WRITE ? TLBLO_DIRTY : 0) | TLBLO_GLOBAL;
        tlb_write(ehi, elo, i);
        break;  // Esci dopo aver inserito la mappatura
    }

    if (i == TLB_NUM) {
        // Se la TLB è piena
        kprintf("dumbvm: TLB piena!\n");
        splx(spl);
        return EFAULT;
    }

    // Ripristina le interruzioni
    splx(spl);

    return 0;
}