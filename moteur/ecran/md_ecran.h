// ============================================================================
//  L'écran : une grille de caractères, comme LSDJ. Rien d'autre.
//
//  Le VDP en mode 40 colonnes donne des tuiles de 8x8 sur 320x224, soit
//  40 colonnes et 28 lignes. C'est moins large que les 64 colonnes de la DS,
//  d'où une page SONG plus serrée — mais la densité reste suffisante pour dix
//  canaux, qui tiennent en 32 colonnes.
//
//  ⚠️ Les registres écrits ici sont EXACTEMENT ceux dont on se sert, et pas un
//  de plus. Une version antérieure écrivait les vingt-quatre en boucle, dont
//  ceux du DMA et de l'interruption verticale, dont ce programme ne fait aucun
//  usage : la console se figeait avant d'afficher quoi que ce soit, là où
//  l'émulateur ne bronchait pas. Un registre inutilisé n'a pas à être écrit.
// ============================================================================
#ifndef MD_ECRAN_H
#define MD_ECRAN_H

#include <stdint.h>

#define MD_COLS   40
#define MD_LIGNES 28

// ── Les trois rôles, et rien d'autre ──────────────────────────────────────
// Repris tels quels du tracker DS, y compris l'intention : toute page passe
// par ces rôles-là — un intitulé, une valeur, ce que le curseur désigne. Ils
// existent pour qu'on ne PUISSE PAS habiller une page autrement qu'une autre.
// C'est déjà arrivé là-bas, et les pages PSG et PCM ne ressemblaient plus à la
// page FM. Changer l'apparence se fait ici, une fois, pour tout le tracker.
//
// Les teintes viennent de LSDJPal, via le projet DS : un violet clair sur noir,
// un rouge franc pour le repère de lecture. Converties de cinq bits par
// composante à trois, celles du VDP.
#define MD_TITRE   0   // les intitulés fixes
#define MD_DATA    1   // les valeurs qu'on modifie
#define MD_ACCENT  2   // ce que le curseur désigne, et les alertes
#define MD_NUM     3   // les numéros de ligne

// Le curseur n'a pas de couleur à lui : il INVERSE la case, comme LSDJ — un
// pavé plein qu'on repère sans le chercher.
#define MD_INVERSE 0x0100u   // à combiner avec un rôle

// Le SECOND TON d'un rôle : le glyphe prend la couleur 2 de sa palette au lieu
// de la 1. Il sert aux numéros de ligne, que LSDJ colore par groupes de quatre
// en alternant deux teintes.
#define MD_TON2    0x0200u   // à combiner avec un rôle

// Le PAVÉ : le glyphe garde son encre, le vide autour prend la couleur 3 de la
// palette. C'est ainsi que LSDJ marque un groupe de lignes sur deux.
#define MD_FOND2   0x0400u   // à combiner avec un rôle

// Définie dans boot.s : débloque les interruptions, une fois tout prêt.
void md_irq_autorise(void);

void md_ecran_init(void);
void md_ecran_vide(void);
void md_ecran_texte(int col, int lig, uint16_t style, const char *s);
void md_ecran_car(int col, int lig, uint16_t style, char c);
void md_ecran_hex(int col, int lig, uint16_t style, uint32_t v, int chiffres);
void md_ecran_dec(int col, int lig, uint16_t style, uint32_t v, int chiffres);
void md_ecran_fond(uint16_t couleur);
void md_ecran_attend_image(void);

#endif
