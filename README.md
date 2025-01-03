# Progetto os161-c1

## Introduzione
Il progetto mira a espandere il modulo di gestione della memoria (dumbvm), sostituendolo completamente con un gestore di memoria virtuale più potente basato sulle tabelle delle pagine dei processi. Il progetto richiede inoltre di lavorare sulla TLB.

## Autori
* s292671 Donato Agostino Modugno
* s308455 Simone Colagiovanni
* s317631 Angelo Cimino


## Divisione del lavoro


## Scelte Implemetative
DA MODIFICARE - RISPONDERE A QUESTE RICHIESTE 
( The project goal is to replace dumbvm with a new virtual-memory system that relaxes some (not all) of 
dumbvm’s limitations.  
The new system will implement demand paging (with a page table) with page replacement, according to the following requirements: 
• New TLB support is needed, by implementing a replacement policy for the TLB, so that the kernel 
will not crash if the TLB fills up. 
• On-demand loading of pages: this will allow programs that have address spaces larger than physical memory to run, provided that they do not touch more pages than will fit in physical memory.  • In addition, page replacement (based on victim selection) is needed, so that a new frame can be found when no more free frames are available.  
• Different page table policies can be implemented: e.g. per process page table or Inverted PT, victim selection policies, free frame management, etc. The choice can be discussed and deferred to a later moment. )

## Moduli principali

### addrspace.c
#### Panoramica
Il file `addrspace.c` si occupa della gestione degli address space nel kernel di un sistema operativo. Fornisce funzioni e strutture dati per creare, copiare, attivare, disattivare e distruggere gli address space, oltre a definire regioni e configurare segmenti di memoria per i processi utente. Questa implementazione include funzionalità aggiuntive per la gestione della memoria, delle tabelle delle pagine, della TLB e il monitoraggio delle statistiche.

#### Strutture Dati
```c
struct addrspace {
    struct segment *code;   // Segmento per il codice
    struct segment *data;   // Segmento per i dati
    struct segment *stack;  // Segmento per lo stack
    struct pagetable *pt;   // Page table
};
```
- **`struct addrspace`**: rappresenta l'address space di un processo. I membri principali includono:
    - **Code**: Memorizza il segmento di codice.
    - **Data**: Memorizza il segmento di dati.
    - **Stack**: Memorizza il segmento di stack.
    - **Page table**: Gestisce il mapping della memoria a livello di pagina.

#### Funzioni

- **`as_create(void)`**: utilizzata per creare un nuovo address space. Vengono creati tre segmenti:
    - **code**
    - **data**
    - **stack**
e viene inizializzata la page table. Inoltre, viene inizializzato lo swapfile.

- **`as_copy(struct addrspace *old, struct addrspace **ret)`**: crea una copia di un address space esistente. Viene creata una nuova struttura `addrspace` e vengono copiati i segmenti
    - **code**
    - **data**
    - **stack**.
Inoltre, viene copiata anche la page table.

- **`as_destroy(struct addrspace* as)`**: utilizzata per distruggere un address space. Tutti i segmenti (code, data, stack) vengono distrutti, la page table viene eliminata e il file di swap associato al segmento di codice viene chiuso. Infine, la memoria allocata per l'address space viene liberata.

- **`void as_activate(void)`**: attiva l'address space del processo corrente. Se un processo ha un address space, vengono invalidate tutte le voci della TLB per garantire che le mappature siano aggiornate. Se il processo non ha un address space (ad esempio, è un thread del kernel), non viene eseguita alcuna operazione.

```c
for (i = 0; i < NUM_TLB; i++) {
    tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);  // Invalida tutte le voci della TLB
}
```

- **`as_deactivate(void)`**: Simile a `as_activate`, la funzione `as_deactivate` disattiva l'address space corrente invalidando tutte le voci della TLB. Questo garantisce che le vecchie mappature delle pagine virtuali non siano più valide.

