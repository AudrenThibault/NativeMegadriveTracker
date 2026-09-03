// ============================================================================
//  Le relevé du côté DS, sans DS.
//
//  md_replayer.c est du C portable : on le compile ici et on lui donne une
//  fausse couche de puce qui NOTE les écritures au lieu de les envoyer. On
//  obtient donc exactement ce que la DS envoie au YM2612, sans console, sans
//  carte à échanger, et sans toucher au projet DS — on ne fait que le lire,
//  comme on lit déjà ymfm.
//
//  ⚠️ Ce n'est PAS une dépendance du tracker Mega Drive : rien de tout ça
//  n'entre dans la ROM. C'est un instrument de mesure, au même titre que
//  Genesis-Plus-GX.
// ============================================================================
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// ── La fausse couche de puce ──────────────────────────────────────────────
static struct { int part; unsigned char reg, val; } trace[512];
static int trace_n = 0;
static int arme = 0;

void md_chip_ym_write(uint8_t part, uint8_t reg, uint8_t val) {
  if (arme && trace_n < 512) {
    trace[trace_n].part = part;
    trace[trace_n].reg = reg;
    trace[trace_n].val = val;
    trace_n++;
  }
}
// ⚠️ La DS coupe et rallume la note par une fonction À PART, alors que la
// Mega Drive écrit le registre 0x28 elle-même. On la traduit ici dans la même
// écriture, sinon les deux relevés seraient incomparables sur la forme alors
// qu'ils disent la même chose.
void md_chip_ym_key(int fm_channel, bool on) {
  const uint8_t cle = (uint8_t)((fm_channel % 3) + (fm_channel >= 3 ? 4 : 0));
  md_chip_ym_write(0, 0x28, (uint8_t)(on ? (0xF0 | cle) : cle));
}
void md_chip_reset(int hz) { (void)hz; }
void md_chip_psg_write(uint8_t d) { (void)d; }
void md_chip_psg_set_period(int c, uint16_t p) { (void)c; (void)p; }
void md_chip_psg_set_volume(int c, uint8_t a) { (void)c; (void)a; }
void md_chip_psg_set_noise(uint8_t c) { (void)c; }
void md_chip_psg_set_pan(int c, uint8_t p) { (void)c; (void)p; }
void md_chip_generate(int16_t *o, int n) { if (o) memset(o, 0, (size_t)n * 4); }
void md_chip_pcm_play(const uint8_t *d, uint32_t l, int32_t b, int r, int v) {
  (void)d; (void)l; (void)b; (void)r; (void)v;
}
void md_chip_pcm_stop(void) { }
void md_chip_pcm_enable(bool o) { (void)o; }
void md_chip_set_ladder(bool o) { (void)o; }
bool md_chip_get_ladder(void) { return true; }

#include "md_replayer.h"

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: trace_ds <morceau.mdm> <instrument> <note>\n");
    return 1;
  }
  const int ins  = atoi(argv[2]);
  const int note = atoi(argv[3]);

  FILE *g = fopen(argv[1], "rb");
  if (!g) { fprintf(stderr, "morceau introuvable : %s\n", argv[1]); return 1; }
  fseek(g, 0, SEEK_END); long taille = ftell(g); fseek(g, 0, SEEK_SET);
  unsigned char *octets = malloc((size_t)taille);
  if (fread(octets, 1, (size_t)taille, g) != (size_t)taille) return 1;
  fclose(g);
  if (!md_replayer_load_mem(octets, (uint32_t)taille)) {
    fprintf(stderr, "morceau illisible : %s\n", argv[1]);
    return 1;
  }
  md_replayer_init(53267);

  // ⚠️ On force l'instrument au réglage d'usine de la DS : c'est le SEUL
  // moyen que les deux relevés parlent du même timbre. Le morceau ne sert
  // qu'à donner un contexte au moteur.
  // ⚠️ Celle-ci compte À PARTIR DE ZÉRO, alors que les autres fonctions du
  // moteur prennent un numéro d'instrument à partir de un. Sans ce décalage
  // on remet à neuf l'instrument VOISIN, et on relève un autre timbre.
  md_replayer_init_default_instrument(ins - 1);
  if (argc > 4)
    for (int i = 4; i < argc; i++) {
      int prop, val;
      if (sscanf(argv[i], "gen%d=%d", &prop, &val) == 2)
        md_replayer_set_instr_gen_val(ins, prop, (uint8_t)val);
    }

  arme = 1;
  md_replayer_play_test_note((uint8_t)note, ins, 0);
  arme = 0;

  printf("%d ecritures\n", trace_n);
  FILE *f = fopen("trace_ds.txt", "w");
  for (int i = 0; i < trace_n; i++)
    fprintf(f, " banc%d  %02X <- %02X\n",
            trace[i].part, trace[i].reg, trace[i].val);
  fclose(f);
  return 0;
}
