#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <types.h>


struct segment {
    uint32_t	p_type;
	uint32_t	p_offset;
	uint32_t	p_vaddr;
	uint32_t	p_filesz;
	uint32_t	p_memsz;
	uint32_t	p_permission;
};

struct segment* seg_create(void);
int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission);
void seg_destroy(struct segment*);
int seg_define_stack(struct segment*);

#endif