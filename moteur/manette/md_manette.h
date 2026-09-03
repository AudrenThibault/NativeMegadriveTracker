// ============================================================================
//  La manette trois boutons, et la grammaire LSDJ reportée dessus.
//
//  Il n'y a pas de SELECT sur une Mega Drive. Le report, décidé et figé :
//
//     A     = le SELECT de LSDJ   (navigation)
//     B     = le B de la DS       (copier, armer, grand saut)
//     C     = le A de la DS       (poser et modifier une valeur)
//     C+B   = effacer             (le A+B de la DS)
//     START = jouer / arrêter
//
//  Tout tient sur une manette d'origine : on n'exige jamais X, Y ni Z, pour
//  que le tracker marche sur n'importe quelle machine.
// ============================================================================
#ifndef MD_MANETTE_H
#define MD_MANETTE_H

#include <stdint.h>

#define MD_HAUT   0x0001
#define MD_BAS    0x0002
#define MD_GAUCHE 0x0004
#define MD_DROITE 0x0008
#define MD_B      0x0010
#define MD_C      0x0020
#define MD_A      0x0040
#define MD_START  0x0080
#define MD_CROIX  (MD_HAUT | MD_BAS | MD_GAUCHE | MD_DROITE)

void     md_manette_init(void);
void     md_manette_lit(void);       // une fois par image
uint16_t md_manette_tenus(void);     // boutons maintenus
uint16_t md_manette_frappes(void);   // front d'appui seul
uint16_t md_manette_relaches(void);  // front de relâchement
uint16_t md_manette_appuis(void);    // front d'appui + répétition automatique

#endif
