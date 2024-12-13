/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>
#include <vfs.h>
#include <mips/tlb.h>
#include <swapfile.h>

// Aggiunta header file per la Coremap
#include <coremap.h>
// Aggiunta header file per la TLB
#include <vm_tlb.h>
// Aggiunta header file per la gestione della VM
#include <vmc1.h>


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace* as_create(void) {
    struct addrspace* as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }

	// Valutare per prossime modifiche

    // as->page_table = pt_create(); // Creazione della page table
    // if (as->page_table == NULL) {
    //     kfree(as);
    //     return NULL;
    // }

    // creazione segmenti per le tre parti dell'addrspace
	as->code = seg_create();
	as->data = seg_create();
	as->stack = seg_create();
	as->pt = pt_create(); // Creazione della page table
	swapfile_init();
    return as;
}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	int result;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	result = seg_copy(old->code, &newas->code);
	KASSERT(result == 0);
	result = seg_copy(old->data, &newas->data);
	KASSERT(result == 0);
	result = seg_copy(old->stack, &newas->stack);
	KASSERT(result == 0);
	newas->pt = old->pt; // Copia della page table

	*ret = newas;
	return 0;
}

void as_destroy(struct addrspace* as) {
   
   // Valutare per prossime modifiche
   
    // if (as->page_table != NULL) {
    //     pt_destroy(as->page_table); // Distruzione della page table
    // }

	struct vnode *v;
	KASSERT(as != NULL);
	kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", getOut(), getIn());
	v = as->code->vnode;
	seg_destroy(as->code);
	seg_destroy(as->data);
	seg_destroy(as->stack);
	pt_destroy(as->pt);
	vfs_close(v); 
	kfree(as);
	// swap_shutdown();  Commentato per RIMOZIONE
}


/**
 * Attiva l'address space del processo corrente.
 * 
 * Se il processo ha un address space, invalida tutte le voci nella TLB
 * per assicurarsi che le mappature siano aggiornate. Se il processo è un
 * thread del kernel senza address space, non viene eseguita alcuna operazione.
 */
void as_activate(void)
{
    int i, spl;
    struct addrspace *as;

    // Ottieni l'address space del processo corrente.
    as = proc_getas();

    // Se il processo non ha un address space (es. thread del kernel), non fare nulla.
    if (as == NULL) {
        return;
    }

    // Disabilita le interruzioni per evitare che la TLB venga modificata durante l'operazione.
    spl = splhigh();

    // Invalida tutte le voci nella TLB per il nuovo address space.
    for (i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    // Ripristina le interruzioni al livello precedente.
    splx(spl);
}

/*
 * Questa funzione invalida tutte le entry nella Translation Lookaside Buffer (TLB) 
 * per il processo corrente. È utilizzata per garantire che vecchie mappature di indirizzi virtuali 
 * non vengano utilizzate erroneamente quando il processo non è attivo. La funzione disabilita 
 * temporaneamente gli interrupt per proteggere la coerenza della TLB durante l'operazione.
 */
void as_deactivate(void) {
    int i, spl;
    struct addrspace *as;

    // Ottiene lo spazio degli indirizzi (address space) associato al processo corrente
    as = proc_getas();
    if (as == NULL) {
        // Se non c'è un address space (ad esempio, è un thread del kernel),
        // non è necessario fare nulla.
        return;
    }

    // Disabilita gli interrupt per garantire che la modifica alla TLB non venga interrotta.
    spl = splhigh();

    // Invalida tutte le entry nella TLB.
    // Questo rimuove tutte le mappature degli indirizzi virtuali per il processo corrente,
    // assicurandosi che non vengano riutilizzate per errore.
    for (i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    // Riabilita gli interrupt ora che il TLB è stato pulito.
    splx(spl);
}

int
as_define_region(struct addrspace *as, uint32_t type, uint32_t offset ,vaddr_t vaddr, size_t memsize,
		 uint32_t filesize, int readable, int writeable, int executable, int seg_n, struct vnode *v)
{
	int res = 1;
	int perm = 0x0;

	KASSERT(seg_n < 2); // Solo due segmenti (code e data)

	if(readable)
		perm = perm | PF_R;
	if(writeable)
		perm = perm | PF_W;
	if(executable)
		perm = perm | PF_X;

	if(seg_n == 0)
		res = seg_define(as->code, type, offset, vaddr, filesize, memsize, perm, v);
	else if(seg_n == 1)
		res = seg_define(as->data, type, offset, vaddr, filesize, memsize, perm, v);
	
	KASSERT(res == 0);	// segment defined correctly
	return res;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	int res;

	res = seg_define_stack(as->stack);

	KASSERT(res == 0);
	// File pointer iniziale USER-LEVEL
	*stackptr = USERSTACK;

	(void) as;

	return 0;
}

struct segment* as_get_segment(struct addrspace *as, vaddr_t va) {
	
	KASSERT(as != NULL);

	uint32_t base_seg1, top_seg1;
	uint32_t base_seg2, top_seg2;
	uint32_t base_seg3, top_seg3;
	base_seg1 = as->code->p_vaddr;
	top_seg1 = (as->code->p_vaddr + as->code->p_memsz);
	base_seg2 = as->data->p_vaddr;
	top_seg2 = (as->data->p_vaddr + as->data->p_memsz);
	base_seg3 = as->stack->p_vaddr;
	top_seg3 = USERSTACK;
	if(va >= base_seg1 && va <= top_seg1) {
		return as->code;
	}
	else if(va >= base_seg2 && va <= top_seg2) {
		return as->data;
	}
	else if (va >= base_seg3 && va <= top_seg3) {
		return as->stack;
	}
	return NULL;
}