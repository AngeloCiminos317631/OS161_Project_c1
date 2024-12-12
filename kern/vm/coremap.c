#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#include <coremap.h>
#include <vmc1.h>
#include <swapfile.h>
#include <vm_tlb.h>

// Modulo Coremap per la gestione e il tracking della memoria fisica
static struct coremap_entry *coremap = NULL; // Puntatore alla coremap
static int nRamFrames = 0;                   // Numero di entry nella memoria fisica, aggiornato a runtime dalla funzione ram_getsize()
static int coremapActive = 0;                // Flag per tenere traccia dell'attivazione della coremap
static unsigned int current_victim;          // Victim scelta quando la corememory e' piena

// Lock per la gestione della concorrenza nella coremap
static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;   
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

// Funzioni di utilità e helper per la gestione della coremap
static int isCoremapActive(void);
static paddr_t getfreeppages(unsigned long npages);
static int freeppages(paddr_t addr, unsigned long npages);
static paddr_t getppages(unsigned long npages);
static paddr_t getppage_user(vaddr_t va, struct addrspace *as);

// Sezione 1: Funzioni di inizializzazione e gestione della coremap

// Verifica se la coremap è attiva, utilizzando un lock per evitare problemi di concorrenza
static int isCoremapActive() {
    int active;
    spinlock_acquire(&freemem_lock);
    active = coremapActive;
    spinlock_release(&freemem_lock);
    return active;
}

// Inizializza la coremap con informazioni di base su ogni frame di memoria fisica e la attiva
void coremap_init() {
    int i;
    int coremap_size = 0;
    nRamFrames = ((int)ram_getsize()) / PAGE_SIZE;  // Calcola il numero di frame in RAM
    KASSERT(nRamFrames > 0);

    coremap_size = sizeof(struct coremap_entry) * nRamFrames;
    coremap = kmalloc(coremap_size);  // Alloca la memoria per la coremap, nel kernel space
    KASSERT(coremap != NULL);

    // Inizializza ciascun entry della coremap con valori di default
    for(i = 0; i < nRamFrames; i++) {
        coremap[i].status = clean;
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
        coremap[i].vaddr = 0;
    }

    // Attiva la coremap
    spinlock_acquire(&freemem_lock);
    coremapActive = 1;
    spinlock_release(&freemem_lock);
}

// Rilascia la memoria utilizzata dalla coremap e la disattiva
void coremap_shutdown() {
    int i;
    spinlock_acquire(&freemem_lock);
    coremapActive = 0;  // Disattiva la coremap
    for(i = 0; i < nRamFrames; i++) {
        page_free(i * PAGE_SIZE);  // Libera ogni pagina
    }
    kfree(coremap);  // Libera la memoria della coremap dal kernel space
    spinlock_release(&freemem_lock);
}

void coremap_turn_off() {
    spinlock_acquire(&freemem_lock);
    coremapActive = 0;
    spinlock_release(&freemem_lock);
}

void coremap_turn_on() {
    spinlock_acquire(&freemem_lock);
    coremapActive = 1;
    spinlock_release(&freemem_lock);
}

// Sezione 2: Funzioni di allocazione per il kernel e utente

// Alloca npages pagine contigue per il kernel e restituisce l'indirizzo virtuale
vaddr_t alloc_kpages(unsigned long npages) {
    paddr_t pa;
    vm_can_sleep();
    pa = getppages(npages);
    if (pa == 0) {
        return 0;
    }
    return PADDR_TO_KVADDR(pa);
}

// Alloca una pagina di memoria per l'indirizzo virtuale dato (per user )
paddr_t page_alloc(vaddr_t vaddr/*, int state*/) {
    paddr_t pa;
    struct addrspace *as_cur;

    if (!isCoremapActive()) return 0;
    vm_can_sleep();

    as_cur = proc_getas();
    KASSERT(as_cur != NULL)

    pa = getppage_user(vaddr, as_cur/*, state*/);  // Richiede una pagina fisica
    return pa;
}

