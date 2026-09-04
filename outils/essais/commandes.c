// L'INVENTAIRE DES COMMANDES. Pour chaque lettre, on pose une note et cette
// commande sur une ligne de phrase, on fait tourner le VRAI lecteur, et on
// regarde ce que les puces recoivent. Une commande qui n'agit pas n'ecrit
// rien de plus que la note seule : la comparaison est automatique, personne
// n'a besoin d'ecouter.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

// Une valeur qui a du SENS pour chaque commande. Un 0x24 uniforme ne prouvait
// rien : il place un A hors table, un K au-dela de la ligne, un M a 4...
// ⚠️ LA VALEUR VIENT DE L'EFFET, pas de la lettre : une lettre et un code MD
// qui font la meme chose doivent etre eprouves de la meme facon. Un 0x24
// uniforme ne prouvait rien — il coupait au-dela de la ligne, sautait a une
// ligne de SONG vide, choisissait une table inexistante.
static uint8_t valeur_pour(int e) {
  switch (e) {
    case MD_E_ARPEGE:     return 0x37;
    case MD_E_PORTA_HAUT:
    case MD_E_PORTA_BAS:  return 0x08;
    case MD_E_PORTA_TON:  return 0x20;
    case MD_E_VIBRATO:    return 0x44;
    case MD_E_PORTA_VOL:  return 0x21;
    case MD_E_VIB_VOL:    return 0x42;
    case MD_E_TREMOLO:    return 0x44;
    case MD_E_VOL_SLIDE:  return 0x02;
    case MD_E_RETRIG:     return 0x02;
    case MD_E_SAUT:       return 0x00;
    case MD_E_RUPTURE:    return 0x00;
    case MD_E_VIB_PROF:   return 0x03;
    case MD_E_FINE:       return 0x0C;
    case MD_E_COUPE:      return 0x03;
    case MD_E_RETARD:     return 0x03;
    case MD_E_TEMPO:      return 0x64;
    case MD_E_VITESSE:    return 0x04;
    case MD_E_VOL_GLOBAL: return 0x08;
    case MD_E_PAN:        return 0x01;
    case MD_E_TABLE:      return 0x01;   // l'AUTRE table, pas celle-ci
    case MD_E_PITCH:      return 0x51;
    case MD_E_HOP:        return 0x00;
    default:              return 0x24;
  }
}

// Pourquoi une case reste vide sans que ce soit un defaut.
static const char *excuse(int e, int dans_table) {
  // ⚠️ Ces deux-la ne peuvent RIEN faire dans les conditions de la matrice,
  // et ce n'est pas un defaut : un portamento pose sur la PREMIERE note n'a
  // pas de cible, et un changement de vitesse ne se voit qu'avec une seconde
  // note. Les essais cibles plus bas les prouvent chacun.
  if (e == MD_E_TEMPO || e == MD_E_VITESSE) return "reglage";
  if (e == MD_E_VIB_PROF) return "reglage";
  if (e == MD_E_PORTA_TON) return "sans cible";
  if (!dans_table) return 0;
  if (e == MD_E_RETARD)  return "n/a";     // une table n'a pas de ligne
  if (e == MD_E_HOP)     return "hop";     // hop_resout s'en charge
  if (e == MD_E_VIB_PROF) return "reglage";
  if (e == MD_E_PORTA_TON) return "sans cible";
  return 0;
}

// Pose une table NEUTRE sur l'instrument 01 : seize lignes sans rien.
static void pose_table_neutre(void) {
  uint8_t *m = md_travail();
  m[MD_OFF_INSTR + 59] = 0;
  // La table 01 porte une transposition : sans ça, « choisir la table 01 »
  // ne se distinguerait pas de rester sur la 00.
  for (int l = 0; l < MD_LIGNES_TABLE; l++) {
    const uint32_t b = MD_OFF_TABLES
                     + ((uint32_t)MD_LIGNES_TABLE + (uint32_t)l) * MD_TABLE_OCTETS;
    m[b + 0] = 0; m[b + 1] = 7;
    m[b + 2] = MD_VIDE; m[b + 4] = MD_VIDE; m[b + 6] = MD_VIDE;
  }
  for (int l = 0; l < MD_LIGNES_TABLE; l++) {
    const uint32_t b = MD_OFF_TABLES + (uint32_t)l * MD_TABLE_OCTETS;
    m[b + 0] = 0; m[b + 1] = 0;
    m[b + 2] = MD_VIDE; m[b + 3] = 0;
    m[b + 4] = MD_VIDE; m[b + 5] = 0;
    m[b + 6] = MD_VIDE; m[b + 7] = 0;
  }
}

