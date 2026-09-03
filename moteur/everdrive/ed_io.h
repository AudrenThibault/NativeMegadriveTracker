// ============================================================================
//  Accès à la carte SD de l'EverDrive — NOTRE implémentation.
//
//  ⚠️ Aucune ligne ne vient de `krikzz/mega-ed-pub`. Ce dépôt n'a AUCUNE
//  licence — ni fichier, ni notice, ni mention dans le README ou l'historique
//  git — donc, par défaut, tous droits réservés : on ne peut pas le
//  redistribuer dans un produit vendu, et ce tracker est destiné à la vente.
//
//  Ce qu'on lui emprunte, c'est le PROTOCOLE : l'adresse des registres, les
//  numéros de commande, les bits d'état, la forme des trames. Des faits, pas
//  leur écriture — une interface n'est pas une œuvre. Leurs sources servent de
//  documentation, ce pour quoi elles sont publiées.
//
//  Le protocole, en clair, pour n'avoir plus jamais à y retourner :
//
//    Registres, à partir de 0xA130D0 :
//      +0x01  octet   FIFO, données (lecture et écriture)
//      +0x02  mot     FIFO, état : bit 15 = il y a à lire,
//                     bits 0-10 = combien d'octets attendent
//      +0x06  mot     horloge libre, une unité = 1 ms
//
//    Trame de commande : quatre octets — '+', 0xD4, code, code inversé.
//    Les entiers multi-octets passent en GROS-BOUTISTE (l'ordre mémoire du
//    68000, puisque l'octet est recopié tel quel).
//    Une chaîne = sa longueur sur 16 bits, puis ses octets, sans le zéro final.
//
//    Après chaque commande, on demande l'état (code 0x10) : deux octets, dont
//    le premier DOIT valoir 0xA5. Le second est le code d'erreur, 0 si tout
//    va bien.
//
//    Écrire un fichier : code 0xCC, la taille sur 32 bits, puis par blocs de
//    1024 octets au plus — un octet d'acquittement à lire (0 = continue) avant
//    chaque bloc.
//
//  DIFFÉRENCE ASSUMÉE avec la référence : toutes nos attentes sont bornées par
//  l'horloge de la cartouche. Sans délai de garde, une cartouche qui ne répond
//  pas — mauvais modèle, carte absente, micrologiciel ancien — fige la console
//  sans rien dire. On préfère un code d'erreur.
// ============================================================================
#ifndef ED_IO_H
#define ED_IO_H

#include <stdint.h>

// Codes rendus par les fonctions ci-dessous.
#define ED_OK           0
#define ED_ABSENT       0xE0  // rien n'a répondu : pas d'EverDrive, ou trop ancien
#define ED_EXPIRE       0xE1  // la cartouche a cessé de répondre en cours de route
#define ED_ETAT_FAUX    0xE2  // réponse d'état mal formée (premier octet ≠ 0xA5)
// Tout autre code non nul vient de la cartouche elle-même (erreur FatFs).

// La cartouche répond-elle ? À appeler avant tout le reste.
int ed_present(void);

// Monte la carte SD si elle ne l'est pas déjà.
int ed_disque_pret(void);

// Écrit un fichier d'un bloc. `ecrase` non nul : on repart d'un fichier vide ;
// sinon on ajoute à la fin, ce qui garde l'historique des essais successifs.
int ed_ecrit_fichier(const char *chemin, const void *donnees,
                     uint32_t taille, int ecrase);

#endif
