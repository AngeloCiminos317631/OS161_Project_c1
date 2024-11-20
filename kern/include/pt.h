#ifndef PT_H
#define PT_H

#include <types.h>

/* Costanti */
#define PAGE_SIZE 4096           // Dimensione di una pagina
#define SIZE_PT_OUTER 64         // Numero di entries nell'outer table
#define SIZE_PT_INNER 256        // Numero di entries nell'inner table
#define PFN_NOT_USED 0xFFFFFFFF  // Indica che una pagina non è utilizzata

#define TLB_SIZE 16              // Dimensione della cache TLB

/* Strutture */
struct pt_inner_entry {
    uint8_t valid;     // Indica se la pagina è valida
    paddr_t pfn;       // Physical Frame Number (indirizzo fisico)
};

struct pt_outer_entry {
    uint8_t valid;                  // Indica se la inner table è valida
    struct pt_inner_entry* pages;   // Puntatore alla inner table
};

struct pt_directory {
    unsigned int size;              // Dimensione dell'outer table
    struct pt_outer_entry* pages;   // Puntatore alla outer table
};

/* Funzioni di gestione della page table */

/**
 * Crea una nuova page table a due livelli.
 * @return Puntatore alla nuova struttura di page table.
 */
struct pt_directory* pt_create(void);

/**
 * Distrugge una page table a due livelli, liberando tutta la memoria associata.
 * @param pt Puntatore alla struttura di page table da distruggere.
 */
void pt_destroy(struct pt_directory* pt);

/**
 * Invalida tutte le mappature in un contesto (page table).
 * @param pt Puntatore alla struttura di page table da invalidare.
 */
void pt_invalidate_context(struct pt_directory* pt);

/**
 * Recupera l'indirizzo fisico corrispondente a un indirizzo virtuale.
 * @param pt Puntatore alla page table.
 * @param va Indirizzo virtuale da tradurre.
 * @return Indirizzo fisico corrispondente o PFN_NOT_USED se non valido.
 */
paddr_t pt_get_pa(struct pt_directory* pt, vaddr_t va);

/**
 * Imposta una mappatura tra un indirizzo virtuale e un indirizzo fisico.
 * @param pt Puntatore alla page table.
 * @param va Indirizzo virtuale da mappare.
 * @param pa Indirizzo fisico corrispondente.
 */
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa);

#endif /* PT_H */