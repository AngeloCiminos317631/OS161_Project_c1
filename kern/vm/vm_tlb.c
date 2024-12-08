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

/**
 * Verifica e aggiorna le voci del TLB associate a un indirizzo fisico specifico.
 *
 * @param pa_victim Indirizzo fisico della pagina vittima.
 * @param new_va Nuovo indirizzo virtuale da associare all'indirizzo fisico.
 * @param state Stato della nuova mappatura (es. diritti di accesso).
 * @return 1 se il TLB è stato aggiornato, 0 altrimenti.
 */
int tlb_check_victim_pa(paddr_t pa_victim, vaddr_t new_va, int state) {
    int i;               // Indice per scorrere le voci del TLB
    uint32_t ehi, elo;   // Variabili per memorizzare indirizzo virtuale e fisico di una voce TLB
    int spl;             // Livello di interrupt salvato
    int updated = 0;     // Flag che indica se una voce TLB è stata aggiornata

    // Disabilita le interruzioni per operare in sicurezza sul TLB
    spl = splhigh();

    // Scorre tutte le voci del TLB
    for (i = 0; i < NUM_TLB; i++) {
        // Legge la voce TLB corrente
        tlb_read(&ehi, &elo, i);

        // Salta le voci valide, poiché non possono essere sovrascritte
        if (elo & TLBLO_VALID) {
            continue;
        }

        // Controlla se l'indirizzo fisico della voce corrente corrisponde alla pagina vittima
        if ((elo & TLBLO_PPAGE) == pa_victim) {
            // Aggiorna la voce con il nuovo indirizzo virtuale e stato
            ehi = new_va;                              // Nuovo indirizzo virtuale
            elo = pa_victim | state | TLBLO_VALID;     // Combina indirizzo fisico, stato e bit di validità
            tlb_write(ehi, elo, i);                    // Scrive la voce aggiornata nel TLB
            updated = 1;                               // Segna che è stata effettuata un'operazione di aggiornamento
        }
    }

    // Ripristina il livello di interrupt originale
    splx(spl);

    // Restituisce 1 se è stato effettuato un aggiornamento, 0 altrimenti
    return updated;
}

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
