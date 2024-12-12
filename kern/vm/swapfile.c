#include <types.h>
#include <kern/fcntl.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>
#include <vm.h>
#include <bitmap.h>
#include <swapfile.h>

// Creiamo una lista di entries
static struct swap_page swap_list[NUM_PAGES];

// Solo un processo alla volta può accedere allo swapfile per cui abbiamo bisogno di uno spinlock
static struct spinlock filelock = SPINLOCK_INITIALIZER;

static struct vnode *v = NULL;

static int timesOut = 0; // Contatore per le pagine swappate out
static int timesIn = 0;  // Contatore per le pagine swappate in

void swapfile_init(void) {
    int result,i;

    // Inizializziamo la bitmap
    for(i=0; i<NUM_PAGES; i++)
    {
        swap_list[i].ppadd = 0;
        swap_list[i].pvadd = 0;
        swap_list[i].swap_offset = 0;
        swap_list[i].free = 1;
    }

    // Quando a runtime sono necessari più di 9MB => panic
    kprintf("SWAPFILE INIT\n");
    result = vfs_open((char *)"emu0:/SWAPFILE", O_RDWR | O_CREAT , 0, &v);
    KASSERT(result == 0);
    return;

   
}


int swap_out(paddr_t ppaddr, vaddr_t pvaddr) {
    // Dato l'indirizzo fisico della pagina da swappare
    // restituisce l'offset a cui la salviamo nello swapfile
    int free_index = -1;
    
    struct iovec iov;
    struct uio u;
    int i;
    struct swap_page *entry;
    off_t page_offset;

    spinlock_acquire(&filelock);
    for(i=0; i< NUM_PAGES; i++) {
        entry = &swap_list[i];
        if(entry->free) {
            free_index = i;
            break;
        }
    }
    spinlock_release(&filelock);
    if(free_index == -1) {
        kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", timesOut, timesIn);
        panic("swapfile.c: Out of swap space \n");
        return -1;
    }

    page_offset = free_index * PAGE_SIZE;
    KASSERT(page_offset < FILE_SIZE);
    KASSERT((ppaddr & PAGE_FRAME) == ppaddr); // Verifica che l'indirizzo fisico sia allineato a pagina

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppaddr), PAGE_SIZE, page_offset, UIO_WRITE);
    VOP_WRITE(v, &u);
    if(u.uio_resid != 0) {
        panic("swapfile.c: Cannot write to swap file");
        return -1;
    } else {
        spinlock_acquire(&filelock);
        swap_list[free_index].free = 0;
        swap_list[free_index].ppadd = ppaddr;
        swap_list[free_index].pvadd = pvaddr;
        swap_list[free_index].swap_offset = page_offset;
        spinlock_release(&filelock);
        return swap_list[free_index].swap_offset;
    }
}


int swap_in(paddr_t ppadd, off_t offset) {
    unsigned int swap_index;
    struct iovec iov;
    struct uio u;
    //int i;
    int page_index;
    //off_t new_offset;
    int result;

    timesIn++;

    // page_index = -1;
    // for (i=0; i< NUM_PAGES; i++) {
    //     if(swap_list[i].pvadd == pvadd) {
    //         page_index = i;
    //         break;
    //     }
    // } 

    // KASSERT(page_index != -1);
    // new_offset =(off_t)(page_index * PAGE_SIZE);    // Casting
    KASSERT(offset >= 0); // Verifica che l'offset sia positivo
    //KASSERT(pvadd == pvadd); // Verifica che l'indirizzo virtuale sia valido
    page_index = offset/PAGE_SIZE; // Calcola l'indice della pagina nel file di swap
    spinlock_acquire(&filelock);
    // Fix del descriptor dello swapfile
    swap_list[page_index].ppadd =  0;
    swap_list[page_index].pvadd  = 0;
    swap_list[page_index].free  = 1;
    swap_list[page_index].swap_offset = 0;
    spinlock_release(&filelock);
    // Copia nel suo nuovo ppadd

    uio_kinit(&iov, &u, (void *) 
    PADDR_TO_KVADDR(ppadd), PAGE_SIZE, offset, UIO_READ);
    result = VOP_READ(v, &u); // Legge una pagina dal file di swap nella memoria fisica specificata da ppadd.
    KASSERT(result == 0);     // Verifica che la lettura dal file di swap sia avvenuta con successo; genera un panic in caso di errore.

    if(u.uio_resid != 0)
    {
        kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", timesOut, timesIn);
        panic("swapfile.c: Cannot read from swap file");
        return -1;
    }

    return swap_list[page_index].swap_offset;
}


void swap_shutdown(void) {
    int i;
    vfs_close(v);

    for(i=0; i<NUM_PAGES; i++) {
        swap_list[i].ppadd = 0;
        swap_list[i].pvadd = 0;
        swap_list[i].swap_offset = 0;
        swap_list[i].free = 1;
    }
}

int getIn(void) {
    return timesIn;
}
int getOut(void) {
    return timesOut;
}