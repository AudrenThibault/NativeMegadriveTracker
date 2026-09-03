#include "md_manette.h"

#define MANETTE_DONNEES (*(volatile uint8_t *)0xA10003)
#define MANETTE_CTRL    (*(volatile uint8_t *)0xA10009)

static uint16_t tenus, precedents, frappes, relaches, appuis;
static uint8_t  compte_repet;
static uint16_t direction_tenue;

// Répétition automatique : sans elle, parcourir 256 lignes de SONG demande
// 256 appuis. Le premier délai est long pour qu'un appui bref reste UN pas ;
// ensuite ça défile.
#define REPET_DEBUT 16   // images avant la première répétition
#define REPET_PAS   3    // images entre deux répétitions

void md_manette_init(void) {
  MANETTE_CTRL = 0x40;
  MANETTE_DONNEES = 0x40;
  tenus = precedents = frappes = relaches = appuis = 0;
  compte_repet = 0; direction_tenue = 0;
}

// ⚠️ LA LECTURE DE LA MANETTE NE SUPPORTE PAS D'ETRE INTERROMPUE.
//
// Le port se lit en basculant la ligne de selection puis en lisant, deux fois,
// avec une courte attente entre les deux. L'interruption video, elle, dure des
// MILLISECONDES pendant la lecture d'un morceau — sequenceur compris. Si elle
// tombe au milieu de la sequence, le multiplexeur n'est plus dans l'etat qu'on
// croit, et comme les boutons sont actifs a l'etat BAS, un decalage se lit
// comme un APPUI. Releve sur la console : A+B pendant la lecture effacait une
// case au lieu d'armer la selection — un C fantome s'etait invite.
//
// Le masque tient une cinquantaine de microsecondes. Le sequenceur ne perd
// rien : son tick est rattrape a l'image suivante.
static inline uint16_t irq_coupe(void) {
  uint16_t s;
  __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(s) :: "memory");
  return s;
}
static inline void irq_remet(uint16_t s) {
  __asm__ volatile ("move.w %0,%%sr" :: "d"(s) : "memory");
}

static uint16_t lit_brut(void) {
  const uint16_t sr = irq_coupe();
  // Le port se lit en DEUX temps : la sélection à 1 donne la croix, B et C ;
  // à 0 elle donne A et START. Il faut laisser au multiplexeur le temps de
  // basculer, d'où les attentes — sans elles on lit l'état précédent.
  MANETTE_DONNEES = 0x40;
  for (volatile int d = 0; d < 12; d++) { }
  const uint8_t haut = MANETTE_DONNEES;
  MANETTE_DONNEES = 0x00;
  for (volatile int d = 0; d < 12; d++) { }
  const uint8_t bas = MANETTE_DONNEES;
  MANETTE_DONNEES = 0x40;
  irq_remet(sr);

  // Tous les boutons sont actifs à l'état BAS.
  uint16_t e = 0;
  if (!(haut & 0x01)) e |= MD_HAUT;
  if (!(haut & 0x02)) e |= MD_BAS;
  if (!(haut & 0x04)) e |= MD_GAUCHE;
  if (!(haut & 0x08)) e |= MD_DROITE;
  if (!(haut & 0x10)) e |= MD_B;
  if (!(haut & 0x20)) e |= MD_C;
  if (!(bas  & 0x10)) e |= MD_A;
  if (!(bas  & 0x20)) e |= MD_START;
  return e;
}

void md_manette_lit(void) {
  precedents = tenus;
  tenus = lit_brut();
  frappes  = (uint16_t)(tenus & ~precedents);
  relaches = (uint16_t)(precedents & ~tenus);
  appuis   = frappes;

  // La répétition ne porte QUE sur la croix : un bouton d'action qui se
  // répéterait tout seul poserait des valeurs sans qu'on l'ait demandé.
  const uint16_t dir = (uint16_t)(tenus & MD_CROIX);
  if (!dir || dir != direction_tenue) {
    direction_tenue = dir;
    compte_repet = 0;
  } else if (dir) {
    compte_repet++;
    if (compte_repet >= REPET_DEBUT &&
        ((compte_repet - REPET_DEBUT) % REPET_PAS) == 0)
      appuis |= dir;
  }
}

uint16_t md_manette_tenus(void)    { return tenus; }
uint16_t md_manette_frappes(void)  { return frappes; }
uint16_t md_manette_relaches(void) { return relaches; }
uint16_t md_manette_appuis(void)   { return appuis; }
