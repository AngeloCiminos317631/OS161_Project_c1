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

// Modulo Coremap per la gestione e il tracking della memoria fisica
static struct coremap_entry *coremap = NULL; // Puntatore alla coremap
static int nRamFrames = 0;                   // Numero di entry nella memoria fisica, aggiornato a runtime dalla funzione ram_getsize()
static int coremapActive = 0;                // Flag per tenere traccia dell'attivazione della coremap

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

    coremap_size = sizeof(coremap_entry) * nRamFrames;
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
paddr_t page_alloc(vaddr_t vaddr) {
    paddr_t pa;
    struct addrspace *as_cur;

    if (!isCoremapActive()) return 0;
    vm_can_sleep();

    as_cur = proc_getas();
    KASSERT(as_cur != NULL)

    pa = getppage_user(vaddr, as_cur);  // Richiede una pagina fisica
    return pa;
}

// Funzione helper per assegnare pagina utente ad un frame della coremap
static paddr_t getppage_user(vaddr_t va, struct addrspace *as) {
    int found = 0, pos;
    int i;
    paddr_t pa;
    
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

        //Fa avvenire il crash del kernel, quando non c'è più memoria da "rubare" alla RAM
        KASSERT(pa != 0);
        pos = pa / PAGE_SIZE;
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
    paddr_t addr;
    addr = getfreeppages(npages);
    // Viene ritornato 0 se non sono disponibili pagine liberate in precedenza, quindi si "rubano" dalla RAM
    if (addr == 0) {
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
        // dopo aver rubato memoria dobbiamo avere necessariamente l'indirizzo fisico diverso da 0, se no viene generato un crash del kernel
        KASSERT(addr != 0);
    }
    if (addr != 0 && isCoremapActive()) {
        spinlock_acquire(&freemem_lock);
        //Vengono aggiornate le ritornate da getfreeppages nella coremap, cambiando lo stato da "clean" a "fixed" , cioè assegnate al kernel
        coremap[addr / PAGE_SIZE].alloc_size = npages;
        coremap[addr / PAGE_SIZE].status = fixed;

        for(int i = 1; i < npages; i++) {
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
            if (i - first + 1 >= npages) {
                found = first;
                break;
            }
        }
    }
        
    if (found >= 0) {
        for (i = found; i < found + npages; i++) {
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
    KASSERT(coremap[pos].status != clean);

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
    for (i = first; i < first + npages; i++) {
        coremap[i].status = free;
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
    }
    spinlock_release(&freemem_lock);

    return 1;
}
