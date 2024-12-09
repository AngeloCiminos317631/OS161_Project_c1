#ifndef _VM_TLB_H_
#define _VM_TLB_H_
#include <types.h>

/**
 * Verifica e aggiorna le voci del TLB associate a un indirizzo fisico specifico.
 *
 * @param pa_victim Indirizzo fisico della pagina vittima.
 * @param new_va Nuovo indirizzo virtuale da associare all'indirizzo fisico.
 * @param state Stato della nuova mappatura (es. diritti di accesso).
 * @return 1 se il TLB è stato aggiornato, 0 altrimenti.
 */
 
int tlb_check_victim_pa(paddr_t pa_victim, vaddr_t new_va, int state);

/**
 * Rimuove una voce dal TLB associata a un indirizzo virtuale.
 *
 * @param va Indirizzo virtuale da rimuovere.
 * @return 1 se la voce è stata rimossa, 0 altrimenti.
 */
int tlb_remove_by_va(vaddr_t va);
#endif