// Funzione helper per assegnare pagina utente ad un frame della coremap
static paddr_t getppage_user(vaddr_t va, struct addrspace *as/*, int state*/) {
    volatile int found = 0, pos;
    int i;
    unsigned int victim;
    paddr_t pa;
    vaddr_t victim_va; // Indirizzo virtuale della vittima
    paddr_t victim_pa; // Indirizzo fisico della vittima
    int result_swap_out; // Risultato della funzione swap_out
    
    // Per proteggere l'accesso alla coremap
    spinlock_acquire(&freemem_lock);
    // Cerca una pagina precedentemente liberata, usando una ricerca lineare
    for(i = 0; i < nRamFrames && !found; i++) {
        if(coremap[i].status == free) {
            found = 1;
            break;
        }
    }
    // Rilascio del lock precedentemente acquisito
    spinlock_release(&freemem_lock);

    if(found) {
        pos = i;
        pa = i * PAGE_SIZE;
    }
    else {
        // Se non ci sono pagine libere, chiede una pagina 'clean' alla RAM
        spinlock_acquire(&stealmem_lock);
        pa = ram_stealmem(1);
        spinlock_release(&stealmem_lock);

        // Se non c'è memoria fisica disponibile dobbiamo scegliere una victim tramite Round Robin
        if(pa == 0) {
            victim = get_victim_coremap(1);
            pos = victim;
            // Qui dovremmo chiamare la funzione swap_out per scrivere la pagina vittima su swap
            victim_pa = pos * PAGE_SIZE; // Calcoliamo l'indirizzo fisico della vittima
            victim_va = coremap[pos].vaddr; // Recuperiamo l'indirizzo virtuale della vittima
            result_swap_out = swap_out(victim_pa, victim_va); // Swap-out della pagina
            // Aggiorniamo lo stato della pagina nel page table per segnare la vittima come "swapped out"
            pt_set_offset(as->pt, victim_va, result_swap_out);
            pt_set_pa(as->pt, victim_va, 0);
            // Verifica e aggiorna le voci del TLB associate a un indirizzo fisico vittima, con il nuovo VA. ( CONTROLLARE pa mi sembra errato )
            //KASSERT(state == state); DA VALUTARE INSIEME A state
            pa = victim_pa  // Impostiamo l'indirizzo fisico della vittima come la pagina da restituire
            kprintf("SWAPPING line 276: (victim_pa: 0x%x victim_va: 0x%x)\n", victim_pa, victim_va); // Print per debug
            pos = victim_pa / PAGE_SIZE; // Impostiamo la posizione della vittima
        } else {  
            pos = pa / PAGE_SIZE;
        }
    }

    // Per proteggere l'accesso alla coremap
    spinlock_acquire(&freemem_lock);
    coremap[pos].as = as;
    coremap[pos].status = dirty;
    coremap[pos].vaddr = va;
    coremap[pos].alloc_size = 1;
    // Rilascio del lock precedentemente acquisito
    spinlock_release(&freemem_lock);

    return pa;
}

// Ottiene npages pagine fisiche libere e le imposta come "fixed" nella coremap ( per il kernel )
static paddr_t getppages(unsigned long npages) {
    unsigned long i, pos;
    paddr_t addr;
    unsigned int victim;
    volatile paddr_t victim_pa;
    vaddr_t victim_va;
    int result_swap_out;
    struct addrspace* as;
    int result;
    addr = getfreeppages(npages);
    // Viene ritornato 0 se non sono disponibili pagine liberate in precedenza, quindi si "rubano" dalla RAM
    if (addr == 0) {
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
        // dopo aver rubato memoria dobbiamo avere necessariamente l'indirizzo fisico diverso da 0, se no viene generato un crash del kernel
        KASSERT(addr != 0);
    }
    if(addr == 0) {
        // Se addr è ancora 0, scegliamo una vittima da svuotare tramite Round Robin
        victim = get_victim_coremap(npages);
        
        // Otteniamo l'address space del processo
        as = proc_getas();
        if (as == NULL) return 0;  // Se è un thread del kernel, lasciamo invariato l'address space

        // Eseguiamo lo swap-out delle pagine per liberare spazio
        for(i = 0; i < npages; i++) {
            pos = victim + i;  
            victim_pa = pos * PAGE_SIZE;  // Indirizzo fisico della vittima
            victim_va = coremap[pos].vaddr;  
            result_swap_out = swap_out(victim_pa, victim_va);  // Swap-out della pagina

            // Aggiorniamo la tabella delle pagine
            pt_set_offset(as->pt, victim_va, result_swap_out);
            pt_set_pa(as->pt, victim_va, 0);

            // Rimuoviamo l'entrata dalla TLB
            result = tlb_remove_by_va(victim_va);
            KASSERT(result != -1);  // Verifica della rimozione dalla TLB
        }
        addr = victim * PAGE_SIZE;  // Impostiamo addr all'indirizzo della vittima
    }
    if (addr != 0 && isCoremapActive()) {
        spinlock_acquire(&freemem_lock);
        //Vengono aggiornate le ritornate da getfreeppages nella coremap, cambiando lo stato da "clean" a "fixed" , cioè assegnate al kernel
        coremap[addr / PAGE_SIZE].alloc_size = npages;
        coremap[addr / PAGE_SIZE].status = fixed;

        for(i = 1; i < npages; i++) {
            KASSERT(coremap[(addr / PAGE_SIZE) + i].alloc_size == 0);
            coremap[(addr / PAGE_SIZE) + i].status = fixed;
        }
        spinlock_release(&freemem_lock);
    } 
    return addr;
}

