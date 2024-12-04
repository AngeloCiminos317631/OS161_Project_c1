#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <addrspace.h>
#include <types.h>

/**
 * Enum per rappresentare lo stato di una pagina fisica:
 * fixed: richiesto dal kernel (non liberabile).
 * free: pagina libera e disponibile per l'allocazione.
 * dirty: pagina richiesta da un programma utente.
 * clean: non ancora richiesta e gestita da `ram_stealmem`.
 */
enum status_t {
    fixed,  // Pagine riservate per il kernel
    free,   // Pagine disponibili per l'uso
    dirty,  // Pagine assegnate a un programma utente
    clean   // Pagine non allocate e disponibili da "rubare" (steal)
};

/**
 * Struttura che rappresenta una singola entry nella coremap.
 * - as: puntatore allo spazio degli indirizzi associato a questa pagina.
 * - status: stato corrente della pagina (enum status_t).
 * - vaddr: indirizzo virtuale della pagina (in intervallo [0x80000000, 0x80000000 + ram_size]).
 * - alloc_size: dimensione dell'allocazione per le pagine contigue richieste.
 */
struct coremap_entry {
    struct addrspace *as;    // Spazio degli indirizzi associato (se applicabile)
    enum status_t status;    // Stato della pagina nella coremap
    vaddr_t vaddr;           // Indirizzo virtuale della pagina
    unsigned int alloc_size; // Dimensione di allocazione (in pagine contigue)
};

/**
 * Inizializza la coremap, allocando la memoria necessaria e configurando
 * ogni entry. Deve essere chiamata una sola volta all'avvio.
 */
void coremap_init(void);

/**
 * Rilascia la memoria e disattiva la coremap, liberando ogni entry.
 * Da chiamare alla terminazione per ripulire le risorse.
 */
void coremap_shutdown(void);


// Funzioni per l'allocazione e liberazione di pagine fisiche per programmi utente

/**
 * Alloca una pagina fisica per un indirizzo virtuale specificato (vaddr),
 * restituendo il relativo indirizzo fisico.
 * @param vaddr Indirizzo virtuale per cui viene richiesta la pagina fisica
 * @param state Parametro per tracciare lo stato della pagina da allocare
 * @return L'indirizzo fisico della pagina assegnata, o 0 in caso di errore.
 */
paddr_t page_alloc(vaddr_t vaddr, int state);

/**
 * Libera una pagina fisica, rendendola disponibile per nuove allocazioni.
 * @param paddr Indirizzo fisico della pagina da liberare.
 */
void page_free(paddr_t paddr);


// Funzioni per l'allocazione e liberazione di pagine contigue per il kernel

/**
 * Alloca un blocco di pagine contigue per il kernel e restituisce
 * l'indirizzo virtuale del blocco.
 * @param npages Numero di pagine contigue da allocare.
 * @return L'indirizzo virtuale del blocco, o 0 in caso di errore.
 */
vaddr_t alloc_kpages(unsigned long npages);

/**
 * Libera un blocco di pagine kernel precedentemente allocato,
 * specificato dall'indirizzo virtuale di partenza.
 * @param addr Indirizzo virtuale del blocco di pagine da liberare.
 */
void free_kpages(vaddr_t addr);

#endif