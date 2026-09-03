// ============================================================================
//  Banc d'essai : fait tourner une ROM Mega Drive dans Genesis Plus GX, sans
//  écran, et rend l'image en PPM.
//
//  Écrit parce que « il ne se passe rien à l'écran » n'est pas un diagnostic :
//  il faut VOIR ce que la ROM produit pour savoir où elle échoue. Le cœur
//  libretro est déjà sur la machine (RetroArch) ; on ne fait que l'appeler.
//
//  Ce fichier ne part PAS dans le produit vendu : c'est un outil de mise au
//  point qui tourne sur le Mac. Le cœur reste chez RetroArch, on ne le
//  redistribue pas.
// ============================================================================
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV_GET_CAN_DUPE        3
#define ENV_GET_SYSTEM_DIR      9
#define ENV_SET_PIXEL_FORMAT    10
#define ENV_GET_VARIABLE        15
#define ENV_GET_LOG_INTERFACE   27
#define ENV_GET_SAVE_DIR        31

struct retro_game_info {
  const char *path; const void *data; size_t size; const char *meta;
};
struct retro_game_geometry { unsigned bw, bh, mw, mh; float ratio; };
struct retro_system_timing { double fps, sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geo; struct retro_system_timing tim; };

// ── Capture du son ─────────────────────────────────────────────────────────
// Vérifier une note en regardant une image n'a aucun sens. On accumule ce que
// le cœur produit et on l'écrit en WAV : ensuite on MESURE — présence de
// signal, fréquence, durée — au lieu d'écouter et de conclure au jugé.
#define SON_MAX (48000 * 2 * 60)
static int16_t *son;
static size_t son_n;

static unsigned fmt = 1;             // 0=0RGB1555, 1=XRGB8888, 2=RGB565
static const void *img; static unsigned img_l, img_h; static size_t img_pas;
static char rep[1024];

// ── Injection de boutons ───────────────────────────────────────────────────
// Sans elle, on ne peut vérifier qu'un écran figé. Avec, on éprouve le
// déplacement du curseur et l'édition sans toucher à la console — ce qui,
// vu le prix d'un aller-retour, change tout.
//
// Genesis Plus GX place les boutons Mega Drive ainsi :
//   MD A -> Y(1)   MD B -> B(0)   MD C -> A(8)   START -> START(3)
#define N_PRESSIONS 256
static struct { int image, id, duree; } pressions[N_PRESSIONS];
static int n_pressions;
static int image_courante;

static int id_bouton(const char *n) {
  if (!strcmp(n, "UP"))    return 4;
  if (!strcmp(n, "DOWN"))  return 5;
  if (!strcmp(n, "LEFT"))  return 6;
  if (!strcmp(n, "RIGHT")) return 7;
  if (!strcmp(n, "A"))     return 1;   // MD A
  if (!strcmp(n, "B"))     return 0;   // MD B
  if (!strcmp(n, "C"))     return 8;   // MD C
  if (!strcmp(n, "START")) return 3;
  return -1;
}

// Script : "60:DOWN,60:DOWN*30,120:C" — image, bouton, durée en images (4 par
// défaut, assez pour qu'un programme cadencé sur l'image le voie).
static void lit_script(const char *s) {
  char t[4096]; strncpy(t, s, sizeof t - 1); t[sizeof t - 1] = 0;
  for (char *p = strtok(t, ","); p && n_pressions < N_PRESSIONS; p = strtok(NULL, ",")) {
    int img = 0, duree = 4; char nom[32] = {0};
    char *deuxpoints = strchr(p, ':');
    if (!deuxpoints) continue;
    *deuxpoints = 0; img = atoi(p);
    char *etoile = strchr(deuxpoints + 1, '*');
    if (etoile) { *etoile = 0; duree = atoi(etoile + 1); }
    strncpy(nom, deuxpoints + 1, sizeof nom - 1);
    const int id = id_bouton(nom);
    if (id < 0) { fprintf(stderr, "bouton inconnu: %s\n", nom); continue; }
    pressions[n_pressions].image = img;
    pressions[n_pressions].id = id;
    pressions[n_pressions].duree = duree;
    n_pressions++;
  }
}

static void journal(unsigned niveau, const char *f, ...) { (void)niveau; (void)f; }

struct retro_variable { const char *key; const char *value; };

