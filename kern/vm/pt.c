include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>

/* Costanti */
#define PAGE_SIZE 4096
#define SIZE_PT_OUTER 64
#define SIZE_PT_INNER 256
#define PFN_NOT_USED 0xFFFFFFFF

/* Strutture */
struct pt_inner_entry {
    uint8_t valid;
    paddr_t pfn;
};

struct pt_outer_entry {
    uint8_t valid;
    struct pt_inner_entry* pages;
};

struct pt_directory {
    unsigned int size;
    struct pt_outer_entry* pages;
};

/* Funzioni di utilità */
static int get_outer_index(vaddr_t va) {
    return (va >> 22) & 0x3F; // Usa solo i bit per l'outer table
}

static int get_inner_index(vaddr_t va) {
    return (va >> 12) & 0xFF; // Usa solo i bit per l'inner table
}

static int get_page_offset(vaddr_t va) {
    return va & 0xFFF; // Offset interno alla pagina
}

/* 
 * Crea una nuova page table a due livelli.
 * Alloca memoria per la struttura di gestione e inizializza le sue entries.
 */
struct pt_directory* pt_create(void) {
    struct pt_directory* pt = kmalloc(sizeof(struct pt_directory));
    KASSERT(pt != NULL);

    pt->size = SIZE_PT_OUTER;
    pt->pages = kmalloc(sizeof(struct pt_outer_entry) * SIZE_PT_OUTER);
    KASSERT(pt->pages != NULL);

    for (int i = 0; i < pt->size; i++) {
        pt->pages[i].valid = 0;
        pt->pages[i].pages = NULL;
    }

    return pt;
}

/* 
 * Distrugge una inner table specifica, liberando la memoria associata.
 */
static void pt_destroy_inner(struct pt_inner_entry* inner_pages) {
    KASSERT(inner_pages != NULL);
    kfree(inner_pages);
}

/* 
 * Distrugge un'intera page table, liberando tutte le risorse allocate.
 */
void pt_destroy(struct pt_directory* pt) {
    KASSERT(pt != NULL);

    for (int i = 0; i < pt->size; i++) {
        if (pt->pages[i].valid) {
            pt_destroy_inner(pt->pages[i].pages);
        }
    }

    kfree(pt->pages);
    kfree(pt);
}

/* 
 * Invalida tutte le mappature in un contesto (page table),
 * rendendo tutte le entries non valide.
 */
void pt_invalidate_context(struct pt_directory* pt) {
    KASSERT(pt != NULL);

    for (int i = 0; i < pt->size; i++) {
        if (pt->pages[i].valid) {
            pt_destroy_inner(pt->pages[i].pages);
            pt->pages[i].valid = 0;
        }
    }
}

/* 
 * Definisce una nuova inner table per un indirizzo virtuale specifico.
 * Alloca e inizializza la inner table.
 */
static void pt_define_inner(struct pt_directory* pt, vaddr_t va) {
    int index = get_outer_index(va);
    KASSERT(!pt->pages[index].valid);

    pt->pages[index].pages = kmalloc(sizeof(struct pt_inner_entry) * SIZE_PT_INNER);
    KASSERT(pt->pages[index].pages != NULL);

    pt->pages[index].valid = 1;

    for (int i = 0; i < SIZE_PT_INNER; i++) {
        pt->pages[index].pages[i].valid = 0;
        pt->pages[index].pages[i].pfn = PFN_NOT_USED;
    }
}

/* 
 * Recupera l'indirizzo fisico corrispondente a un indirizzo virtuale,
 * se esiste una mappatura valida. Altrimenti, restituisce PFN_NOT_USED.
 */
paddr_t pt_get_pa(struct pt_directory* pt, vaddr_t va) {
    int outer = get_outer_index(va);
    int inner = get_inner_index(va);

    if (!pt->pages[outer].valid) return PFN_NOT_USED;
    if (!pt->pages[outer].pages[inner].valid) return PFN_NOT_USED;

    return pt->pages[outer].pages[inner].pfn;
}

/* 
 * Imposta una mappatura tra un indirizzo virtuale e un indirizzo fisico.
 * Se necessario, crea una nuova inner table.
 */
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa) {
    int outer = get_outer_index(va);
    int inner = get_inner_index(va);

    if (!pt->pages[outer].valid) {
        pt_define_inner(pt, va);
    }

    pt->pages[outer].pages[inner].valid = 1;
    pt->pages[outer].pages[inner].pfn = pa;
}