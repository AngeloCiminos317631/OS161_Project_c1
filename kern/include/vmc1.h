#ifndef VMC1_H
#define VMC1_H

#include <vm.h>

#define VMC1_STACKPAGES 12  // Numero di pagine riservate per lo stack

/*
 * Inizializza il sottosistema di memoria virtuale.
 * Configura le risorse necessarie per la gestione della memoria virtuale.
 */
void vm_bootstrap(void);

/*
 * Verifica se il sistema può entrare in stato di attesa (sleep).
 * Assicura che non ci siano operazioni critiche in corso.
 */
void vm_can_sleep(void);

/*
 * Libera le risorse del sottosistema di memoria virtuale.
 * Viene chiamata durante lo spegnimento del sistema o quando la memoria virtuale non è più necessaria.
 */
void vm_shutdown(void);

/*
 * Gestisce un page fault.
 * Risolve l'accesso a una pagina non mappata, recuperando l'indirizzo fisico o allocando una nuova pagina.
 */
int vm_fault(int fault_type, vaddr_t fault_addr);


/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *ts);

#endif
