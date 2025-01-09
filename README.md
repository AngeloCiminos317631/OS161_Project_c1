# Progetto os161-c1

## Introduzione
Il progetto prevede la sostituzione di dumbvm con un gestore della memoria virtuale basato su tabelle delle pagine per processo, includendo caricamento delle pagine su richiesta, sostituzione delle pagine e gestione avanzata del TLB con politiche di rimpiazzo. L'obiettivo è supportare spazi di indirizzamento più grandi della memoria fisica, implementando politiche di selezione delle vittime e gestione dei frame liberi.

## Autori
* s292671 Donato Agostino Modugno
* s308455 Simone Colagiovanni
* s317631 Angelo Cimino


## Divisione del lavoro
* **Donato Agostino Modugno**
	* Implementazione del modulo per la gestione dello Swapfile `swapfile.c` e del modulo per la gestione dei Segments `segments.c`
* **Simone Colagiovanni**
	* Implementazione del modulo per la gestione dell' Address Space `addrspace.c` , del modulo per la gestione della Page Table `pt.c` e della parte relativa alle statistiche inerenti al sistema di gestione della memoria virtuale `statistics.c`
* **Angelo Cimino**
	* Implementazione del modulo per la gestione della Coremap `coremap.c` , del modulo per la gestione della Virtual Memory `vm_c1.c` e parte della TLB `vm_tlb.c` . Gestione delle opzioni e delle configurazioni per il progetto `C1_PAG`.


## Scelte Implemetative
Per "rilassare" i limiti di dumbvm ci siamo serviti di alcuni file. Nella cartella `vm` troviamo: addrspace.c, coremap.c, kmalloc.c, pt.c, segments.c, vm_tlb.c e vmc1.c.
Analizzeremo ogni file più nel dettaglio nelle sezioni seguenti.

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

- **`as_deactivate(void)`**: Simile a `as_activate`, la funzione `as_deactivate` disattiva l'address space corrente invalidando tutte le voci della TLB. Questo garantisce che le vecchie mappature delle pagine virtuali non siano più valide.

- **`as_define_region(struct addrspace *as, uint32_t type, uint32_t offset ,vaddr_t vaddr, size_t memsize,
		 uint32_t filesize, int readable, int writeable, int executable, int seg_n, struct vnode *v)`**: definisce una regione di memoria in un address space. Essa stabilisce i permessi di accesso (lettura, scrittura, esecuzione) per il segmento specificato e ne determina le caratteristiche, come il tipo, l'offset, l'indirizzo virtuale e la dimensione in memoria. I segmenti possibili sono `code` e `data`.

- **`as_prepare_load e as_complete_load`**: non contengono implementazioni specifiche. Sono destinate alla preparazione e al completamento del caricamento di un programma nell'address space. Potrebbero essere implementate in futuro per gestire operazioni specifiche.

- **`as_define_stack(struct addrspace *as, vaddr_t *stackptr)`**: definisce lo stack di un address space. Imposta il puntatore allo stack all'indirizzo iniziale di `USERSTACK`.

- **`as_get_segment(struct addrspace *as, vaddr_t va)`**: restituisce il segmento (code, data o stack) associato a un dato indirizzo virtuale (`va`). Questo permette di identificare a quale parte dell'address space appartiene una determinata pagina.

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

#### Strutture Dati

```c
struct segment {
    uint32_t		p_type;
	uint32_t		p_offset;
	uint32_t		p_vaddr;
	uint32_t		p_filesz;
	uint32_t		p_memsz;
	uint32_t		p_permission;
	struct vnode	*vnode;
};
```

- **`struct segment`**: struttura che definisce un segmento
    - `p_type`: tipo di segmento
    - `p_offset`: posizione dei dati all'interno del file in cui inizia il segmento
    - `p_vaddr`: indirizzo virtuale (base address)
    - `p_filesz`: dimensione dei dati all'interno del file
    - `p_memsz`: dimensione dei dato da caricare in memoria
    - `p_permission`: descrive quali operazioni possono essere eseguite sulle pagine del segmento

