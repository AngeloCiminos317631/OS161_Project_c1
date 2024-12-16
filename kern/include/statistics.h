#ifndef STATISTICS_H
#define STATISTICS_H

/* Definizione di tutte le statistiche richieste */
#define STATISTICS_TLB_FAULT              0  // Errore TLB (Translation Lookaside Buffer)
#define STATISTICS_TLB_FAULT_FREE         1  // Errore TLB risolto con un frame libero
#define STATISTICS_TLB_FAULT_REPLACE      2  // Errore TLB risolto con la sostituzione di un'entry
#define STATISTICS_TLB_INVALIDATE         3  // Invalidate di un'entry nella TLB
#define STATISTICS_TLB_RELOAD             4  // Ricaricamento di un'entry nella TLB
#define STATISTICS_PAGE_FAULT_ZERO        5  // Page fault su una pagina zero-initialized
#define STATISTICS_PAGE_FAULT_DISK        6  // Page fault risolto leggendo dal disco
#define STATISTICS_ELF_FILE_READ          7  // Lettura di un file ELF (Executable and Linkable Format)
#define STATISTICS_SWAP_FILE_READ         8  // Lettura da un file di swap
#define STATISTICS_SWAP_FILE_WRITE        9  // Scrittura su un file di swap
#define N_STATS                           10 // Numero totale delle statistiche

/* Funzione per inizializzare tutte le statistiche */
void init_statistics(void);

/* Funzione per incrementare il contatore di una statistica specifica */
void increment_statistics(unsigned int stat);

/* Funzione per stampare tutte le statistiche */
void print_all_statistics(void);

#endif /* STATISTICS_H */