static int env(unsigned cmd, void *d) {
  switch (cmd) {
    case ENV_SET_PIXEL_FORMAT: fmt = *(unsigned *)d; return 1;
    case ENV_GET_CAN_DUPE:     *(int *)d = 1; return 1;
    case ENV_GET_SYSTEM_DIR:
    case ENV_GET_SAVE_DIR:     *(const char **)d = rep; return 1;
    case ENV_GET_LOG_INTERFACE: *(void **)d = (void *)journal; return 1;
    // Forcer la région du cœur. Sans ça il démarre en 60 Hz, et une correction
    // de tempo qui ne se voit qu'en 50 Hz reste invérifiable ici.
    // ⚠️ Pour une option qu'on ne fournit PAS, il faut poser value à NULL avant
    // de rendre la main. Le laisser tel quel laisse le cœur lire ce qui traînait
    // là — et Genesis Plus GX perdait alors tout son son, quelle que soit la
    // valeur demandée. Le symptôme accusait la région ; la cause était ce
    // pointeur non initialisé.
    case ENV_GET_VARIABLE: {
      struct retro_variable *v = d;
      if (!v || !v->key) return 0;
      const char *r = getenv("BANC_REGION");
      if (r && !strcmp(v->key, "genesis_plus_gx_region_detect")) {
        v->value = r; return 1;
      }
      v->value = NULL;
      return 0;
    }
    default: return 0;   // tout le reste : réglages par défaut du cœur
  }
}