I possibili valori di `p_permission` (definiti in `elf.h`) sono:
- `PF_R` (0x4): il segmento è leggibile
- `PF_W` (0x2): il segmento è scrivibile
- `PF_X` (0x1): il segmento è eseguibile
- `PF_S` (0x8): il segmento è stack

#### Funzioni

- `seg_create()`, `seg_copy()` e `seg_destroy()`: metodi principali di gestione della struttura segment
- `seg_define()` e `seg_define_stack()`: vengono invocati nel momento di creazione del processo. La differenza è che dati e codice vengono caricati da file mentre lo stack no
- `int seg_load_page(struct segment* seg, vaddr_t va, paddr_t pa)`: riceve l'indirizzo virtuale che ha causato il page fault e l'indirizzo fisico iniziale del frame di memoria che è già stato allocato per ospitare la pagina, quindi calcola quante pagine sono necessarie, l'indice della pagina all'interno del segmento e l'offset da aggiungere per la fault page. Questi risultati verranno usati per riempire la struttura uio per la gestione dei fault.

### statistics.c

#### Panoramica

Il file `statistics.c` implementa un sistema di gestione delle statistiche per monitorare e tracciare vari eventi nel sistema di gestione della memoria virtuale. Utilizzando contatori e spinlock, il sistema raccoglie informazioni sulle **TLB faults**, **page faults**, **operazioni di swap** e altri eventi rilevanti. Questi dati possono essere utilizzati per analisi e ottimizzazione delle performance del sistema.

#### Struttura e funzionalità

Il sistema di statistiche è costituito da un insieme di contatori e funzioni che consentono di inizializzare, incrementare, e visualizzare le statistiche. Le statistiche vengono raccolte in modo sicuro grazie all'uso di un **spinlock** per sincronizzare l'accesso concorrente ai contatori.

### swapfile.c

#### Panoramica

Viene creato uno swapfile della dimensione (arbitraria) di 9 MB suddivisa in un numero di pagine pari a `NUM_PAGES = FILE_SIZE / PAGE_SIZE`

#### Strutture Dati

```c
struct swap_page {
    paddr_t ppadd;
    vaddr_t pvadd;
    off_t swap_offset;
    int free;
};
```

- **`struct swap_page`**: struttura che definisce una pagina nello swapfile
    - `ppadd`: indirizzo fisico
    - `pvadd`: indirizzo virtuale
    - `swap_offset`: offset della pagina
    - `free`: 1 se libero, 0 se occupato

Viene creata una lista di struct di lunghezza `NUM_PAGES`

#### Funzioni

- `swapfile_init(void)`: tutti i parametri dello swapfile vengono settati a 0, ad eccezione di `free` che viene settato a 1
- `swap_out(paddr_t ppaddr, vaddr_t pvaddr)`: viene invocata nel momento in cui si vuole rimuovere una pagina (victim page) dalla memoria fisica e copiarla nello swapfile
- `swap_in(paddr_t ppadd, off_t offset)`: viene invocata nel momento in cui si vuole inserire una pagina dallo swapfile nella memoria fisica
- `swap_shutdown(void)`: resetta l'intera lista di struct
- `getIn()` e `getOut()`: restituiscono rispettivamente il numero di pagine swappate in ingresso e in uscita

### vm_tlb.c

#### Panoramica
Il modulo **vm_tlb.c** gestisce le operazioni sulla TLB (Translation Lookaside Buffer), estendendo le funzionalità di base ( presenti in **mips/tlb.c** ). La funzione principale di questo file è la gestione delle voci nel TLB relative agli indirizzi virtuali. Quando un indirizzo virtuale non è più necessario nel TLB, viene rimosso per ottimizzare l'uso della cache. La rimozione della voce avviene con la funzione `tlb_remove_by_va()`, che cerca e invalida una voce del TLB corrispondente all'indirizzo virtuale fornito.

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
Il modulo di gestione della TLB implementa il caricamento e la sostituzione delle voci TLB al verificarsi di un TLB miss. Ogni nuova voce viene aggiunta sfruttando lo spazio libero o sostituendo una voce esistente tramite una politica di sostituzione Round-Robin, che ciclicamente seleziona la prossima voce da evictare. Inoltre, la funzione as_activate garantisce l'invalidamento del TLB durante i context switch, assicurando che le voci siano sempre relative al processo corrente.

