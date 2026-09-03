#include "md_ecran.h"
#include "md_font.h"
#include "md_puces.h"

#define VDP_DATA   (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL   (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL32 (*(volatile uint32_t *)0xC00004)
#define VDP_ETAT   (*(volatile uint16_t *)0xC00004)

#define PLAN_A 0xC000

static void reg(uint8_t r, uint8_t v) { VDP_CTRL = 0x8000 | (r << 8) | v; }
static void vram(uint16_t a) {
  VDP_CTRL32 = 0x40000000u | ((uint32_t)(a & 0x3FFF) << 16) | ((a >> 14) & 3);
}
static void cram(uint16_t a) {
  VDP_CTRL32 = 0xC0000000u | ((uint32_t)(a & 0x3FFF) << 16) | ((a >> 14) & 3);
}

// ── La police, en QUATRE jeux ──────────────────────────────────────────────
// Les tuiles 0 à 63 portent les caractères normaux, les 64 suivantes les mêmes
// EN INVERSE — le glyphe en noir sur un pavé plein, c'est le curseur de LSDJ,
// un bloc et pas une couleur. Les 64 suivantes sont un SECOND TON — le glyphe
// y est peint avec la couleur 2 au lieu de la 1 — et les 64 dernières portent
// un PAVÉ : le glyphe garde la couleur 1, mais le vide autour prend la 3.
//
// ⚠️ Ce pavé est ce qui manquait aux numéros de ligne. LSDJ ne change pas
// seulement l'encre d'un groupe de quatre à l'autre : il pose un FOND COLORÉ
// derrière un groupe sur deux. C'est ça qui se voit de loin ; une nuance
// d'encre, non.
//
// ⚠️ Pourquoi des jeux de tuiles plutôt que des palettes de plus : le VDP n'en
// offre que QUATRE, et les quatre rôles du tracker les occupent déjà. Mais
// chaque palette a seize couleurs dont on n'utilisait que deux — il restait
// donc quatorze teintes libres partout. Deux kilo-octets de VRAM par jeu,
// c'est le bon échange : ils étaient de toute façon vides.
//
// La tuile 0 est l'espace, donc un plan vidé est un fond propre sans rien y
// écrire.
static void police(void) {
  vram(0);
  for (int jeu = 0; jeu < 4; jeu++)
    for (int c = 0; c < 0x40; c++)
      for (int l = 0; l < 8; l++) {
        const uint8_t bits = (l < MD_FONT_HT) ? md_font[c][l] : 0;
        uint32_t mot = 0;
        for (int p = 0; p < 8; p++) {
          const int allume = (p < 5) && (bits & (0x10 >> p));
          uint32_t couleur;
          if (jeu == 1)      couleur = allume ? 0u : 1u;   // inverse
          else if (jeu == 3) couleur = allume ? 1u : 3u;   // sur pavé coloré
          else               couleur = allume ? (jeu == 2 ? 2u : 1u) : 0u;
          mot |= couleur << ((7 - p) * 4);
        }
        VDP_DATA = (uint16_t)(mot >> 16);
        VDP_DATA = (uint16_t)mot;
      }
}

