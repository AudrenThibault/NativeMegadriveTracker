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

#define TICKS 34

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

  // ── LES COMMANDES QU'UNE TRACE DE QUATORZE TICKS NE MONTRE PAS ────────
  // Celles qui deplacent une position ou reglent un parametre ne s'ecrivent
  // pas sur la puce a la ligne ou on les pose. On les eprouve sur ce qu'elles
  // CHANGENT : la suite des notes jouees.
  printf("\n\nEssais cibles — la suite des notes que la puce recoit :\n\n");
  { uint8_t *m;
    // H dans une PHRASE : trois notes, puis H00. On doit les reentendre.
    pose_morceau(0); m = md_travail();
    for (int l = 0; l < 3; l++) {
      const uint32_t b = MD_OFF_PHRASES + (uint32_t)l * MD_PHRASE_OCTETS;
      m[b + 0] = (uint8_t)(49 + l); m[b + 1] = 1; m[b + 2] = MD_VIDE;
    }
    { const uint32_t b = MD_OFF_PHRASES + 3 * MD_PHRASE_OCTETS;
      m[b + 3] = (uint8_t)rang_de('H'); m[b + 4] = 0x00; }
    trace(0, avec);
    printf("  H00 en ligne 3 d'une phrase\n    %s\n\n", avec);

    // K : coupure a trois ticks.
    pose_morceau(0); m = md_travail();
    m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('K'); m[MD_OFF_PHRASES + 4] = 0x03;
    trace(0, avec);
    printf("  K03 — la note doit s'eteindre\n    %s\n\n", avec);

    // L : glissando vers la note de la ligne suivante.
    pose_morceau(0); m = md_travail();
    { const uint32_t b = MD_OFF_PHRASES + MD_PHRASE_OCTETS;
      m[b + 0] = 61; m[b + 1] = 1; m[b + 2] = MD_VIDE;
      m[b + 3] = (uint8_t)rang_de('L'); m[b + 4] = 0x20; }
    trace(0, avec);
    printf("  L20 vers une note douze demi-tons plus haut\n    %s\n\n", avec);

    // M : volume general a la moitie.
    pose_morceau(0); m = md_travail();
    m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('M'); m[MD_OFF_PHRASES + 4] = 0x08;
    trace(0, avec);
    printf("  M08 — le volume doit tomber\n    %s\n\n", avec);

    // W puis V : la profondeur vient de W.
    pose_morceau(0); m = md_travail();
    m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('W'); m[MD_OFF_PHRASES + 4] = 0x02;
    { const uint32_t b = MD_OFF_PHRASES + MD_PHRASE_OCTETS;
      m[b + 3] = (uint8_t)rang_de('V'); m[b + 4] = 0x40; }
    trace(0, avec);
    printf("  W02 puis V40 — vibrato de profondeur 2, pas 0\n    %s\n\n", avec);

    // N : rupture, on doit passer a la ligne de SONG suivante.
    pose_morceau(0); m = md_travail();
    m[MD_OFF_SONG + 1] = 1;                       // ligne 1 -> chain 01
    m[MD_OFF_CHAINS + 1 * MD_LIGNES_CHAIN * 2] = 1;   // chain 01 -> phrase 01
    { const uint32_t b = MD_OFF_PHRASES + (uint32_t)MD_LIGNES_PHRASE * MD_PHRASE_OCTETS;
      m[b + 0] = 80; m[b + 1] = 1; m[b + 2] = MD_VIDE; }
    m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('N'); m[MD_OFF_PHRASES + 4] = 0x00;
    trace(0, avec);
    printf("  N00 — la note 80 de la phrase suivante doit arriver\n    %s\n", avec);
  }
  return 0;
}