int main(void) {
  char temoin[2048], temoinT[2048], avec[2048];

  pose_morceau(0);                trace(0, temoin);
  pose_morceau(0); pose_table_neutre(); trace(0, temoinT);

  printf("TOUTES LES COMMANDES, DANS LES CINQ COLONNES\n");
  printf("(agit = la puce recoit autre chose que sans la commande)\n\n");
  printf("%-4s %-20s %-10s %-10s %-10s\n",
         "CMD", "NOM", "PHRASE", "TABLE C1", "TABLE C2");
  printf("---- -------------------- ---------- ---------- ----------\n");
  int manquantes = 0;
  for (int r = 0; r < MD_CMD_NOMBRE; r++) {
    const char le = md_cmd_lettre(r);
    const int e = md_cmd_effet(r);
    const uint8_t val = valeur_pour(e);
    int ok[3];

    pose_morceau(0);
    md_travail()[MD_OFF_PHRASES + 3] = (uint8_t)r;
    md_travail()[MD_OFF_PHRASES + 4] = val;
    trace(0, avec);
    ok[0] = strcmp(avec, temoin) != 0;

    for (int col = 0; col < 2; col++) {
      pose_morceau(0); pose_table_neutre();
      { const uint32_t b = MD_OFF_TABLES + 3 * MD_TABLE_OCTETS;
        md_travail()[b + (col ? 4 : 2)] = (uint8_t)r;
        md_travail()[b + (col ? 5 : 3)] = val; }
      trace(0, avec);
      ok[1 + col] = strcmp(avec, temoinT) != 0;
    }
    const char *xp = excuse(e, 0), *xt = excuse(e, 1);
    for (int k = 0; k < 3; k++)
      if (!ok[k] && !(k == 0 ? xp : xt)) manquantes++;
    printf("%-4c %-20s %-10s %-10s %-10s\n", le, md_cmd_nom(r),
           ok[0] ? "agit" : (xp ? xp : "RIEN"),
           ok[1] ? "agit" : (xt ? xt : "RIEN"),
           ok[2] ? "agit" : (xt ? xt : "RIEN"));
  }

  printf("\n%-6s %-24s %-10s %-10s\n", "MDCMD", "NOM", "PHRASE", "TABLE MD");
  printf("------ ------------------------ ---------- ----------\n");
  for (int r = 0; r < MD_MDCMD_NOMBRE; r++) {
    int ok[2];
    pose_morceau(0);
    const int e = md_mdcmd_effet(r);
    const uint8_t val = (e == MD_E_RIEN) ? 0x24 : valeur_pour(e);
    md_travail()[MD_OFF_PHRASES + 5] = (uint8_t)r;
    md_travail()[MD_OFF_PHRASES + 6] = val;
    trace(0, avec);
    ok[0] = strcmp(avec, temoin) != 0;

    pose_morceau(0); pose_table_neutre();
    { const uint32_t b = MD_OFF_TABLES + 3 * MD_TABLE_OCTETS;
      md_travail()[b + 6] = (uint8_t)r; md_travail()[b + 7] = val; }
    trace(0, avec);
    ok[1] = strcmp(avec, temoinT) != 0;
    const char *xp = excuse(e, 0), *xt = excuse(e, 1);
    for (int k = 0; k < 2; k++)
      if (!ok[k] && !(k == 0 ? xp : xt)) manquantes++;
    printf("%02X     %-24s %-10s %-10s\n", md_mdcmd_code(r), md_mdcmd_nom(r),
           ok[0] ? "agit" : (xp ? xp : "RIEN"),
           ok[1] ? "agit" : (xt ? xt : "RIEN"));
  }
  printf("\n%d cases sans effet.\n", manquantes);

  // ── LES « REGLAGE » PROUVES A PART ────────────────────────────────────
  // Elles ne parlent pas a la puce : elles changent la CADENCE ou un
  // parametre. On les eprouve sur ce qu'elles deplacent.
  printf("\nLes reglages, prouves sur ce qu'ils changent :\n");
  { uint8_t *m;
    for (int essai = 0; essai < 3; essai++) {
      pose_morceau(0); m = md_travail();
      for (int l = 0; l < 2; l++) {
        const uint32_t b = MD_OFF_PHRASES + (uint32_t)l * MD_PHRASE_OCTETS;
        m[b + 0] = (uint8_t)(49 + l); m[b + 1] = 1; m[b + 2] = MD_VIDE;
      }
      if (essai == 1) { m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('S');
                        m[MD_OFF_PHRASES + 4] = 0x03; }
      if (essai == 2) { m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('T');
                        m[MD_OFF_PHRASES + 4] = 0xF0; }
      trace(0, avec);
      int quand = -1, n = 0;
      for (const char *q = avec; *q; q++)
        if (*q == '|') { n++; }
        else if (q[0] == 'F' && q[1] == 'M' && q[2] == 'O' && q[3] == 'N'
                 && n > 2 && quand < 0) quand = n;
      printf("  %-24s deuxieme note au tick %d\n",
             essai == 0 ? "sans rien" : essai == 1 ? "S03 (3 ticks/ligne)"
                                                   : "TF0 (240 BPM)", quand);
    }
    // ⚠️ UN EFFET CONTINU DOIT SURVIVRE A LA LIGNE. Un arpege pose en MD CMD
    // sur la ligne 00 doit tourner encore aux lignes suivantes.
    { pose_morceau(0); m = md_travail();
      m[MD_OFF_PHRASES + 5] = 0x00;      // MD CMD 00 = arpege
      m[MD_OFF_PHRASES + 6] = 0x0C;
      trace(0, avec);
      int tours = 0;
      for (const char *q = avec; (q = strstr(q, "PITCH0=61")); q++) tours++;
      printf("  MD CMD 00/0C sur la ligne 00 : %d retours a +12 sur %d ticks%s\n",
             tours, TICKS, tours > 3 ? " — il dure" : " — IL S'ARRETE");
    }

    // ⚠️ L NE PEUT GLISSER QUE VERS UNE CIBLE. Pose sur la premiere note, il
    // n'a nulle part ou aller — ce n'est pas un defaut. On l'eprouve donc
    // comme on l'ecrit : une note, puis une autre PLUS HAUT portant le L.
    { pose_morceau(0); m = md_travail();
      const uint32_t b = MD_OFF_PHRASES + MD_PHRASE_OCTETS;
      m[b + 0] = 61; m[b + 1] = 1; m[b + 2] = MD_VIDE;
      m[b + 3] = (uint8_t)rang_de('L'); m[b + 4] = 0x20;
      trace(0, avec);
      printf("  L20 vers la note du dessus  %s\n",
             strstr(avec, "PITCH0=49+64") ? "glisse sans reattaquer : oui"
                                          : "NE GLISSE PAS");
    }

    // S depuis une TABLE : il faut deux notes pour voir la ligne raccourcir.
    { for (int essai = 0; essai < 2; essai++) {
        pose_morceau(0); m = md_travail();
        pose_table_neutre();
        for (int l = 0; l < 2; l++) {
          const uint32_t b2 = MD_OFF_PHRASES + (uint32_t)l * MD_PHRASE_OCTETS;
          m[b2 + 0] = (uint8_t)(49 + l); m[b2 + 1] = 1; m[b2 + 2] = MD_VIDE;
        }
        if (essai) { const uint32_t b3 = MD_OFF_TABLES + MD_TABLE_OCTETS;
                     m[b3 + 2] = (uint8_t)rang_de('S'); m[b3 + 3] = 0x03; }
        trace(0, avec);
        int quand = -1, n = 0;
        for (const char *q = avec; *q; q++)
          if (*q == '|') n++;
          else if (q[0] == 'F' && q[1] == 'M' && q[2] == 'O' && q[3] == 'N'
                   && n > 2 && quand < 0) quand = n;
        printf("  %-26s deuxieme note au tick %d\n",
               essai ? "S03 dans la TABLE" : "table neutre", quand);
      }
    }

    // La VITESSE du pitch bend, en demi-tons par tick.
    { pose_morceau(0); m = md_travail();
      m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('P'); m[MD_OFF_PHRASES + 4] = 0x50;
      trace(0, avec);
      const char *q = strstr(avec, "PITCH0=49+");
      int a1 = q ? atoi(q + 10) : 0;
      const char *q2 = q ? strstr(q + 1, "PITCH0=49+") : 0;
      int a2 = q2 ? atoi(q2 + 10) : 0;
      printf("  P50 : %d puis %d 256e de demi-ton — %d.%02d demi-ton par tick\n",
             a1, a2, (a2 - a1) / 256, (((a2 - a1) % 256) * 100) / 256);
    }

    // W seul ne s'entend pas ; W puis V, si.
    pose_morceau(0); m = md_travail();
    m[MD_OFF_PHRASES + 3] = (uint8_t)rang_de('W'); m[MD_OFF_PHRASES + 4] = 0x02;
    { const uint32_t b = MD_OFF_PHRASES + MD_PHRASE_OCTETS;
      m[b + 3] = (uint8_t)rang_de('V'); m[b + 4] = 0x40; }
    trace(0, avec);
    printf("  W02 puis V40             %s\n",
           strstr(avec, "PITCH0=49+32") ? "vibrato de profondeur 2 : oui"
                                        : "PAS DE VIBRATO");
  }
  return 0;
}