static void video(const void *data, unsigned l, unsigned h, size_t pas) {
  img = data; img_l = l; img_h = h; img_pas = pas;
}
static void audio1(int16_t a, int16_t b) {
  if (son && son_n + 2 <= SON_MAX) { son[son_n++] = a; son[son_n++] = b; }
}
static size_t audiob(const int16_t *d, size_t n) {
  if (son) for (size_t k = 0; k < n * 2 && son_n < SON_MAX; k++) son[son_n++] = d[k];
  return n;
}
static void poll(void) { }
static int16_t entree(unsigned p, unsigned d, unsigned i, unsigned id) {
  (void)d; (void)i;
  if (p != 0) return 0;
  for (int k = 0; k < n_pressions; k++)
    if ((int)id == pressions[k].id &&
        image_courante >= pressions[k].image &&
        image_courante < pressions[k].image + pressions[k].duree)
      return 1;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: banc <coeur> <rom> <images> [sortie.ppm] [reset|-1] [script]\n"
            "  script : \"60:DOWN,90:DOWN*30,120:C\"  (image:bouton[*duree])\n"
            "  boutons : UP DOWN LEFT RIGHT A B C START\n");
    return 2;
  }
  const char *sortie = (argc > 4) ? argv[4] : "image.ppm";
  const int images = atoi(argv[3]);
  snprintf(rep, sizeof rep, ".");

  void *c = dlopen(argv[1], RTLD_NOW);
  if (!c) { fprintf(stderr, "coeur introuvable: %s\n", dlerror()); return 1; }

  typedef void (*pose_env_t)(int (*)(unsigned, void *));
  typedef void (*pose_video_t)(void (*)(const void *, unsigned, unsigned, size_t));
  typedef void (*pose_audio1_t)(void (*)(int16_t, int16_t));
  typedef void (*pose_audiob_t)(size_t (*)(const int16_t *, size_t));
  typedef void (*pose_poll_t)(void (*)(void));
  typedef void (*pose_entree_t)(int16_t (*)(unsigned, unsigned, unsigned, unsigned));
  typedef void (*rien_t)(void);
  typedef int  (*charge_t)(const struct retro_game_info *);
  typedef void (*reinit_t)(void);
  typedef void (*av_info_t)(struct retro_system_av_info *);
  typedef void  *(*mem_donnees_t)(unsigned);
  typedef size_t (*mem_taille_t)(unsigned);

  #define LIE(v, n, t) t v = (t)dlsym(c, n); \
    if (!v) { fprintf(stderr, "symbole manquant: %s\n", n); return 1; }
  LIE(pose_env,    "retro_set_environment",        pose_env_t)
  LIE(pose_video,  "retro_set_video_refresh",      pose_video_t)
  LIE(pose_a1,     "retro_set_audio_sample",       pose_audio1_t)
  LIE(pose_ab,     "retro_set_audio_sample_batch", pose_audiob_t)
  LIE(pose_poll,   "retro_set_input_poll",         pose_poll_t)
  LIE(pose_entree, "retro_set_input_state",        pose_entree_t)
  LIE(demarre,     "retro_init",                   rien_t)
  LIE(charge,      "retro_load_game",              charge_t)
  LIE(tourne,      "retro_run",                    rien_t)
  LIE(reinit,      "retro_reset",                  reinit_t)
  LIE(av_info,     "retro_get_system_av_info",     av_info_t)
  LIE(mem_donnees, "retro_get_memory_data",        mem_donnees_t)
  LIE(mem_taille,  "retro_get_memory_size",        mem_taille_t)

  pose_env(env);
  pose_video(video);
  pose_a1(audio1);
  pose_ab(audiob);
  pose_poll(poll);
  pose_entree(entree);
  demarre();
  son = malloc(SON_MAX * sizeof *son);

  FILE *f = fopen(argv[2], "rb");
  if (!f) { fprintf(stderr, "rom introuvable\n"); return 1; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  void *rom = malloc((size_t)n);
  if (fread(rom, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "lecture rom\n"); return 1; }
  fclose(f);

  struct retro_game_info g = { argv[2], rom, (size_t)n, NULL };
  if (!charge(&g)) { fprintf(stderr, "le coeur refuse la rom\n"); return 1; }

  // Un reset en cours de route imite la COUPURE DE COURANT du diagnostic :
  // il relance le 68000 sans effacer la RAM sauvegardée, ce qui est très
  // exactement la situation qu'on veut mettre à l'épreuve.
  const int reset_a = (argc > 5) ? atoi(argv[5]) : -1;
  if (argc > 6) lit_script(argv[6]);
  for (int i = 0; i < images; i++) {
    image_courante = i;
    if (i == reset_a) { reinit(); fprintf(stderr, "reset a l'image %d\n", i); }
    tourne();
  }

  if (!img) { fprintf(stderr, "aucune image produite\n"); return 1; }

  FILE *o = fopen(sortie, "wb");
  fprintf(o, "P6\n%u %u\n255\n", img_l, img_h);
  for (unsigned y = 0; y < img_h; y++) {
    const uint8_t *l = (const uint8_t *)img + y * img_pas;
    for (unsigned x = 0; x < img_l; x++) {
      uint8_t r, v, b;
      if (fmt == 2) { uint16_t p = ((const uint16_t *)l)[x];
                      r = (p >> 11) << 3; v = ((p >> 5) & 0x3F) << 2; b = (p & 0x1F) << 3; }
      else if (fmt == 0) { uint16_t p = ((const uint16_t *)l)[x];
                      r = ((p >> 10) & 0x1F) << 3; v = ((p >> 5) & 0x1F) << 3; b = (p & 0x1F) << 3; }
      else { uint32_t p = ((const uint32_t *)l)[x];
                      r = (p >> 16) & 0xFF; v = (p >> 8) & 0xFF; b = p & 0xFF; }
      fputc(r, o); fputc(v, o); fputc(b, o);
    }
  }
  fclose(o);
  fprintf(stderr, "image %ux%u ecrite dans %s (format %u)\n", img_l, img_h, sortie, fmt);

  // La SRAM de la machine émulée, telle que le programme l'a laissée. Lire ce
  // que le tracker a RÉELLEMENT écrit vaut mieux que d'interpréter des pixels :
  // on vérifie la donnée, pas son rendu.
  {
    void *sram = mem_donnees(0);          // RETRO_MEMORY_SAVE_RAM
    const size_t n = mem_taille(0);
    if (sram && n) {
      char chemin[1200];
      snprintf(chemin, sizeof chemin, "%s.sram", sortie);
      FILE *sf = fopen(chemin, "wb");
      if (sf) { fwrite(sram, 1, n, sf); fclose(sf);
                fprintf(stderr, "sram %zu octets ecrite dans %s\n", n, chemin); }
    }
  }

  // Le WAV, à côté de l'image.
  {
    struct retro_system_av_info av; av_info(&av);
    const unsigned taux = (unsigned)(av.tim.sample_rate + 0.5);
    char chemin[1200];
    snprintf(chemin, sizeof chemin, "%s.wav", sortie);
    FILE *w = fopen(chemin, "wb");
    if (w && son_n) {
      const uint32_t octets = (uint32_t)(son_n * 2);
      const uint32_t tailleFichier = 36 + octets;
      const uint32_t debit = taux * 2 * 2;
      fwrite("RIFF", 1, 4, w); fwrite(&tailleFichier, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
      uint32_t u = 16; fwrite(&u, 4, 1, w);
      uint16_t v = 1;  fwrite(&v, 2, 1, w);
      v = 2;           fwrite(&v, 2, 1, w);
      u = taux;        fwrite(&u, 4, 1, w);
      u = debit;       fwrite(&u, 4, 1, w);
      v = 4;           fwrite(&v, 2, 1, w);
      v = 16;          fwrite(&v, 2, 1, w);
      fwrite("data", 1, 4, w); fwrite(&octets, 4, 1, w);
      fwrite(son, 2, son_n, w);
      fclose(w);
      fprintf(stderr, "son %zu echantillons a %u Hz dans %s\n", son_n / 2, taux, chemin);
    }
  }
  return 0;
}
