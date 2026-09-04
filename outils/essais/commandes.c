// L'INVENTAIRE DES COMMANDES. Pour chaque lettre, on pose une note et cette
// commande sur une ligne de phrase, on fait tourner le VRAI lecteur, et on
// regarde ce que les puces recoivent. Une commande qui n'agit pas n'ecrit
// rien de plus que la note seule : la comparaison est automatique, personne
// n'a besoin d'ecouter.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "md_song.h"
#include "md_lecture.h"
#include "md_commandes.h"

extern char journal[512];
extern void journal_vide(void);
extern void bouchons_remet(void);

#define TICKS 14

static void pose_morceau(uint8_t voie) {
  uint8_t *m = md_travail();
  memset(m, 0, MD_TAILLE_TOTALE);
  for (uint32_t o = MD_OFF_SONG; o < MD_OFF_SONG + MD_TAILLE_SONG; o++) m[o] = MD_VIDE;
  for (uint32_t o = MD_OFF_CHAINS; o < MD_OFF_CHAINS + MD_TAILLE_CHAINS; o += 2) m[o] = MD_VIDE;
  for (uint32_t o = MD_OFF_PHRASES; o < MD_OFF_PHRASES + MD_TAILLE_PHRASES;
       o += MD_PHRASE_OCTETS) { m[o + 3] = MD_VIDE; m[o + 5] = MD_VIDE; }
  m[MD_OFF_SONG + (uint32_t)voie * MD_SONG_LIGNES] = 0;
  m[MD_OFF_CHAINS] = 0;
  const uint32_t ph = MD_OFF_PHRASES;
  m[ph + 0] = 49; m[ph + 1] = 1; m[ph + 2] = MD_VIDE;   // C-4, instrument 01
  m[MD_OFF_INSTR + 59] = MD_VIDE;                        // AUCUNE table
  m[MD_OFF_INSTR + 60] = 1;                              // genre FM
}

// Rend une empreinte de ce que les puces ont recu sur `ticks` ticks.
static void trace(uint8_t voie, char sortie[2048]) {
  sortie[0] = 0;
  bouchons_remet();
  md_lecture_init();
  md_lecture_demarre(0);
  for (int t = 0; t < TICKS; t++) {
    journal_vide();
    md_lecture_tick();
    strncat(sortie, journal[0] ? journal : ".", 2047 - strlen(sortie));
    strncat(sortie, " | ", 2047 - strlen(sortie));
  }
  md_lecture_arrete();
}

static int rang_de(char lettre) {
  for (int r = 0; r < MD_CMD_NOMBRE; r++) if (md_cmd_lettre(r) == lettre) return r;
  return -1;
}

int main(void) {
  char temoin[2048], avec[2048];
  pose_morceau(0);
  trace(0, temoin);
  printf("Temoin, note seule sur FM1 :\n  %s\n\n", temoin);

  printf("%-4s %-20s %s\n", "CMD", "NOM", "EFFET");
  printf("---- -------------------- --------------------------------------\n");
  for (int r = 0; r < MD_CMD_NOMBRE; r++) {
    const char le = md_cmd_lettre(r);
    // Une valeur qui a du sens pour la plupart : deux quartets non nuls.
    pose_morceau(0);
    md_travail()[MD_OFF_PHRASES + 3] = (uint8_t)r;
    md_travail()[MD_OFF_PHRASES + 4] = 0x24;
    trace(0, avec);
    printf("%-4c %-20s %s\n", le, md_cmd_nom(r), avec);
  }

  printf("\nLes MD CMD (colonne MD), memes conditions :\n");
  printf("%-6s %-24s %s\n", "CODE", "NOM", "EFFET");
  printf("------ ------------------------ ------------------------------\n");
  for (int r = 0; r < MD_MDCMD_NOMBRE; r++) {
    pose_morceau(0);
    md_travail()[MD_OFF_PHRASES + 5] = (uint8_t)r;
    md_travail()[MD_OFF_PHRASES + 6] = 0x24;
    trace(0, avec);
    printf("%02X     %-24s %s\n", md_mdcmd_code(r), md_mdcmd_nom(r), avec);
  }
  return 0;
}