- **`as_define_region(struct addrspace *as, uint32_t type, uint32_t offset ,vaddr_t vaddr, size_t memsize,
		 uint32_t filesize, int readable, int writeable, int executable, int seg_n, struct vnode *v)`**: definisce una regione di memoria in un address space. Essa stabilisce i permessi di accesso (lettura, scrittura, esecuzione) per il segmento specificato e ne determina le caratteristiche, come il tipo, l'offset, l'indirizzo virtuale e la dimensione in memoria. I segmenti possibili sono `code` e `data`.

```c
if (readable) perm = perm | PF_R;     // Imposta il permesso di lettura
if (writeable) perm = perm | PF_W;    // Imposta il permesso di scrittura
if (executable) perm = perm | PF_X;   // Imposta il permesso di esecuzione
res = seg_define(as->code, type, offset, vaddr, filesize, memsize, perm, v); // Definisce il segmento di codice
```

- **`as_prepare_load e as_complete_load`**: non contengono implementazioni specifiche. Sono destinate alla preparazione e al completamento del caricamento di un programma nell'address space. Potrebbero essere implementate in futuro per gestire operazioni specifiche.

- **`as_define_stack(struct addrspace *as, vaddr_t *stackptr)`**: definisce lo stack di un address space. Imposta il puntatore allo stack all'indirizzo iniziale di `USERSTACK`.

- **`as_get_segment(struct addrspace *as, vaddr_t va)`**: restituisce il segmento (code, data o stack) associato a un dato indirizzo virtuale (`va`). Questo permette di identificare a quale parte dell'address space appartiene una determinata pagina.

```c
if (va >= base_seg1 && va <= top_seg1) {
    return as->code;  // Se l'indirizzo è nel segmento di codice, restituisce il segmento di codice
} else if (va >= base_seg2 && va <= top_seg2) {
    return as->data;  // Se l'indirizzo è nel segmento di dati, restituisce il segmento di dati
} else if (va >= base_seg3 && va <= top_seg3) {
    return as->stack; // Se l'indirizzo è nel segmento di stack, restituisce il segmento di stack
}
```
### coremap.c

#### Panoramica
Il modulo **coremap.c** è responsabile della gestione della memoria fisica nel sistema. Tiene traccia dello stato di ogni frame di memoria, supporta l'allocazione e il rilascio sia per il kernel che per i processi utente, e implementa meccanismi di rimpiazzo delle pagine per garantire un utilizzo efficiente della memoria. La sincronizzazione è assicurata tramite spinlock, che protegge le operazioni sulla coremap da accessi concorrenti.

---

#### Strutture Dati
- **`enum status_t`**: Definisce lo stato di ogni frame nella coremap:
  - `fixed`: Frame riservato al kernel.
  - `free`: Frame disponibile per nuove allocazioni.
  - `dirty`: Frame in uso da un processo utente.
  - `clean`: Frame rubabile (non in uso attivo).
  
- **`struct coremap_entry`**: Ogni elemento rappresenta un frame fisico e include:
  - `as`: Puntatore allo spazio degli indirizzi associato (se presente).
  - `status`: Stato attuale del frame (basato su `status_t`).
  - `vaddr`: Indirizzo virtuale associato a questa pagina fisica.
  - `alloc_size`: Dimensione del blocco contiguo (utile per allocazioni multiple).

---

#### Funzioni

**Inizializzazione**
- `coremap_init()`: Configura la coremap durante l'avvio del sistema, calcolando i frame disponibili e classificandoli come riservati, liberi o kernel.
- `coremap_shutdown()`: Libera risorse e stampa eventuali statistiche diagnostiche sulla gestione della memoria.

**Gestione della Memoria**
- `alloc_kpages(npages)`: Alloca un blocco contiguo di **npages** per il kernel e restituisce l'indirizzo fisico iniziale.
- `free_kpages(addr)`: Libera un blocco di pagine allocate dal kernel, aggiornandone lo stato nella coremap.
- `page_alloc(vaddr)`: Assegna una pagina fisica a un indirizzo virtuale utente, aggiornando la struttura `coremap_entry`.
- `page_free(paddr)`: Rilascia una pagina fisica precedentemente allocata.

