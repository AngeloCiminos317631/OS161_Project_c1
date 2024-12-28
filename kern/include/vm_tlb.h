#ifndef _VM_TLB_H_
#define _VM_TLB_H_
#include <types.h>


/**
 * Rimuove una voce dal TLB associata a un indirizzo virtuale.
 *
 * @param va Indirizzo virtuale da rimuovere.
 * @return 1 se la voce è stata rimossa, 0 altrimenti.
 */
int tlb_remove_by_va(vaddr_t va);
#endif