void md_ecran_init(void) {
  // Le Z80 d'abord : à l'allumage il exécute n'importe quoi et réclame le bus.
  // La séquence exacte vit dans md_puces.c, parce qu'elle décide aussi du sort
  // du YM2612 — la même ligne de reset commande les deux.
  md_z80_prepare();

  // TMSS : sans lui les consoles récentes coupent le VDP. La lecture du
  // registre de version est indispensable — écrire en 0xA14000 sur une console
  // qui n'en a pas provoque une erreur de bus.
  if (*(volatile uint8_t *)0xA10001 & 0x0F)
    *(volatile uint32_t *)0xA14000 = 0x53454741;   // "SEGA"

  // Le générateur de sons se tait : ses quatre voies sortent d'on ne sait où.
  *(volatile uint8_t *)0xC00011 = 0x9F;
  *(volatile uint8_t *)0xC00011 = 0xBF;
  *(volatile uint8_t *)0xC00011 = 0xDF;
  *(volatile uint8_t *)0xC00011 = 0xFF;

  //   0x0000 tuiles       0xD000 fenêtre (inutilisée)
  //   0xC000 plan A       0xF000 sprites
  //   0xE000 plan B       0xF800 défilement horizontal
  // ⚠️ Sprites et défilement doivent être SÉPARÉS : les mettre tous deux en
  // 0xF000 ne se voit pas sur une VRAM vierge, mais après le menu de la
  // cartouche le VDP y trouve des sprites parasites et un défilement aberrant.
  reg(0,  0x04);
  reg(1,  0x64);   // affichage allumé, mode 5, INTERRUPTION VERTICALE — pas de DMA
  reg(2,  0x30);
  reg(3,  0x34);
  reg(4,  0x07);
  reg(5,  0x78);
  reg(7,  0x00);
  reg(11, 0x00);
  reg(12, 0x81);   // 40 colonnes
  reg(13, 0x3E);
  reg(15, 0x02);
  reg(16, 0x01);   // plans 64 x 32
  reg(17, 0x00);
  reg(18, 0x00);

  // Toute la VRAM, pas seulement le plan du texte : c'est ce qui efface les
  // graphismes laissés par le menu de la cartouche.
  vram(0);
  for (long i = 0; i < 32768; i++) VDP_DATA = 0;
  VDP_CTRL32 = 0x40000010u;                 // défilement vertical
  for (int i = 0; i < 40; i++) VDP_DATA = 0;

  // Une palette par rôle. La couleur 0 est le fond, la 1 porte le glyphe —
  // et une tuile inversée échange les deux, ce qui donne le pavé du curseur
  // sans consommer une palette de plus.
  cram(0);
  VDP_DATA = 0x0000; VDP_DATA = 0x0ECE;    // 0 TITRE   violet clair
  for (int i = 0; i < 14; i++) VDP_DATA = 0;
  VDP_DATA = 0x0000; VDP_DATA = 0x0EAA;  // 1 DATA    violet atténué
  for (int i = 0; i < 14; i++) VDP_DATA = 0;
  VDP_DATA = 0x0000; VDP_DATA = 0x044E;   // 2 ACCENT  rouge franc
  for (int i = 0; i < 14; i++) VDP_DATA = 0;
  // ⚠️ La palette des NUMÉROS DE LIGNE en porte DEUX, comme sur la DS : LSDJ
  // colore les numéros par groupes de quatre lignes en alternant deux teintes,
  // et c'est ce qui donne le rythme à l'œil sur deux cent cinquante-six
  // lignes. Sans ça on compte les lignes une par une.
  // Les trois teintes des NUMÉROS DE LIGNE, converties de celles de la DS
  // (kNumTexteA, kNumTexteB, kNumFondA) : l'encre du groupe fort, celle du
  // groupe faible, puis le pavé qui va derrière le groupe fort. Le fond du
  // groupe faible est le noir de l'écran, donc la couleur 0 suffit.
  VDP_DATA = 0x0000; VDP_DATA = 0x0CA6;      // 1 encre du groupe fort
  VDP_DATA = 0x0A88;                          // 2 encre du groupe faible
  VDP_DATA = 0x0E08;                          // 3 le pavé, violet
  for (int i = 0; i < 12; i++) VDP_DATA = 0;

  police();
}

void md_ecran_vide(void) {
  vram(PLAN_A);
  for (int i = 0; i < 64 * 32; i++) VDP_DATA = 0;
}

void md_ecran_car(int col, int lig, uint16_t style, char c) {
  if (col < 0 || col >= MD_COLS || lig < 0 || lig >= MD_LIGNES) return;
  if (c >= 'a' && c <= 'z') c -= 32;          // la police n'a que des majuscules
  if (c < 0x20 || c > 0x5F) c = ' ';
  // Un seul des quatre jeux à la fois : l'inversion du curseur prime, puis le
  // pavé, puis le second ton.
  const uint16_t jeu = (style & MD_INVERSE) ? 1
                     : (style & MD_FOND2)   ? 3
                     : (style & MD_TON2)    ? 2 : 0;
  const uint16_t tuile = (uint16_t)(c - 0x20) + jeu * 0x40;
  vram((uint16_t)(PLAN_A + (lig * 64 + col) * 2));
  VDP_DATA = (uint16_t)(((style & 3) << 13) | tuile);
}

void md_ecran_texte(int col, int lig, uint16_t style, const char *s) {
  for (; *s; s++, col++) md_ecran_car(col, lig, style, *s);
}

void md_ecran_hex(int col, int lig, uint16_t style, uint32_t v, int chiffres) {
  for (int i = chiffres - 1; i >= 0; i--) {
    const uint8_t n = (v >> (i * 4)) & 0xF;
    md_ecran_car(col++, lig, style, (char)(n < 10 ? '0' + n : 'A' + n - 10));
  }
}

void md_ecran_dec(int col, int lig, uint16_t style, uint32_t v, int chiffres) {
  char t[12];
  for (int i = chiffres - 1; i >= 0; i--) { t[i] = (char)('0' + v % 10); v /= 10; }
  for (int i = 0; i < chiffres; i++) md_ecran_car(col + i, lig, style, t[i]);
}

void md_ecran_fond(uint16_t couleur) { cram(0); VDP_DATA = couleur; }

// On attend le retour vertical AVANT de redessiner : sans ça le VDP affiche
// une grille à moitié réécrite, et la page semble clignoter à chaque
// déplacement du curseur.
void md_ecran_attend_image(void) {
  while (VDP_ETAT & 0x0008) { }   // sortir d'un éventuel retour en cours
  while (!(VDP_ETAT & 0x0008)) { }
}