**Strategia di Riampiazzo**
- **Round Robin**: Utilizzato per selezionare una vittima da rimpiazzare quando non ci sono frame liberi.
- **Swap-out**: Scrive una pagina fisica su disco (file di swap) per liberare spazio in memoria principale, facilitando il rimpiazzo.

---

### pt.c

#### Panoramica

Il file `pt.c` implementa un sistema per la gestione delle **page tables** (tabelle delle pagine) in un sistema di memoria virtuale. In particolare, il codice si occupa della gestione di una struttura a due livelli: una **outer table** (directory di pagine) e delle **inner tables** (tabelle interne) per la traduzione degli indirizzi virtuali in indirizzi fisici.

#### Strutture Dati

```c
struct pt_directory {
    unsigned int size;
    struct pt_outer_entry *pages;
};
```

- **`pt_directory`**: La struttura `pt_directory` rappresenta la **outer table**, ovvero la directory di pagine. Contiene un array di **entries**, ognuna delle quali può puntare a una **inner table** (tabella interna) o essere vuota (non valida). Ogni entry ha anche un campo che ne segnala la validità.

```c
struct pt_outer_entry {
    unsigned int valid;
    unsigned int size;
    struct pt_inner_entry *pages;
};
```

- **`pt_outer_entry`**: Ogni entry della `pt_directory` è rappresentata da una struttura di tipo `pt_outer_entry`. Ogni entry può contenere una **inner table** (come array di `pt_inner_entry`) e un flag che indica se la entry è valida. Se la entry è valida, può mappare un indirizzo virtuale a un insieme di indirizzi fisici.

```c
struct pt_inner_entry {
    unsigned int valid;
    paddr_t pfn;
    off_t swap_offset;
};
```

- **`pt_inner_entry`**: Le entry della **inner table** (una struttura di tipo `pt_inner_entry`) rappresentano una singola pagina virtuale e contengono informazioni su se la pagina è valida, il **Page Frame Number** (PFN) e lo stato di swap.

#### Funzioni

**Funzioni di utilità per l'estrazione di indici e offset dall'indirizzo virtuale**

- **`get_outer_index(vaddr_t va)`**: estrae l'indice della **outer table** dato un indirizzo virtuale (`va`). Vengono utilizzati i bit più significativi dell'indirizzo per calcolare l'indice nella outer table.

- **`get_inner_index(vaddr_t va)`**: Analogamente, `get_inner_index` estrae l'indice della **inner table** dato un indirizzo virtuale. Vengono utilizzati i bit corrispondenti all'indice della inner table.

- **`get_page_offset(vaddr_t va)`**: estrae l'**offset** di una pagina dato un indirizzo virtuale. L'offset corrisponde ai bit meno significativi dell'indirizzo virtuale.

**Gestione della struttura della page table**

- **`pt_create(void)`**: crea una nuova **outer table**. Alloca memoria per la struttura `pt_directory` e per l'array di entries. Ogni entry viene inizializzata come non valida.

- **`pt_destroy_inner(struct pt_outer_entry pt_inner)`**: libera la memoria associata a una **inner table**. Se una pagina è valida e non swappata, libera il frame fisico associato. Alla fine, libera la memoria dell'array di entries della inner table.

- **`pt_destroy(struct pt_directory* pt)`**: libera tutta la memoria associata alla **directory di pagine** (outer table), comprese le inner tables. Per ogni entry valida, chiama `pt_destroy_inner` per liberare la memoria della inner table associata:

- **`pt_define_inner(struct pt_directory* pt, vaddr_t va)`**: crea una **inner table** per una specifica entry della outer table. Prima verifica che l'entry non sia già valida, poi alloca memoria per la inner table e la inizializza come non valida.

- **`pt_get_pa(struct pt_directory* pt, vaddr_t va)`**: recupera l'indirizzo fisico associato a un indirizzo virtuale. Prima calcola gli indici per la outer e inner table e poi verifica se la pagina è valida, restituendo il **Page Frame Number (PFN)**.
  
- **`pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa)`**: imposta una mappatura tra un indirizzo virtuale e un indirizzo fisico. Se la inner table non esiste, viene creata.