// Cerca npages pagine contigue libere nella coremap
static paddr_t getfreeppages(unsigned long npages) {
    paddr_t addr;    
    long i, first, found;

    if (!isCoremapActive()) return 0; 
    spinlock_acquire(&freemem_lock);
    for (i = 0, first = found = -1; i < nRamFrames; i++) {
        if (coremap[i].status == free) {
            if (i == 0 || coremap[i-1].status != free) 
                first = i;
            if (i - first + 1 >= (long) npages) {
                found = first;
                break;
            }
        }
    }
        
    if (found >= 0) {
        for (i = found; i < found + (long) npages; i++) {
            coremap[i].status = fixed;
            KASSERT(coremap[i].alloc_size == 0);
        }
        coremap[found].alloc_size = npages;
        addr = (paddr_t) found * PAGE_SIZE;
    } else {
        addr = 0;
    }

    spinlock_release(&freemem_lock);
    return addr;
}

// Sezione 3: Funzioni di rilascio per il kernel e utente

// Libera una pagina fisica specificata dall'indirizzo fisico passato come argomento ( per utente )
void page_free(paddr_t addr) {
    int pos;
    pos = addr / PAGE_SIZE;

    KASSERT(coremap[pos].status != fixed);
    //KASSERT(coremap[pos].status != clean);

    // Per proteggere l'accesso alla coremap
    spinlock_acquire(&freemem_lock);
    coremap[pos].status = free;
    coremap[pos].as = NULL;
    coremap[pos].alloc_size = 0;
    coremap[pos].vaddr = 0;
    // Rilascio del lock precedentemente acquisito
    spinlock_release(&freemem_lock);
}

// Libera le pagine kernel contigue specificate dall'indirizzo virtuale iniziale ( per kernel )
void free_kpages(vaddr_t addr) {
    if (isCoremapActive()) {
        paddr_t paddr = addr - MIPS_KSEG0;
        long first = paddr / PAGE_SIZE;    
        KASSERT(nRamFrames > first);
        freeppages(paddr, coremap[first].alloc_size);    
    }
}

// Libera npages pagine fisiche contigue a partire dall'indirizzo fisico specificato ( per kernel )
static int freeppages(paddr_t addr, unsigned long npages) {
    long i, first;    

    if (!isCoremapActive()) return 0; 
    first = addr / PAGE_SIZE;
    KASSERT(nRamFrames > first);

    spinlock_acquire(&freemem_lock);
    for (i = first; i < first + (long) npages; i++) {
        coremap[i].status = free;
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
    }
    spinlock_release(&freemem_lock);

    return 1;
}


/**
 * Seleziona una sequenza contigua di frame di memoria utilizzabili come vittime 
 * per il TLB, escludendo quelli con stato "fixed" o "clean". 
 * Utilizza un algoritmo di selezione Round Robin per garantire una distribuzione 
 * equa delle vittime tra tutti i frame disponibili.
 */
static int get_victim_coremap(int size) {
    int victim = -1;      // Indica il frame corrente selezionato come potenziale vittima
    int len = 0;          // Contatore per verificare la contiguità di "size" frame
    KASSERT(size != 0);   // Verifica che la dimensione richiesta non sia zero

    while (len < size) {  // Continua finché non si trovano "size" frame contigui non fissi
        // Controlla se ci si avvicina al limite dell'array della coremap
        // e resetta l'indice al primo frame (Round Robin)
        if (current_victim + (size - len) >= (unsigned int)nRamFrames) {
            current_victim = 0;
        }

        // Registra l'indice corrente come potenziale vittima
        victim = current_victim;
        // Incrementa l'indice per il prossimo frame (Round Robin)
        current_victim = (current_victim + 1) % nRamFrames;

        // Controlla lo stato del frame corrente
        if (coremap[victim].status != fixed && coremap[victim].status != clean) {
            len += 1; // Aggiungi al contatore se il frame è idoneo
        } else {
            len = 0; // Reset del contatore se un frame non idoneo viene trovato
        }
    }

    // Restituisce l'indice del primo frame contiguo trovato
    return victim - (len - 1);
}
