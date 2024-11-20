#ifndef VMC1_H
#define VMC1_H

#include <vm.h>

#define VMC1_STACKPAGES 18

/*
 * Funzione per inizializzare il sottosistema
 * di memoria virtuale.
 */
void vm_bootstrap(void);

/*
 * Funzione per controllare se il sistema Ã¨ in uno stato sicuro
 * per consentire l'attesa (o sleep).
 */
void vm_can_sleep(void);

/*
 * Funzione per liberare risorse allocate nel sottosistema
 * della memoria virtuale.
 */
void vm_shutdown(void);

/*
 * Funzione per gestire i page fault.
 */
void vm_fault_handler(int fault_type, vaddr_t fault_addr);

#endif