- **`pt_get_offset(struct pt_directory* pt, vaddr_t va)`**: restituisce lo stato di una pagina, cioè se la pagina è in memoria, swappata o non valida. Se la pagina è valida, restituisce l'offset associato, altrimenti restituisce `-1`.

- **`pt_set_offset(struct pt_directory* pt, vaddr_t va, off_t offset)`**: aggiorna lo stato di una pagina, marcandola come swappata o in memoria, a seconda del valore dell'offset fornito. Se la inner table non esiste, viene creata.

#### Costanti e Macro
Il file utilizza alcune costanti per la gestione degli indici e degli offset:
- **`P_OUT_MASK`**, **`P_IN_MASK`**, **`D_MASK`**: maschere per estrarre indici e offset.
- **`PFN_NOT_USED`**: valore che indica un frame fisico non utilizzato.
- **`SIZE_PT_OUTER`**, **`SIZE_PT_INNER`**: dimensioni della outer e inner table.

### segments.c
### statistics.c

#### Panoramica

Il file `statistics.c` implementa un sistema di gestione delle statistiche per monitorare e tracciare vari eventi nel sistema di gestione della memoria virtuale. Utilizzando contatori e spinlock, il sistema raccoglie informazioni sulle **TLB faults**, **page faults**, **operazioni di swap** e altri eventi rilevanti. Questi dati possono essere utilizzati per analisi e ottimizzazione delle performance del sistema.

#### Struttura e funzionalità

Il sistema di statistiche è costituito da un insieme di contatori e funzioni che consentono di inizializzare, incrementare, e visualizzare le statistiche. Le statistiche vengono raccolte in modo sicuro grazie all'uso di un **spinlock** per sincronizzare l'accesso concorrente ai contatori.

#### Contatori

I contatori gestiti da questo sistema sono memorizzati nell'array `counters`, dove ogni indice corrisponde a una statistica specifica. L'array è definito come:

```c
static unsigned int counters[N_STATS];
```

Ogni contatore è associato a una statistica e viene identificato dal proprio indice nell'array. Le statistiche disponibili sono:

- **TLB Faults**
- **TLB Faults with Free**
- **TLB Faults with Replace**
- **TLB Invalidations**
- **TLB Reloads**
- **Page Faults (Zeroed)**
- **Page Faults (Disk)**
- **Page Faults from ELF**
- **Page Faults from Swapfile**
- **Swapfile Writes**

#### Variabili globali

- **statistics_spinlock**: Un spinlock utilizzato per garantire che l'accesso ai contatori sia sicuro in un ambiente concorrente.
- **statistics_names[]**: Un array che contiene i nomi delle statistiche, in ordine corrispondente agli indici dei contatori nell'array `counters`.
- **is_active**: Un flag che indica se il sistema di statistiche è attivo o meno.

#### Funzioni

- **`init_statistics(void)`**: Inizializza il sistema di statistiche, azzerando tutti i contatori e attivando il sistema. La funzione acquisisce il **spinlock** per garantire la sincronizzazione durante l'inizializzazione.

- **`increment_statistics(unsigned int stat)`**: Incrementa il contatore specificato dall'indice `stat`. La funzione verifica che il sistema sia attivo e che l'indice del contatore sia valido prima di procedere. La funzione è protetta da un **spinlock** per evitare accessi concorrenti ai contatori.

- **`print_all_statistics(void)`**: Stampa tutte le statistiche attualmente registrate. La funzione calcola somme parziali e controlla la consistenza dei dati, emettendo avvisi in caso di discrepanze tra i contatori. Se il sistema di statistiche non è attivo, la funzione non esegue alcuna stampa.

#### Controlli di consistenza

Quando le statistiche vengono stampate, vengono effettuati dei controlli di consistenza tra le varie statistiche, per garantire che i totali e le somme parziali siano coerenti. In particolare, vengono effettuati i seguenti controlli:

- **TLB Faults** devono essere uguali alla somma di **TLB Faults with Free** e **TLB Faults with Replace**.
- **TLB Faults** devono essere uguali alla somma di **TLB Reloads**, **Page Faults (Zeroed)** e **Page Faults (Disk)**.
- **Page Faults (Disk)** devono essere uguali alla somma di **Page Faults from ELF** e **Page Faults from Swapfile**.

