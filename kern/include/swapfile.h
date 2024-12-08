#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <types.h>

#define PAGE_SIZE 32768 //4KB
#define FILE_SIZE 9*1024*1024 //9MB
#define NUM_PAGES FILE_SIZE / PAGE_SIZE

struct swap_page {
    // Inizializzazione della swap page
    paddr_t ppadd;
    vaddr_t pvadd;       // Usato per lo swap in
    off_t swap_offset ;
    int free;               // 1: libera, 0: occupata


};

void swapfile_init(void);
int swap_out(paddr_t ppaddr, vaddr_t pvaddr);
int swap_in(paddr_t ppadd, vaddr_t pvadd, off_t offset);
void swap_shutdown(void);
int getIn(void);
int getOut(void);
#endif