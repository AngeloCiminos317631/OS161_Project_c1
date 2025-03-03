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
#include <segments.h>

// Aggiunta header file per la Coremap
#include <coremap.h>
// Aggiunta header file per la TLB
#include <vm_tlb.h>
// Aggiunta header file per la gestione della VM
#include <vmc1.h>
//Aggiunta header file per la generazione delle statistiche
#include <statistics.h>


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
	// Incrementa il contatore delle statistiche per le TLB invalidate
	increment_statistics(STATISTICS_TLB_INVALIDATE);
    // Ripristina le interruzioni al livello precedente.
    splx(spl);
}

/*
 * Funzione: as_deactivate
 * Scopo: Disattiva l'address space associato al processo corrente.
 * Questa operazione svuota la TLB (Translation Lookaside Buffer) per assicurare
 * che le traduzioni delle vecchie pagine virtuali non siano più valide.
 */
void
as_deactivate(void)
{
    int i, spl;
    struct addrspace *as;

    /* Recupera l'address space del processo corrente */
    as = proc_getas();
    
    if (as == NULL) {
        /*
         * Se il processo corrente non ha un address space (ad esempio, 
         * un kernel thread), non è necessario fare nulla. 
         * L'address space precedente rimane attivo.
         */
        return;
    }

    /*
     * Disabilita gli interrupt su questa CPU per garantire la consistenza
     * mentre si manipola la TLB. Questo previene condizioni di race
     * durante la modifica delle entry nella TLB.
     */
    spl = splhigh();

    /* Itera su tutte le entry della TLB e le invalida una per una */
    for (i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

    /* Incrementa una statistica che traccia quante volte la TLB è stata invalidata */
    increment_statistics(STATISTICS_TLB_INVALIDATE);

    /* Ripristina il livello degli interrupt al valore precedente */
    splx(spl);
}


/*
 * Configura un segmento all'indirizzo virtuale VADDR di dimensione MEMSIZE. 
 * Il segmento in memoria si estende da VADDR fino a (ma non incluso) VADDR+MEMSIZE.
 *
 * I flag READABLE, WRITEABLE e EXECUTABLE indicano se i permessi di lettura, 
 * scrittura o esecuzione devono essere abilitati per il segmento. 
 * Attualmente, questi flag vengono ignorati. Quando implementerai il sistema 
 * di memoria virtuale (VM), potresti volerli utilizzare.
 */

int as_define_region(struct addrspace *as, uint32_t type, uint32_t offset ,vaddr_t vaddr, size_t memsize,
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