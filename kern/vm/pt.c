#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
#include <coremap.h> // include header per modulo Coremap

#include <pt.h>
#include <vmc1.h>

/* Funzioni di utilità per l'estrazione di indici e offset dall'indirizzo virtuale */

/**
 * Estrae l'indice per la outer table dall'indirizzo virtuale.
 * @param va: indirizzo virtuale
 * @return indice nella outer table
 */
static int get_outer_index(vaddr_t va) {
    return (va & P_OUT_MASK) >> 22; // Usa i bit corrispondenti alla outer table
    // si fa lo shift per portare i bit di interesse al fondo e usare il valore come indice
}

/**
 * Estrae l'indice per la inner table dall'indirizzo virtuale.
 * @param va: indirizzo virtuale
 * @return indice nella inner table
 */
static int get_inner_index(vaddr_t va) {
    return (va & P_IN_MASK) >> 12; // Usa i bit corrispondenti alla inner table
    // si fa lo shift per portare i bit di interesse al fondo e usare il valore come indice
}

/**
 * Estrae l'offset all'interno di una pagina dall'indirizzo virtuale.
 * @param va: indirizzo virtuale
 * @return offset all'interno della pagina
 */
static int get_page_offset(vaddr_t va) {
    return va & D_MASK; // Usa i bit meno significativi (offset)
}

/* Gestione della struttura della page table */

/**
 * Crea una nuova directory di pagine (outer table).
 * Alloca memoria per la struttura e inizializza tutte le entries come non valide.
 * @return puntatore alla nuova directory di pagine
 */
struct pt_directory* pt_create(void) {
    unsigned int i;
    struct pt_directory *pt;

    pt = kmalloc(sizeof(struct pt_directory));
    KASSERT(pt != NULL); // Assicura che la memoria sia stata allocata

    pt->size = SIZE_PT_OUTER;
    pt->pages = kmalloc(sizeof(struct pt_outer_entry) * SIZE_PT_OUTER);
    KASSERT(pt->pages != NULL); // Assicura che la memoria sia stata allocata

    // Inizializza tutte le entries della outer table come non valide
    for (i = 0; i < pt->size; i++) {
        pt->pages[i].pages = NULL;
        pt->pages[i].valid = 0;
    }

    return pt;
}

/**
 * Libera la memoria associata a una tabella interna (inner table) della paginazione.
 * 
 * @param pt_inner Una struttura rappresentante una entry di livello superiore che punta
 *                 a una inner table.
 */
void pt_destroy_inner(struct pt_outer_entry pt_inner) {

    unsigned int i; // Variabile per l'indice del ciclo for

    KASSERT(pt_inner.pages != NULL);
    KASSERT(pt_inner.size != 0);
    KASSERT(pt_inner.valid != 0);

    // Itera su tutte le pagine della tabella interna
    for (i = 0; i < pt_inner.size; i++) {
        // Verifica se l'entry corrente è valida e che la pagina non sia stata "swappata"
        if (pt_inner.pages[i].valid && pt_inner.pages[i].swapped_out != 1) {
            // Libera il frame fisico associato all'indice di pagina (PFN - Page Frame Number)
            page_free(pt_inner.pages[i].pfn);
        }
    }

    // Libera la memoria allocata per l'array delle pagine nella tabella interna
    kfree(pt_inner.pages);
}


/**
 * Libera tutta la memoria associata a una directory di pagine (outer table e inner tables).
 * @param pt: puntatore alla directory di pagine
 */
void pt_destroy(struct pt_directory* pt) {
    unsigned int i;

    KASSERT(pt != NULL); // Assicura che il puntatore sia valido

    // Itera su tutte le entries della outer table
    for (i = 0; i < pt->size; i++) {
        if (pt->pages[i].pages != NULL && pt->pages[i].valid) {
            pt_destroy_inner(pt->pages[i]); // Libera la inner table se valida
        }
    }

    // Libera la memoria della outer table e della struttura
    kfree(pt->pages);
    kfree(pt);
}

/**
 * Definisce una nuova inner table per una specifica entry della outer table.
 * Alloca memoria per la inner table e inizializza le sue entries come non valide.
 * @param pt: puntatore alla outer table
 * @param va: indirizzo virtuale che richiede la nuova inner table
 */
static void pt_define_inner(struct pt_directory* pt, vaddr_t va) {
    unsigned int index, i;

    index = get_outer_index(va); // Ottiene l'indice della outer table
    KASSERT(pt->pages[index].valid == 0); // Assicura che l'entry non sia già valida

    // Alloca memoria per la nuova inner table
    pt->pages[index].size = SIZE_PT_INNER;
    pt->pages[index].pages = kmalloc(sizeof(struct pt_inner_entry) * SIZE_PT_INNER);
    KASSERT(pt->pages[index].pages != NULL);
    pt->pages[index].valid = 1;

    // Inizializza tutte le entries della inner table come non valide
    for (i = 0; i < pt->pages[index].size; i++) {
        pt->pages[index].pages[i].valid = 0;
        pt->pages[index].pages[i].pfn = PFN_NOT_USED;
        pt->pages[index].pages[i].swapped_out = -1;
    }
}

