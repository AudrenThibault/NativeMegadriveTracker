// Les puces, en trompe-l'oeil. Le lecteur ne doit rien savoir de leur absence :
// on veut eprouver SA logique de tables, pas le YM2612.
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "md_song.h"

static uint8_t travail_h[MD_TAILLE_TOTALE];
uint8_t *md_travail(void)                { return travail_h; }
uint8_t  md_lit(uint32_t o)              { return travail_h[o]; }
void     md_ecrit(uint32_t o, uint8_t v) { travail_h[o] = v; }

uint8_t md_song_lit(int c, int l) { return travail_h[MD_OFF_SONG + (uint32_t)c * MD_SONG_LIGNES + l]; }
void md_chain_lit(int id, int l, uint8_t *ph, int8_t *tsp) {
  const uint32_t b = MD_OFF_CHAINS + ((uint32_t)id * MD_LIGNES_CHAIN + l) * 2;
  if (ph)  *ph  = travail_h[b];
  if (tsp) *tsp = (int8_t)travail_h[b + 1];
}
void md_phrase_lit(int id, int l, md_ligne_phrase *r) {
  const uint32_t b = MD_OFF_PHRASES + ((uint32_t)id * MD_LIGNES_PHRASE + l) * MD_PHRASE_OCTETS;
  r->note = travail_h[b]; r->instr = travail_h[b+1]; r->vel = travail_h[b+2];
  r->cmd = travail_h[b+3]; r->val = travail_h[b+4];
  r->mdcmd = travail_h[b+5]; r->mdval = travail_h[b+6];
}
static int bpm_h = 120, vit_h = 6;
// ⚠️ A REMETTRE ENTRE DEUX ESSAIS. S et T changent le tempo POUR DE BON ; sans
// remise a zero, l'essai suivant tourne a la vitesse que le precedent a
// laissee et on lit ce decalage comme un effet de la commande.
void bouchons_remet(void) { bpm_h = 120; vit_h = 6; }
uint16_t md_song_bpm(void)        { return (uint16_t)bpm_h; }
void md_song_pose_bpm(int v)      { bpm_h = v; }
uint8_t md_song_vitesse(void)     { return (uint8_t)vit_h; }
void md_song_pose_vitesse(uint8_t v) { vit_h = v; }

// ── TOUT CE QUE LES PUCES ONT ENTENDU ─────────────────────────────────────
// Chaque bouchon inscrit son appel dans un journal, remis a zero a chaque
// tick. C'est ce journal qui dit si une commande AGIT : une commande qui ne
// fait rien n'ecrit rien, et aucune oreille n'est necessaire pour le voir.
#include <stdio.h>
char journal[512];
static int jl;
static void note_appel(const char *forme, ...) {
  va_list a; va_start(a, forme);
  if (jl && jl < (int)sizeof journal - 1) journal[jl++] = ' ';
  jl += vsnprintf(journal + jl, sizeof journal - (size_t)jl, forme, a);
  if (jl > (int)sizeof journal - 1) jl = (int)sizeof journal - 1;
  va_end(a);
}
void journal_vide(void) { jl = 0; journal[0] = 0; }

int derniere_hauteur[10];
int dernier_niveau[10];
void md_fm_hauteur(int c, int n, int f) {
  derniere_hauteur[c] = n; note_appel("PITCH%d=%d%+d", c, n, f);
}
void md_fm_frequence(int c, uint8_t n) {
  derniere_hauteur[c] = n; note_appel("FREQ%d=%d", c, n);
}
void md_psg_hauteur(int c, int n, int f) {
  derniere_hauteur[c + 6] = n; note_appel("PSGPITCH%d=%d%+d", c, n, f);
}
void md_psg_note_on(int c, uint8_t n, uint8_t v) {
  derniere_hauteur[c+6] = n; dernier_niveau[c+6] = v;
  note_appel("PSGON%d=%d/v%d", c, n, v);
}
void md_psg_bruit(uint8_t g, uint8_t n, uint8_t v) {
  derniere_hauteur[9] = n; dernier_niveau[9] = v;
  note_appel("BRUIT g%d n%d v%d", g, n, v);
}
void md_fm_note_on(int c, uint8_t n) {
  derniere_hauteur[c] = n; note_appel("FMON%d=%d", c, n);
}
void md_fm_charge(int c, uint32_t o) { note_appel("CHARGE%d", c); }
void md_fm_note_off(int c) { note_appel("FMOFF%d", c); }
void md_fm_pose_alg_fb(int c, uint8_t a, uint8_t b) { note_appel("ALGFB%d=%d/%d", c, a, b); }
void md_fm_pose_ar(int c, int o, uint8_t v) { note_appel("AR%d.%d=%d", c, o, v); }
void md_fm_pose_lfo(int a, uint8_t b) { note_appel("LFO=%d/%d", a, b); }
void md_fm_pose_mul(int c, int o, uint8_t v) { note_appel("MUL%d.%d=%d", c, o, v); }
void md_fm_pose_pan(int c, uint8_t v, uint8_t a, uint8_t p) { note_appel("PAN%d=%d", c, v); }
void md_fm_pose_tl(int c, int o, uint8_t v) { note_appel("TL%d.%d=%d", c, o, v); }
void md_psg_note_off(int c) { note_appel("PSGOFF%d", c); }
void md_puces_init(void) {}
void md_puces_silence(void) {}
void md_z80_prepare(void) {}
int  md_pcm_actif(void) { return 0; }
void md_pcm_anneau_pose(const uint8_t *e) {}
void md_pcm_arrete(void) { note_appel("PCMSTOP"); }
uint32_t md_pcm_bilan(void) { return 0; }
uint8_t md_pcm_etat(void) { return 0; }
void md_pcm_joue(uint32_t a, uint32_t b, int c, int d) { note_appel("PCM %u/%d/%d", (unsigned)b, c, d); }
const uint8_t  pcm_banque[1] = {0};
const uint32_t pcm_offset[32] = {0};
const uint32_t pcm_longueur[32] = {0};
const uint8_t  pcm_note[32] = {0};

void sonde(const char *f, int a, int b, int c) {
  note_appel("[%s v%d cmd%d note%d]", f, a, b, c);
}
