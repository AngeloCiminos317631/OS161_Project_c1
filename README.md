# Progetto os161-c1

## Coremap ( Bozza ) (Angelo)

Il modulo coremap gestisce e traccia l'uso della memoria fisica nel sistema, fornendo un sistema di allocazione e rilascio di pagine di memoria sia per il kernel sia per i processi utente. La coremap è una struttura dati che mantiene informazioni su ogni frame di memoria fisica, inclusi lo stato (free, fixed, dirty, clean), l'indirizzo virtuale associato, lo spazio di indirizzi (addrspace) del processo, e la dimensione dell'allocazione.

Le principali funzionalità del modulo includono:

- Inizializzazione e spegnimento: Configura e libera la coremap, rendendo la memoria fisica tracciabile.
- Allocazione e rilascio per processi utente: Permette a un processo di richiedere pagine di memoria fisica o di liberarle quando non più necessarie.
- Allocazione e rilascio per il kernel: Consente al kernel di allocare e rilasciare pagine contigue in modo efficiente.

### **Memoria Virtuale** (Cenni teorici e funzioni implementate fin'ora) (Simone)

La **memoria virtuale** è una tecnica che separa lo spazio di indirizzamento usato dai processi (spazio virtuale) dallo spazio di memoria fisica (RAM). Consente ai processi di avere un proprio spazio di indirizzamento indipendente, migliorando:

- **Protezione**: i processi non possono accedere alla memoria di altri.
- **Gestione dinamica**: i processi possono utilizzare più memoria di quella fisica disponibile tramite meccanismi di swap.
- **Efficienza**: l'allocazione della memoria è gestita su pagine, riducendo frammentazioni e aumentando la velocità di accesso.

La traduzione degli indirizzi virtuali in indirizzi fisici è effettuata tramite una **tabella delle pagine** e il **TLB** (Translation Lookaside Buffer), accelerando il processo.

---

### **Funzioni implementate in `vmc1.c`**

#### 1. **`vm_bootstrap()`**

- **Scopo**: Inizializza il sistema di gestione della memoria durante l'avvio.
- **Implementazione**: Chiama `coremap_init()`, che prepara la struttura per gestire la memoria fisica.

#### 2. **`vm_shutdown()`**

- **Scopo**: Effettua il "teardown" della gestione della memoria durante lo spegnimento del sistema.
- **Implementazione**: Libera risorse utilizzate dalla coremap tramite `coremap_shutdown()`.

#### 3. **`vm_can_sleep()`**

