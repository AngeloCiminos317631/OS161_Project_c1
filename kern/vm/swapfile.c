#include <types.h>
#include <kern/fcntl.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>
#include <vm.h>
#include <bitmap.h>
#include <swapfile.h>

// Abbiamo bisogno di una bitmap per tenere traccia di quali chunk siano pieni e vuoti nello swapfile
//// static struct bitmap *map;
// Creiamo invece una lista di entries
static struct swap_page swap_list[NUM_PAGES];



// Solo un processo alla volta può accedere allo swapfile per cui abbiamo bisogno di uno spinlock
static struct spinlock filelock = SPINLOCK_INITIALIZER;

static struct vnode *v = NULL;

void swapfile_init(void) {
    int result;

    // Quando a runtime sono necessari più di 9MB => panic
    result = vfs_open((char *)"./SWAPFILE", O_RDWR | O_CREAT , 777, &v);
    KASSERT(result == 0);
    return;

   
}


int swap_out(paddr_t ppaddr) {
    // Dato l'indirizzo fisico della pagina da swappare
    // restituisce l'offset a cui la salviamo nello swapfile
    int free_index = -1;
    
    struct iovec iov;
    struct uiov u;
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
    if(free_index == -1) {
        panic("swapfile.c: Out of swap space \n");
        return -1;
    }

    page_offset = free_index * PAGE_SIZE;
    KASSERT(page_offset < FILE_SIZE);

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppaddr), PAGE_SIZE, page_offset, UIO_WRITE);
    VOP_WRITE(v, &u);
    if(u.uio_resid != 0) {
        panic("swapfile.c: Cannot write to swap file");
        return -1;
    } else {
        return 0;
    }
}


int swap_in(paddr_t ppadd, vaddr_t pvadd) {
    unsigned int swap_index;
    struct iovec iov;
    struct uio u;
    int i;
    int page_index;
    off_t new_offset;


    page_index = -1;
    for (i=0; i< NUM_PAGES; i++) {
        if(swap_list[i].pvadd == pvadd) {
            page_index = i;
            break;
        }
    } 

    KASSERT(page_index != -1);
    new_offset =(off_t)(page_index * PAGE_SIZE);    // Casting

    spinlock_acquire(&filelock);
    // Fix del descriptor dello swapfile
    swap_list[page_index].ppadd =  NULL;
    swap_list[page_index].vadd  = NULL;
    swap_list[page_index].free  = 1;
    // Copia nel suo nuovo ppadd

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppadd), PAGE_SIZE, new_offset, UIO_READ);
    VOP(v, &u);
    KASSERT(u.uio_resid ! = 0);

    spinlock_release(&filelock);

    return 0;
}


void swap_shutdown(void) {
   vfs_close(v);
}