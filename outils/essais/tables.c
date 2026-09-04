// Un morceau minimal, une table qui porte un H, et on regarde la
// TRANSPOSITION que le lecteur pose tick apres tick. Si le H boucle, la suite
// des hauteurs se repete ; s'il ne fait rien, elle deroule les seize lignes.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "md_song.h"
#include "md_lecture.h"
#include "md_commandes.h"

extern int derniere_hauteur[10];

static int rang_de(char lettre) {
  for (int r = 0; r < MD_CMD_NOMBRE; r++) if (md_cmd_lettre(r) == lettre) return r;
  return -1;
}

static void pose_morceau(void) {
  uint8_t *m = md_travail();
  memset(m, 0, MD_TAILLE_TOTALE);
  for (uint32_t o = MD_OFF_SONG; o < MD_OFF_SONG + MD_TAILLE_SONG; o++) m[o] = MD_VIDE;
  for (uint32_t o = MD_OFF_CHAINS; o < MD_OFF_CHAINS + MD_TAILLE_CHAINS; o += 2) m[o] = MD_VIDE;

  // ⚠️ TOUTE ligne de phrase doit porter MD_VIDE dans ses colonnes de
  // commande. A zero, c'est la commande de RANG 0 — « A », changer de table —
  // et les quinze lignes vides relançaient la table six ticks sur six.
  for (uint32_t o = MD_OFF_PHRASES; o < MD_OFF_PHRASES + MD_TAILLE_PHRASES;
       o += MD_PHRASE_OCTETS) { m[o + 3] = MD_VIDE; m[o + 5] = MD_VIDE; }

  m[MD_OFF_SONG + 0] = 0;                        // voie FM1, ligne 0 -> chain 00
  m[MD_OFF_CHAINS + 0] = 0;                      // chain 00, ligne 0 -> phrase 00
  const uint32_t ph = MD_OFF_PHRASES;
  m[ph + 0] = 49; m[ph + 1] = 1;                 // C-4, instrument 01
  m[ph + 3] = MD_VIDE; m[ph + 5] = MD_VIDE;      // pas de commande sur la ligne
  m[MD_OFF_INSTR + 59] = 0;                      // instrument 01 -> table 00
}

// Seize lignes de TSP 0,1,2,...  et un H a la ligne donnee.
static void pose_table(int ligne_h, uint8_t val_h, int colonne) {
  uint8_t *m = md_travail();
  for (int l = 0; l < MD_LIGNES_TABLE; l++) {
    const uint32_t b = MD_OFF_TABLES + (uint32_t)l * MD_TABLE_OCTETS;
    m[b + 0] = 0;                 // VOL : on n'y touche pas
    m[b + 1] = (uint8_t)(10 + l);  // TSP : 10+ligne, pour lire la ligne jouee
    m[b + 2] = MD_VIDE; m[b + 3] = 0;
    m[b + 4] = MD_VIDE; m[b + 5] = 0;
    m[b + 6] = MD_VIDE; m[b + 7] = 0;
  }
  if (ligne_h >= 0) {
    const uint32_t b = MD_OFF_TABLES + (uint32_t)ligne_h * MD_TABLE_OCTETS;
    m[b + (colonne == 1 ? 2 : 4)] = (uint8_t)rang_de('H');
    m[b + (colonne == 1 ? 3 : 5)] = val_h;
  }
}

static void deroule(const char *titre, int ticks) {
  // Sinon la premiere valeur affichee est celle que l'essai PRECEDENT avait
  // laissee sur la voie, et on la lit comme un resultat.
  for (int c = 0; c < 10; c++) derniere_hauteur[c] = 49;
  md_lecture_init();
  md_lecture_demarre(0);
  printf("  %-34s", titre);
  for (int t = 0; t < ticks; t++) {
    md_lecture_tick();
    printf(" %d", derniere_hauteur[0] - 49);   // la transposition posee
  }
  printf("\n");
  md_lecture_arrete();
}

int main(void) {
  printf("H est la commande de rang %d\n\n", rang_de('H'));
  printf("Transposition posee sur FM1, tick par tick :\n");

  pose_morceau(); pose_table(-1, 0, 1);
  deroule("sans H : deroule les 16 lignes", 20);

  pose_morceau(); pose_table(3, 0x00, 1);
  deroule("H00 en ligne 3, colonne CMD1", 20);

  pose_morceau(); pose_table(3, 0x02, 1);
  deroule("H02 en ligne 3 (2 fois -> ligne 2)", 20);

  pose_morceau(); pose_table(5, 0x21, 1);
  deroule("H21 en ligne 5 (2 tours vers 1)", 24);

  pose_morceau(); pose_table(3, 0x00, 2);
  deroule("H00 en colonne CMD2 (TSP libre)", 20);

  // ⚠️ LA LIGNE QUI PORTE LE H NE SE JOUE PAS. Le saut est resolu AVANT de
  // lire la ligne : on arrive sur le H, on repart, et sa propre valeur de TSP
  // n'est jamais posee. C'est ce qu'on veut — la boucle couvre ce qui est
  // AU-DESSUS du H, pas le H lui-meme.
  printf("\n  Chaque ligne porte TSP = 10 + son numero.\n");
  pose_morceau(); pose_table(4, 0x00, 1);
  deroule("H00 en ligne 4 -> on doit voir 10..13", 16);
  return 0;
}
