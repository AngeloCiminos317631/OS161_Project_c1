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
#include <elf.h>

#include <segments.h>
#include <vmc1.h>


struct segment* seg_create(void) {
    struct segment* seg;

    seg = kmalloc(sizeof(struct segment));
    KASSERT(seg != NULL);

    seg->p_type = 0;
    seg->p_offset = 0;
    seg->p_vaddr = 0;
    seg->p_filesz = 0;
    seg->p_memsz = 0;
    seg->p_permission = 0;

    return seg;
}

int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission) {
    
    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);

    seg->p_type = p_type;
    seg->p_offset = p_offset;
    seg->p_vaddr = p_vaddr;
    seg->p_filesz = p_filesz;
    seg->p_memsz = p_memsz;
    seg->p_permission = p_permission;

    return 0;
}

void seg_destroy(struct segment* seg) {

    KASSERT(seg != NULL);
    kfree(seg);
}

int seg_define_stack(struct segment* seg) {

    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);

    seg->p_type = PT_LOAD;
    seg->p_offset = 0;
    seg->p_vaddr = USERSTACK;
    seg->p_filesz = 0;
    seg->p_memsz = USERSTACK - VMC1_STACKPAGES * PAGE_SIZE;
    seg->p_permission = PF_R | PF_W;

    return 0;
}
