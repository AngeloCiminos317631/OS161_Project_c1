#include <types.h>
#include <lib.h>
#include <synch.h>
#include <spl.h>
#include <statistics.h>

// Spinlock per la sincronizzazione durante l'accesso ai contatori
static struct spinlock statistics_spinlock = SPINLOCK_INITIALIZER;

// Array di contatori per le statistiche
static unsigned int counters[N_STATS];

// Nomi delle statistiche (in ordine corrispondente ai contatori)
static const char *statistics_names[] = {
    "TLB Faults",
    "TLB Faults with Free",
    "TLB Faults with Replace",
    "TLB Invalidations",
    "TLB Reloads",
    "Page Faults (Zeroed)",
    "Page Faults (Disk)",
    "Page Faults from ELF",
    "Page Faults from Swapfile",
    "Swapfile Writes",
};

// Flag che indica se il sistema di statistiche è attivo
static unsigned int is_active = 0;

/*
 * Inizializza il sistema di statistiche, impostando tutti i contatori a zero.
 * Inoltre, attiva il flag `is_active` per indicare che il sistema è operativo.
 */
void init_statistics(void) {
    int i = 0;
    spinlock_acquire(&statistics_spinlock); // Protezione durante l'inizializzazione
    for (i = 0; i < N_STATS; i++) {
        counters[i] = 0;
    }
    is_active = 1; // Attiva il sistema di statistiche
    spinlock_release(&statistics_spinlock);
}

/*
 * Incrementa il contatore specificato da `stat`.
 * Verifica che il sistema sia attivo e che l'indice del contatore sia valido.
 */
void increment_statistics(unsigned int stat) {
    spinlock_acquire(&statistics_spinlock); // Protezione durante l'incremento
    if (is_active == 1) {
        KASSERT(stat < N_STATS); // Verifica che `stat` sia un indice valido
        counters[stat] += 1;     // Incrementa il contatore specificato
    }
    spinlock_release(&statistics_spinlock);
}

/*
 * Stampa tutte le statistiche e verifica la consistenza dei dati.
 * Effettua delle somme parziali e controlla che i totali siano coerenti.
 */
void print_all_statistics(void) {
    int i = 0;

    // Variabili per somme parziali e controlli di consistenza
    int fr = 0;               // Somma di "TLB Faults with Free" e "TLB Faults with Replace"
    int tlbr_pfd_pfz = 0;     // Somma di "TLB Reloads", "Page Faults (Disk)" e "Page Faults (Zeroed)"
    int pfelf_pfswp = 0;      // Somma di "Page Faults from ELF" e "Page Faults from Swapfile"
    int tlb_faults = 0;       // Totale di "TLB Faults"
    int pf_disk = 0;          // Totale di "Page Faults (Disk)"

    // Se il sistema non è attivo, esce senza stampare nulla
    if (is_active == 0) {
        return;
    }

    kprintf("VM STATISTICS:\n");
    for (i = 0; i < N_STATS; i++) {
        // Stampa il nome della statistica e il relativo contatore
        kprintf("%25s = %10d\n", statistics_names[i], counters[i]);
    }

    // Calcola somme parziali
    tlb_faults = counters[STATISTICS_TLB_FAULT];
    pf_disk = counters[STATISTICS_PAGE_FAULT_DISK];
    fr = counters[STATISTICS_TLB_FAULT_FREE] + counters[STATISTICS_TLB_FAULT_REPLACE];
    tlbr_pfd_pfz = counters[STATISTICS_TLB_RELOAD] + counters[STATISTICS_PAGE_FAULT_DISK] + counters[STATISTICS_PAGE_FAULT_ZERO];
    pfelf_pfswp = counters[STATISTICS_ELF_FILE_READ] + counters[STATISTICS_SWAP_FILE_READ];

    /* Controlli di consistenza */
    if (tlb_faults != fr) {
        kprintf("WARNING: TLB Faults (%d) != TLB Faults with Free + TLB Faults with Replace (%d)\n", tlb_faults, fr);
    }

    if (tlb_faults != tlbr_pfd_pfz) {
        kprintf("WARNING: TLB Faults (%d) != TLB Reloads + Page Faults (Zeroed) + Page Faults (Disk) (%d)\n", tlb_faults, tlbr_pfd_pfz);
    }

    if (pf_disk != pfelf_pfswp) {
        kprintf("WARNING: Page Faults (Disk) (%d) != ELF File reads + Swapfile reads (%d)\n", pf_disk, pfelf_pfswp);
    }
}