/**
 * Recupera l'indirizzo fisico associato a un indirizzo virtuale.
 * Restituisce PFN_NOT_USED se non esiste una mappatura valida.
 * @param pt: puntatore alla directory di pagine
 * @param va: indirizzo virtuale da tradurre
 * @return indirizzo fisico corrispondente o PFN_NOT_USED
 */
int pt_get_pa(struct pt_directory* pt, vaddr_t va) {
    unsigned int outer, inner, d;

    outer = get_outer_index(va);
    KASSERT(outer < SIZE_PT_OUTER);

    inner = get_inner_index(va);
    KASSERT(inner < SIZE_PT_INNER);

    d = get_page_offset(va);
    KASSERT(d < PAGE_SIZE);

    // && !pt->pages[outer].pages[inner].swapped_out 
    if (pt->pages[outer].valid) {
        if (pt->pages[outer].pages[inner].valid) {
            return pt->pages[outer].pages[inner].pfn;
        }
    }

    return PFN_NOT_USED;
}

/**
 * Imposta una mappatura tra un indirizzo virtuale e un indirizzo fisico.
 * Se necessario, alloca una nuova inner table.
 * @param pt: puntatore alla directory di pagine
 * @param va: indirizzo virtuale
 * @param pa: indirizzo fisico
 */
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa) {
    unsigned int outer, inner, d;

    outer = get_outer_index(va);
    KASSERT(outer < SIZE_PT_OUTER);

    inner = get_inner_index(va);
    KASSERT(inner < SIZE_PT_INNER);

    d = get_page_offset(va);
    KASSERT(d < PAGE_SIZE);

    // Alloca una nuova inner table se necessario
    if (!pt->pages[outer].valid) {
        pt_define_inner(pt, va);
    }

    // Imposta la mappatura nella inner table
    KASSERT(pt->pages[outer].valid == 1);
    pt->pages[outer].pages[inner].valid = 1;
    pt->pages[outer].pages[inner].pfn = pa;
}

/**
 * Recupera lo stato di una pagina in una page table.
 * Controlla se la pagina è swappata su disco, valida o non valida.
 *
 * @param pt La page table in cui cercare.
 * @param va L'indirizzo virtuale della pagina.
 * @return 0 se la pagina è in memoria, 1 se è swappata, 2 se non è valida.
 */
off_t pt_get_state(struct pt_directory* pt, vaddr_t va) {
    unsigned int outer, inner, d;
    off_t flag;

    // Estrai l'indice della outer table
    outer = get_outer_index(va);
    KASSERT(outer < SIZE_PT_OUTER); // Verifica che l'indice sia valido

    // Estrai l'indice della inner table
    inner = get_inner_index(va);
    KASSERT(inner < SIZE_PT_INNER); // Verifica che l'indice sia valido

    // Estrai l'offset nella pagina
    d = get_page_offset(va);
    KASSERT(d < PAGE_SIZE); // Verifica che l'offset sia valido

    // Controlla se la outer table è valida
    if (pt->pages[outer].valid) {
        // Controlla se la inner table è valida
        if (pt->pages[outer].pages[inner].valid) {
            // Recupera il valore del campo swapped_out
            flag = pt->pages[outer].pages[inner].swapped_out;
        } else {
            return -1; // Pagina non valida
        }
    } else {
        return -1; // Outer table non valida
    }

    return flag; // Restituisci lo stato della pagina
}

/**
 * Imposta lo stato di una pagina in una page table.
 * Permette di aggiornare il campo swapped_out per indicare se la pagina è swappata o meno.
 *
 * @param pt La page table in cui aggiornare lo stato.
 * @param va L'indirizzo virtuale della pagina.
 * @param state Il nuovo stato: 0 per in memoria, 1 per swappata.
 * @param pa L'indirizzo fisico della pagina.
 */
void pt_set_state(struct pt_directory* pt, vaddr_t va, off_t state, paddr_t pa) {
    volatile unsigned int outer, inner, d;

    // Estrai l'indice della outer table
    outer = get_outer_index(va);
    KASSERT(outer < SIZE_PT_OUTER); // Verifica che l'indice sia valido

    // Estrai l'indice della inner table
    inner = get_inner_index(va);
    KASSERT(inner < SIZE_PT_INNER); // Verifica che l'indice sia valido

    // Estrai l'offset nella pagina
    d = get_page_offset(va);
    KASSERT(d < PAGE_SIZE); // Verifica che l'offset sia valido

    // Alloca una nuova inner table se necessario
    if(!pt->pages[outer].valid) {
        pt_define_inner(pt, va);
    }

    // Assicurati che la outer table sia valida
    KASSERT(pt->pages[inner].valid == 1);

    // Marca la pagina come valida
    pt->pages[inner].pages[outer].valid = 1;

    // Aggiorna il campo swapped_out con il nuovo stato
    pt->pages[inner].pages[outer].swapped_out = state;

    // Imposta l'indirizzo fisico della pagina
    pt->pages[outer].pages[inner].pfn = pa;
}
