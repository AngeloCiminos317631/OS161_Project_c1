#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <coremap.h>
#include <vmc1.h>
#include <mips/tlb.h>
#include <vm_tlb.h>


// Rimuove la voce del TLB corrispondente all'indirizzo virtuale dato (va)
// Se la voce è presente, la invalida.
int tlb_remove_by_va(vaddr_t va) {
    int spl, index;
    struct addrspace *as;

    // Ottieni l'address space del processo corrente
    as = proc_getas();
    if (as == NULL) {
        /*
         * Thread kernel senza un address space; lasciamo inalterato
         * l'address space precedente.
         */
        return -1;
    }

    // Disabilita le interruzioni durante la manipolazione del TLB
    spl = splhigh();

    // Cerca la voce nel TLB corrispondente all'indirizzo virtuale (va)
    index = tlb_probe(va, 0);
    if (index >= 0) {
        // Se la voce è presente, la invalida
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }

    // Ripristina lo stato delle interruzioni
    splx(spl);

    // Restituisce 0 per indicare che la rimozione è andata a buon fine
    return 0;
}
