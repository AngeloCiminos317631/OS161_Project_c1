#ifndef PT_H
#define PT_H

/* Costanti per la gestione delle page table */
#define SIZE_PT_OUTER 1024         // Numero di entry nella outer table
#define SIZE_PT_INNER 1024         // Numero di entry nella inner table
#define PFN_NOT_USED 0x00000000    // Indica che una pagina non è utilizzata

/* Maschere per estrarre i vari campi da un indirizzo virtuale */
#define P_OUT_MASK 0xFFC00000      // Maschera per il livello outer della page table
#define P_IN_MASK 0x003FF000       // Maschera per il livello inner della page table
#define D_MASK 0x00000FFF          // Maschera per il displacement (offset interno alla pagina)

/* Strutture dati per la gestione della page table */

/**
 * Entry della inner table (livello 2).
 */
struct pt_inner_entry {
    unsigned int valid;   // Indica se la pagina è valida (1: valida, 0: non valida)
    paddr_t pfn;          // Physical Frame Number (numero di frame fisico)
};

/**
 * Entry della outer table (livello 1).
 */
struct pt_outer_entry {
    unsigned int valid;               // Indica se l'inner table associata è valida
    unsigned int size;                // Dimensione dell'inner table
    struct pt_inner_entry* pages;     // Puntatore all'inner table (array di entry)
};

/**
 * Struttura principale della page table (outer table).
 */
struct pt_directory {
    unsigned int size;                // Dimensione della outer table (numero di entry)
    struct pt_outer_entry* pages;     // Puntatore alla outer table (array di entry)
};

/* Dichiarazioni delle funzioni di gestione della page table */

/**
 * Crea una nuova page table a due livelli.
 * Inizialmente tutte le entry dell'outer table puntano a NULL.
 * @return Puntatore alla nuova struttura di page table.
 */
struct pt_directory* pt_create(void);

/**
 * Distrugge una page table a due livelli, liberando tutta la memoria associata.
 * @param pt Puntatore alla struttura di page table da distruggere.
 */
void pt_destroy(struct pt_directory* pt);

/**
 * Definisce una nuova inner table per una specifica entry della outer table.
 * Viene allocata una nuova inner table e inizializzata con valori di default.
 * @param pt Puntatore alla page table.
 * @param va Indirizzo virtuale che richiede una nuova inner table.
 */
void pt_define_inner(struct pt_directory* pt, vaddr_t va);

// VALUTARE SEMPRE CON SIMONE
void pt_destroy_inner(struct pt_outer_entry pt_inner);

// /**
//  * Distrugge una inner table e libera la memoria associata.
//  * @param pt_inner Puntatore alla inner table da distruggere.
//  */
// void pt_destroy_inner(struct pt_inner_entry* pt_inner);

/**
 * Recupera l'indirizzo fisico corrispondente a un indirizzo virtuale.
 * Se non esiste una mappatura valida, restituisce PFN_NOT_USED.
 * @param pt Puntatore alla page table.
 * @param va Indirizzo virtuale da tradurre.
 * @return Indirizzo fisico corrispondente o PFN_NOT_USED se la mappatura non è valida.
 */
paddr_t pt_get_pa(struct pt_directory* pt, vaddr_t va);

/**
 * Imposta una mappatura tra un indirizzo virtuale e un indirizzo fisico.
 * Se necessario, viene allocata e inizializzata una nuova inner table.
 * @param pt Puntatore alla page table.
 * @param va Indirizzo virtuale da mappare.
 * @param pa Indirizzo fisico corrispondente.
 */
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa);

/**
 * Invalida tutte le mappature in un contesto di page table.
 * Funzione eventualmente da implementare nei prossimi commit.
 * @param pt Puntatore alla struttura di page table da invalidare.
 */
// void pt_invalidate_context(struct pt_directory* pt);

#endif /* PT_H */