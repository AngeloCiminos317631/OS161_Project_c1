# Progetto os161-c1

## Introduzione
Il progetto mira a espandere il modulo di gestione della memoria (dumbvm), sostituendolo completamente con un gestore di memoria virtuale più potente basato sulle tabelle delle pagine dei processi. Il progetto richiede inoltre di lavorare sulla TLB.

## Autori
* s292671 Donato Agostino Modugno
* s308455 Simone Colagiovanni
* s317631 Angelo Cimino


## Divisione del lavoro


## Scelte Implemetative
DA MODIFICARE - RISPONDERE A QUESTE RICHIESTE 
( The project goal is to replace dumbvm with a new virtual-memory system that relaxes some (not all) of 
dumbvm’s limitations.  
The new system will implement demand paging (with a page table) with page replacement, according to the following requirements: 
• New TLB support is needed, by implementing a replacement policy for the TLB, so that the kernel 
will not crash if the TLB fills up. 
• On-demand loading of pages: this will allow programs that have address spaces larger than physical memory to run, provided that they do not touch more pages than will fit in physical memory.  • In addition, page replacement (based on victim selection) is needed, so that a new frame can be found when no more free frames are available.  
• Different page table policies can be implemented: e.g. per process page table or Inverted PT, victim selection policies, free frame management, etc. The choice can be discussed and deferred to a later moment. )

## Moduli principali

### addrspace.c
### coremap.c
### pt.c
### segments.c
### statistics.c
### swapfile.c
### vm_tlb.c
### vmc1.c

## Funzionalità implementate

### Tlb Management
### Read-Only Text Segment 
### On-Demand Page Loading
### Page Replacement 
### Instrumentation ( Statistiche )

## Considerazioni finali
