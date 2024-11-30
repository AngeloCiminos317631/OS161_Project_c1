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
#include <vnode.h>
#include <uio.h>

void zero(paddr_t paddr, size_t n) {
    bzero((void *)PADDR_TO_KVADDR(paddr), n);
}


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
    seg->vnode = NULL;

    return seg;
}

int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission, struct vnode *v) {
    
    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);
    KASSERT(seg->vnode == NULL);

    seg->p_type = p_type;
    seg->p_offset = p_offset;
    seg->p_vaddr = p_vaddr;
    seg->p_filesz = p_filesz;
    seg->p_memsz = p_memsz;
    seg->p_permission = p_permission;
    seg->vnode = v;

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
    KASSERT(seg->vnode == NULL);

    seg->p_type = PT_LOAD;
    seg->p_offset = 0;
    seg->p_vaddr = USERSTACK - (VMC1_STACKPAGES * PAGE_SIZE);
    seg->p_filesz = 0;
    seg->p_memsz = VMC1_STACKPAGES * PAGE_SIZE;
    seg->p_permission = PF_S;
    seg->vnode = NULL;

    return 0;
}


int seg_load_page(struct segment* seg, vaddr_t va, paddr_t pa) {
    vaddr_t page_offset, load_offset;
    unsigned long index;
    int result;
    paddr_t paddr_load;
    size_t len, npages;
    struct addrspace* as;

    // get the index number of the page which made the fault
    index = (va - (seg->p_vaddr & PAGE_FRAME)) / PAGE_SIZE;
    // we cannot get an index of a page out of bound
    KASSERT(index*PAGE_SIZE < seg->p_filesz);
    KASSERT(index < seg->p_memsz);
    // how much offset do we have inside the page
    page_offset = seg->p_vaddr & ~(PAGE_FRAME);

    npages = seg->p_memsz + (va & ~(vaddr_t)PAGE_FRAME);
	npages = (npages + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = npages / PAGE_SIZE;
    paddr_load = pa + page_offset;
    // KASSERT(paddr_load/PAGE_SIZE <= 1);
    len = (PAGE_SIZE - page_offset > seg->p_filesz) ? seg->p_filesz : PAGE_SIZE - page_offset;
    load_offset = seg->p_filesz + PAGE_SIZE * index;

    zero(pa, PAGE_SIZE);



    as = proc_getas();
    if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}
        result = load_segment(as, seg->vnode, load_offset, PADDR_TO_KVADDR(paddr_load),
                len, len,
                seg->p_permission & PF_X);
    if (result) {
        return result;
    }

    return 0;
}

int seg_copy(struct segment *old, struct segment **ret) {
    struct segment *newps;
    int result;
    
    KASSERT(old != NULL);
    newps = seg_create();
    if (newps == NULL) {
        return ENOMEM;
    }
    result = seg_define(newps, old->p_type, old->p_offset, old->p_vaddr, old->p_filesz, old->p_memsz, old->p_permission, old->vnode);
    KASSERT(result == 0);
    
    *ret = newps;
    return 0;
}