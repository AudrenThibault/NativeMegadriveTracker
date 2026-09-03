// ============================================================================
//  Comparateur de timbre FM.
//
//  LA QUESTION QU'IL TRANCHE : quand le même instrument sonne autrement sur la
//  DS et sur la Mega Drive, est-ce parce qu'on n'envoie pas les mêmes octets,
//  ou parce que les deux modèles de puce n'en font pas la même chose ?
//
//  Lire le code ne répond plus : j'ai vérifié registre par registre, les deux
//  projets écrivent la même chose. La différence est donc dans le RÉSULTAT, et
//  pour la voir il faut faire jouer la même suite d'écritures à deux moteurs.
//
//  ⚠️ La suite d'écritures n'est pas RECOPIÉE ici, elle est RELEVÉE : on
//  compile md_puces.c tel quel, avec MD_HORS_CONSOLE, et on intercepte
//  md_ym_ecrit. Ce qui sort est donc exactement ce que la console envoie —
//  aucune place pour une erreur de transcription.
//
//  Il produit :
//    trace.txt   la suite d'écritures, lisible
//    ymfm.wav    ce que le modèle de la DS en fait
//    trace.h     la même suite, en table C, pour la ROM de rejeu
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "ymfm_opn.h"

extern "C" {
  void md_fm_charge(int voie, uint32_t base);
  void md_fm_note_on(int voie, uint8_t note);
  void md_puces_pose_region(int est_pal);
  uint8_t md_lit(uint32_t o);
}

// ── Le morceau, en forme compacte ─────────────────────────────────────────
static std::vector<uint8_t> morceau;
extern "C" uint8_t md_lit(uint32_t o) {
  return (o < morceau.size()) ? morceau[o] : 0;
}
extern "C" void md_ecrit(uint32_t o, uint8_t v) {
  if (o < morceau.size()) morceau[o] = v;
}

// ── Le relevé ─────────────────────────────────────────────────────────────
struct Ecriture { int banc; uint8_t reg, val; };
static std::vector<Ecriture> trace;
extern "C" void md_ym_ecrit(int banc, uint8_t reg, uint8_t valeur) {
  trace.push_back({banc, reg, valeur});
}

// ── ymfm : le modèle de puce de la DS ─────────────────────────────────────
class Interface : public ymfm::ymfm_interface { };

