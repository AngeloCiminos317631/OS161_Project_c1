# Progetto os161-c1

## Coremap ( Bozza ) (Angelo)

Il modulo coremap gestisce e traccia l'uso della memoria fisica nel sistema, fornendo un sistema di allocazione e rilascio di pagine di memoria sia per il kernel sia per i processi utente. La coremap è una struttura dati che mantiene informazioni su ogni frame di memoria fisica, inclusi lo stato (free, fixed, dirty, clean), l'indirizzo virtuale associato, lo spazio di indirizzi (addrspace) del processo, e la dimensione dell'allocazione.

Le principali funzionalità del modulo includono:

- Inizializzazione e spegnimento: Configura e libera la coremap, rendendo la memoria fisica tracciabile.
- Allocazione e rilascio per processi utente: Permette a un processo di richiedere pagine di memoria fisica o di liberarle quando non più necessarie.
- Allocazione e rilascio per il kernel: Consente al kernel di allocare e rilasciare pagine contigue in modo efficiente.
