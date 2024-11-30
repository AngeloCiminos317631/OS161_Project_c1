#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <types.h>
#include <pt.h>
#include <addrspace.h>

struct segment {
    uint32_t		p_type;
	uint32_t		p_offset;
	uint32_t		p_vaddr;
	uint32_t		p_filesz;
	uint32_t		p_memsz;
	uint32_t		p_permission;
	struct vnode	*vnode;
};

struct segment* seg_create(void);
int seg_define(
	struct segment* seg,
	uint32_t p_type,
	uint32_t p_offset,
	uint32_t p_vaddr,
	uint32_t p_filesz,
	uint32_t p_memsz,
	uint32_t p_permission,
	struct vnode *v);
void seg_destroy(struct segment*);
int seg_define_stack(struct segment*);
int seg_load_page(struct segment* seg, vaddr_t va, paddr_t pa);
int seg_copy(struct segment *old, struct segment **ret);
void zero(paddr_t paddr, size_t n);

#endif