static void ecris_wav(const char *nom, const std::vector<int16_t> &g,
                      const std::vector<int16_t> &d, uint32_t hz) {
  FILE *f = fopen(nom, "wb");
  const uint32_t n = (uint32_t)g.size();
  const uint32_t octets = n * 4;
  fwrite("RIFF", 1, 4, f);
  uint32_t v = 36 + octets; fwrite(&v, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f);
  uint16_t w = 1; fwrite(&w, 2, 1, f);
  w = 2; fwrite(&w, 2, 1, f);
  fwrite(&hz, 4, 1, f);
  v = hz * 4; fwrite(&v, 4, 1, f);
  w = 4; fwrite(&w, 2, 1, f);
  w = 16; fwrite(&w, 2, 1, f);
  fwrite("data", 1, 4, f);
  fwrite(&octets, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) { fwrite(&g[i], 2, 1, f); fwrite(&d[i], 2, 1, f); }
  fclose(f);
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr,
      "usage: comparateur <morceau.compact> <instrument> <note> [pal] [duree_ms]\n"
      "  instrument : 1 a 32   note : 1 a 108 (49 = C-4)\n");
    return 1;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "morceau introuvable\n"); return 1; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  morceau.resize((size_t)n);
  if (fread(morceau.data(), 1, (size_t)n, f) != (size_t)n) return 1;
  fclose(f);

  const int ins  = atoi(argv[2]);
  const int note = atoi(argv[3]);
  const int pal  = (argc > 4) ? atoi(argv[4]) : 1;
  const int duree_ms = (argc > 5) ? atoi(argv[5]) : 400;
  // ⚠️ Le DIVISEUR : 144 est la cadence native de la puce, 288 celle que la
  // DS lui impose pour moitié moins de calcul. On l'obtient en divisant
  // l'horloge par deux — ymfm en tire tout le reste. C'est ce qui permet de
  // comparer les deux machines sur le même flux d'écritures.
  const int diviseur = (argc > 6) ? atoi(argv[6]) : 144;
  // Une trace toute faite, au lieu de la relever : c'est ainsi qu'on rejoue
  // celle de la DS, produite par trace_ds.
  const char *trace_donnee = (argc > 7) ? argv[7] : 0;
  md_puces_pose_region(pal);

  // ⚠️ Les offsets viennent de md_song.h : instrument 1 au début de la zone.
  const uint32_t OFF_INSTR = 23616, INSTR_OCTETS = 80;
  const uint32_t base = OFF_INSTR + (uint32_t)(ins - 1) * INSTR_OCTETS;

  // La voie 0 : une voie FM ordinaire, banc 0. C'est md_puces.c qui décide de
  // tout le reste, et c'est le but.
  size_t apres_voix;
  if (trace_donnee) {
    FILE *td = fopen(trace_donnee, "r");
    if (!td) { fprintf(stderr, "trace introuvable\n"); return 1; }
    char ligne[128];
    while (fgets(ligne, sizeof ligne, td)) {
      int b; unsigned r, v;
      if (sscanf(ligne, " banc%d %x <- %x", &b, &r, &v) == 3)
        trace.push_back({b, (uint8_t)r, (uint8_t)v});
    }
    fclose(td);
    apres_voix = trace.size();
  } else {
    md_fm_charge(0, base);
    apres_voix = trace.size();
    md_fm_note_on(0, (uint8_t)note);
  }

  FILE *t = fopen("trace.txt", "w");
  fprintf(t, "instrument %02X  note %d  %s\n", ins, note, pal ? "PAL" : "NTSC");
  for (size_t i = 0; i < trace.size(); i++)
    fprintf(t, "%s banc%d  %02X <- %02X\n",
            i == apres_voix ? "\n-- note on --\n" : "",
            trace[i].banc, trace[i].reg, trace[i].val);
  fclose(t);

  FILE *h = fopen("trace.h", "w");
  fprintf(h, "// Releve par outils/comparateur — ne pas editer.\n");
  fprintf(h, "#define TRACE_N %zu\n", trace.size());
  fprintf(h, "static const unsigned char TRACE[][3] = {\n");
  for (auto &e : trace)
    fprintf(h, "  {%d,0x%02X,0x%02X},\n", e.banc, e.reg, e.val);
  fprintf(h, "};\n");
  fclose(h);

  // ── On rejoue dans ymfm ────────────────────────────────────────────────
  // L'horloge est celle de la console : 7 600 489 Hz en 50 Hz, 7 670 454 en
  // 60 Hz. Le diviseur interne donne la cadence de sortie.
  Interface intf;
  ymfm::ym2612 puce(intf);
  puce.reset();
  const uint32_t horloge = (pal ? 7600489u : 7670454u) / (uint32_t)(diviseur / 144);
  const uint32_t hz = puce.sample_rate(horloge);

  auto envoie = [&](const Ecriture &e) {
    if (e.banc) { puce.write_address_hi(e.reg); puce.write_data_hi(e.val); }
    else        { puce.write_address(e.reg);    puce.write_data(e.val); }
  };
  // Les registres globaux que md_puces_init pose au démarrage : sans eux le
  // LFO garderait l'état d'un essai précédent.
  puce.write_address(0x22); puce.write_data(0x00);
  puce.write_address(0x27); puce.write_data(0x00);
  puce.write_address(0x2B); puce.write_data(0x00);
  for (auto &e : trace) envoie(e);

  const uint32_t total = hz * (uint32_t)duree_ms / 1000u;
  std::vector<int16_t> g(total), d(total);
  ymfm::ym2612::output_data s;
  for (uint32_t i = 0; i < total; i++) {
    puce.generate(&s, 1);
    int32_t a = s.data[0], b = s.data[1];
    if (a > 32767) a = 32767; if (a < -32768) a = -32768;
    if (b > 32767) b = 32767; if (b < -32768) b = -32768;
    g[i] = (int16_t)a; d[i] = (int16_t)b;
  }
  ecris_wav("ymfm.wav", g, d, hz);

  printf("%zu ecritures relevees (%zu pour la voix, %zu pour la note)\n",
         trace.size(), apres_voix, trace.size() - apres_voix);
  printf("  trace.txt  trace.h  ymfm.wav (%u Hz, %d ms)\n", hz, duree_ms);
  return 0;
}