### Read-Only Text Segment

Per evitare il crash del kernel, ogni processo verrà terminato ad ogni tentativo di modificarne il text segment. In vmc1.c controlliamo i permessi del segmento: se è read-only con dirty bit impostato su 0, nessun processo può scrivere su una pagina con un flag TLBLO_DIRTY impostato su 0.

```c
if (seg->p_permission == (PF_R | PF_W) || seg->p_permission == PF_S || seg->p_permission == PF_W)
{
    elo = elo | TLBLO_DIRTY;
}
```

### On-Demand Page Loading
Il sistema implementa un caricamento delle pagine "on demand", allocando frame fisici e caricando le pagine solo al primo accesso. Durante un'eccezione di TLB miss, il kernel verifica se la pagina è già in memoria; se non lo è, utilizza il modulo Coremap per la gestione dei frame fisici per allocare uno spazio, recupera i dati ELF e aggiorna la mappatura dell'address space. Infine, inserisce la nuova mappatura nella TLB utilizzando una politica di sostituzione Round-Robin, se necessario. Questo approccio riduce il consumo di memoria fisica caricando solo le pagine effettivamente utilizzate.

### Page Replacement 
Il sistema utilizza un algoritmo di **page replacement Round Robin**, semplice e funzionale, per selezionare ciclicamente le pagine da sostituire. Le pagine in stato `dirty` vengono scritte su un file **SWAPFILE**, limitato a **9 MB**. Se lo spazio richiesto supera questo limite, il kernel invoca `panic("Out of swap space")`. La dimensione massima è configurabile a compile time.

La **coremap** traccia lo stato dei frame (`free`, `clean`, `dirty`, `fixed`), gestendo la sostituzione solo per i frame non riservati al kernel (`fixed`). In assenza di frame liberi, la pagina vittima viene salvata su disco con `swap_out`, la TLB aggiornata, e la tabella delle pagine modificata per indicare lo stato "swapped out".

Per la gestione concorrente sono usati spinlock, garantendo integrità durante le operazioni critiche.

### Instrumentation ( Statistiche )

Abbiamo eseguito i seguenti test per verificare la memoria virtuale:

- **palin**
- **huge**
- **ctest**
- **sort**
- **matmul**

### Risultati:

| Test        | TLB Faults | TLB Faults con Free | TLB Faults con Replace | TLB Invalidations | TLB Reloads | Page Faults (zero filled) | Page Faults (disk) | Page Faults from ELF | Page Faults from Swapfile | Swapfile Writes |
|-------------|------------|----------------------|-------------------------|-------------------|-------------|---------------------------|--------------------|-----------------------|--------------------------|-----------------|
| **palin**   | 13918      | 13918               | 0                       | 8010              | 13913       | 1                         | 4                  | 4                     | 0                        | 0               |
| **huge**    | 7574       | 7574                | 0                       | 7190              | 3908        | 512                       | 3154               | 3                     | 3151                     | 3631            |
| **ctest**   | 249425     | 249425              | 0                       | 251495            | 123658      | 257                       | 125510             | 3                     | 125507                   | 125724          |
| **sort**    | 8483       | 8483                | 0                       | 4501              | 6198        | 289                       | 1996               | 4                     | 1992                     | 2242            |
| **matmul**  | 4824       | 4824                | 0                       | 1586              | 3967        | 380                       | 477                | 3                     | 474                      | 814             |

Il valore 0 per **"TLB Faults with Replace"** è dovuto all'uso di una tabella a più livelli per la gestione della memoria. In questo caso, l'indirizzamento virtuale viene risolto in più fasi, riducendo la necessità di sostituire voci nel TLB. Poiché la struttura a più livelli ottimizza l'accesso alle pagine e riduce la probabilità di esaurire lo spazio del TLB, non si verificano TLB misses che richiedano sostituzioni, portando così a un valore di 0 per **"TLB Faults with Replace"**.