Se uno di questi controlli fallisce, viene emesso un avviso tramite `kprintf`.

### swapfile.c

### vm_tlb.c

#### Panoramica
Il modulo **vm_tlb.c** gestisce le operazioni sulla TLB (Translation Lookaside Buffer), estendendo le funzionalità di base ( presenti in **mips/tlb.c** ). La funzione principale di questo file è la gestione delle voci nel TLB relative agli indirizzi virtuali. Quando un indirizzo virtuale non è più necessario nel TLB, viene rimosso per ottimizzare l'uso della cache. La rimozione della voce avviene con la funzione `tlb_remove_by_va()`, che cerca e invalida una voce del TLB corrispondente all'indirizzo virtuale fornito.

---

#### Strutture Dati

---

#### Funzioni

- **`tlb_remove_by_va(vaddr_t va)`**: 
  Rimuove una voce dal TLB associata a un indirizzo virtuale dato (va).
  1. Ottiene lo spazio degli indirizzi del processo corrente con `proc_getas()`.
  2. Disabilita le interruzioni usando `splhigh()` per evitare condizioni di gara.
  3. Cerca la voce nel TLB corrispondente all'indirizzo virtuale con `tlb_probe()`.
  4. Se la voce è trovata, viene invalidata usando `tlb_write()`.
  5. Ripristina lo stato delle interruzioni con `splx()`.

  Se la voce non è presente nel TLB, la funzione non fa nulla. La funzione restituisce `0` per indicare che l'operazione è stata completata con successo.

---


### vmc1.c

#### Panoramica
Il modulo **vmc1.c** gestisce il sottosistema di memoria virtuale. Fornisce strumenti per l'inizializzazione, l'allocazione della memoria virtuale, e la gestione dei page fault. Utilizza il file di swap per mantenere uno spazio virtuale esteso e una strategia Round-Robin per la gestione della Translation Lookaside Buffer (TLB), assicurando semplicità ed efficienza.

---

#### Strutture Dati
- **`unsigned int current_victim`**: Variabile globale statica che indica il prossimo indice TLB da sovrascrivere secondo la strategia Round-Robin.
- **costanti**:
  - `VMC1_STACKPAGES`: Numero di pagine riservate per lo stack.

---

#### Funzioni

**Inizializzazione e Terminazione**
- `vm_bootstrap()`: Inizializza il sottosistema di memoria virtuale. Configura la coremap, resetta il contatore TLB, e avvia il sistema di statistiche.
- `vm_shutdown()`: Libera risorse come il file di swap e la coremap. Stampa le statistiche raccolte durante l'esecuzione.

**Gestione TLB**
- `tlb_get_rr_victim()`: Implementa la selezione Round-Robin per determinare quale entry TLB sovrascrivere. Restituisce l'indice dello slot selezionato.
- `vm_tlbshootdown(const struct tlbshootdown *ts)`: Non implementata; genererà un errore in caso di chiamata.

**Gestione dei Page Fault**
- `vm_fault(int fault_type, vaddr_t fault_addr)`: Risolve page fault gestendo indirizzi mancanti nella TLB o nella memoria fisica:
  - Recupera l'indirizzo fisico dalla page table.
  - Alloca una nuova pagina se necessario, azzerandola per lo stack o caricandola dal file di swap.
  - Aggiorna la TLB con la nuova mappatura.
  - Supporta i tipi di fault `VM_FAULT_READ`, `VM_FAULT_WRITE`, e `VM_FAULT_READONLY`.

**Funzioni di Supporto**
- `vm_can_sleep()`: Verifica che il sistema sia in uno stato sicuro per entrare in modalità sleep, assicurandosi che non ci siano spinlock attivi o interruzioni in corso.

---

## Funzionalità implementate

### Tlb Management
### Read-Only Text Segment 
### On-Demand Page Loading
### Page Replacement 
### Instrumentation ( Statistiche )

## Considerazioni finali