- **Scopo**: Verifica se la CPU corrente è in uno stato che consente al thread di dormire (cioè bloccare l'esecuzione in attesa di un evento).
- **Implementazione**:
  - Controlla che non siano attivi spinlock (che richiedono operazioni atomiche).
  - Verifica che il thread non sia in una routine di interrupt.
  - Utilizza `KASSERT` per assicurare che le condizioni siano rispettate.

#### 4. **`vm_fault(int faulttype, vaddr_t faultaddress)`**

- **Scopo**: Gestisce i page fault, che si verificano quando un processo tenta di accedere a una pagina non presente in memoria.
- **Implementazione**:
  - Identifica il tipo di errore (**readonly**, **read**, **write**) e ritorna un codice di errore appropriato.
  - Recupera l'indirizzo fisico associato all'indirizzo virtuale tramite la tabella delle pagine.
  - Se non esiste un frame associato, ne alloca uno nuovo (comportamento ancora da completare nel codice).
  - Aggiorna il TLB con la nuova mappatura (indirizzo virtuale → fisico).
  - Gestisce i limiti della TLB, notificando se questa si satura.

### Page Table (Cenni teorici e funzioni implementate fin'ora) (Simone)

La **page table** è una struttura dati fondamentale per la gestione della memoria virtuale. Essa fornisce una mappatura tra gli **indirizzi virtuali** e gli **indirizzi fisici**, consentendo al sistema operativo di implementare uno spazio di indirizzamento virtuale isolato per ogni processo.

In questa implementazione, la page table utilizza una struttura a **due livelli** per bilanciare efficienza e flessibilità. Le sue componenti principali includono una **outer table** che punta a una serie di **inner tables**, ciascuna delle quali contiene le informazioni sulle pagine mappate.

---

### Strutture principali

- **`struct pt_inner_entry`**: rappresenta un'entry di una inner table, contenente:

  - **`valid`**: indica se la pagina è mappata.
  - **`pfn`**: il physical frame number (indirizzo fisico della pagina).

- **`struct pt_outer_entry`**: rappresenta un'entry della outer table, contenente:

  - **`valid`**: indica se la inner table associata è valida.
  - **`pages`**: puntatore alla inner table.

- **`struct pt_directory`**: rappresenta l'intera page table, contenente:
  - **`size`**: numero di entry nella outer table.
  - **`pages`**: puntatore alla outer table.

---

### Funzioni implementate in `pt.c`

1. **Creazione della Page Table**

   - **`struct pt_directory* pt_create(void)`**  
     Inizializza una nuova page table, allocando memoria per la outer table e impostando tutte le entry come invalide.

2. **Distruzione della Page Table**

   - **`void pt_destroy(struct pt_directory* pt)`**  
     Rilascia la memoria associata alla page table, inclusa la memoria di tutte le inner tables.

3. **Invalidazione del contesto**

   - **`void pt_invalidate_context(struct pt_directory* pt)`**  
     Rimuove tutte le mappature nella page table senza distruggere la struttura. Questa operazione serve a "resettare" o invalidare tutte le entry della page table relative a un determinato processo o thread, in modo che il nuovo contesto (il nuovo processo o thread) possa utilizzare la sua propria mappatura della memoria virtuale senza conflitti.

4. **Definizione di una nuova inner table**

   - **`static void pt_define_inner(struct pt_directory* pt, vaddr_t va)`**  
     Allocca una nuova inner table e la associa all'indirizzo virtuale indicato.

5. **Recupero dell'indirizzo fisico**

   - **`paddr_t pt_get_pa(struct pt_directory* pt, vaddr_t va)`**  
     Cerca nella page table l'indirizzo fisico associato a un dato indirizzo virtuale. Restituisce un valore speciale (`PFN_NOT_USED`) se la pagina non è mappata.

6. **Impostazione di un indirizzo fisico**
   - **`void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa)`**  
     Mappa un indirizzo virtuale a un indirizzo fisico. Se necessario, crea una nuova inner table per gestire la mappatura.

### Address Space (Cenni teorici e funzioni implementate fin'ora) (Simone)

L'**address space** rappresenta lo spazio degli indirizzi virtuali utilizzati da un processo. Ogni processo dispone di un proprio address space che fornisce un'astrazione della memoria, consentendo al processo di accedere a risorse senza interferire con altri processi. Questo spazio è suddiviso in regioni, ciascuna delle quali può essere associata a specifiche aree della memoria (ad esempio, codice, dati o stack) e configurata con permessi di accesso (lettura, scrittura, esecuzione).

Un address space utilizza una **page table** per tradurre gli indirizzi virtuali in indirizzi fisici e implementa meccanismi per allocare, deallocare e gestire queste traduzioni in risposta a eventi come i page fault.

### **Modifiche e Funzioni Implementate**

#### 1. **Gestione delle Regioni (`as_define_region`)**

- La funzione **`as_define_region`** è stata implementata per consentire la definizione di regioni nello spazio degli indirizzi.
- Una regione è caratterizzata da:
  - Un indirizzo di base (virtuale).
  - Una dimensione (in pagine).
  - Permessi di accesso (lettura, scrittura, esecuzione).
- Questa funzione aggiunge le regioni definite al campo `regions` della struttura `addrspace`.

---

#### 2. **Struttura dello Spazio degli Indirizzi**

- La struttura `addrspace` è stata modificata per includere:
  - Un campo per la page table (`pt`).
  - Una lista di regioni (`regions`) con i relativi permessi.
  - Un contatore per il numero di regioni attualmente definite.

---

### **Collegamento tra le Funzioni**

1. **Definizione di una Regione**  
   Quando viene invocata la system call per allocare memoria o caricare un eseguibile, viene chiamata `as_define_region`. Questa funzione configura l'address space con le informazioni necessarie.

2. **Accesso alla Memoria**  
   Durante l'esecuzione, se il processo accede a una pagina non valida (non ancora mappata), il kernel invoca `vm_fault_handler`. Questa funzione si assicura che la pagina richiesta venga allocata e mappata correttamente.

3. **Page Table e TLB**  
   La page table viene aggiornata dinamicamente da `vm_fault_handler`, mentre il TLB accelera l'accesso successivo memorizzando le traduzioni più recenti.

4. **Pulizia e Rilascio delle Risorse**  
   Quando un processo termina, `pt_destroy` e altre funzioni di pulizia vengono invocate per deallocare la memoria e invalidare la page table.
