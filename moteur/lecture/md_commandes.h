#ifndef MD_COMMANDES_H
#define MD_COMMANDES_H
#include <stdint.h>

// ============================================================================
//  Les commandes du format, portées telles quelles du tracker DS.
//
//  ⚠️ L'ORDRE DE CES TABLES EST LE FORMAT. Le `.mdm` ne range pas une lettre
//  ni un code : il range le RANG dans ces tables. Y insérer une ligne au
//  milieu change la signification de tous les morceaux déjà écrits. On ajoute
//  À LA FIN, jamais ailleurs.
//
//  Deux colonnes, deux tables, et ce n'est pas un doublon :
//    CMD    les vingt et une lettres de LSDJ — ce qu'on écrit à la main ;
//    MD CMD les codes de DefleMask — ce qu'un fichier importé déverse.
//  Quand une ligne porte les deux, c'est CMD qui gagne.
// ============================================================================

#define MD_CMD_NOMBRE 21

// La lettre d'une commande, 0 si le rang n'existe pas.
char md_cmd_lettre(int rang);

// Ce que la commande FAIT, en toutes lettres. Une lettre seule ne se retient
// pas ; le nom s'affiche sous la grille tant que le curseur est sur la colonne.
const char *md_cmd_nom(int rang);

// ── Les commandes MD, par leur code ───────────────────────────────────────
// Elles agissent DIRECTEMENT sur la puce : un registre, une valeur. Ce sont
// les plus simples à exécuter et les plus payantes à l'oreille.
#define MD_MDCMD_NOMBRE 30
uint8_t md_mdcmd_code(int rang);
const char *md_mdcmd_nom(int rang);

// Ce que fait une commande MD. Rendu séparément pour que le séquenceur n'ait
// pas à connaître les codes.
enum {
  MD_P_RIEN = 0,
  MD_P_LFO,      // x : marche/arrêt, y : vitesse
  MD_P_FB,       // feedback 0-7
  MD_P_TL1, MD_P_TL2, MD_P_TL3, MD_P_TL4,   // niveau d'un opérateur, 00-7F
  MD_P_MUL,      // x : opérateur 1-4, y : multiple
  MD_P_ARALL,    // attaque des quatre
  MD_P_AR1, MD_P_AR2, MD_P_AR3, MD_P_AR4,
  MD_P_NOISE,    // mode de bruit PSG
  MD_P_ALG,      // algorithme 0-7
  MD_P_PAN,      // 0 centre, 1 gauche, 2 droite
  MD_P_PMS, MD_P_AMS
};
int md_mdcmd_action(int rang);

// ── L'EFFET, celui que le sequenceur execute ─────────────────────────────
// ⚠️ UNE LETTRE ET UN CODE PEUVENT ETRE LE MEME EFFET. « C » et le code 00
// sont tous deux l'arpege ; « V » et 04 sont le meme vibrato. Le sequenceur
// ne connait donc ni les lettres ni les codes : il ne connait que cette
// liste, et il n'y a qu'UNE implementation par effet. Les ecrire deux fois,
// c'est se garantir qu'un jour l'une des deux sera corrigee et pas l'autre.
enum {
  MD_E_RIEN = 0,
  MD_E_ARPEGE, MD_E_PORTA_HAUT, MD_E_PORTA_BAS, MD_E_PORTA_TON,
  MD_E_VIBRATO, MD_E_PORTA_VOL, MD_E_VIB_VOL, MD_E_TREMOLO,
  MD_E_VOL_SLIDE, MD_E_RETRIG, MD_E_SAUT, MD_E_RUPTURE,
  MD_E_VIB_PROF, MD_E_FINE, MD_E_COUPE, MD_E_RETARD,
  MD_E_TEMPO, MD_E_VITESSE, MD_E_VOL_GLOBAL, MD_E_PAN,
  MD_E_TABLE, MD_E_HOP, MD_E_PITCH
};
// Un effet CONTINU survit au changement de ligne : un pitch bend, un vibrato,
// un arpege continuent de tourner jusqu'a ce qu'on les arrete, comme dans LSDJ
// et comme sur la DS (md_effect_is_continuous). Sans ca un glissando
// s'interrompt sechement au bout d'une ligne. On l'arrete en reecrivant la
// meme commande avec la valeur 00.
int md_effet_continu(int effet);

int md_cmd_effet(int rang);     // colonne CMD, par le rang de la lettre
int md_mdcmd_effet(int rang);   // colonne MD CMD, par le rang du code

#endif
