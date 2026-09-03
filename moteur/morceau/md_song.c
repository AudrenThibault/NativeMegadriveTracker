#include "md_song.h"
#include "md_codec.h"
#include <stddef.h>

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);

// Le compilateur refuse de continuer si la mise en page déborde de ce que la
// cartouche persiste. Mieux vaut une erreur ici qu'un morceau tronqué à
// l'autre bout de la chaîne, découvert sur la machine.
_Static_assert(MD_TAILLE_TOTALE <= MD_SRAM_UTILE,
               "la disposition du morceau depasse les 32 Ko persistes");

// La bascule ROM/SRAM de la cartouche : 1 = la SRAM est visible en 0x200000.
#define MD_SRAM_BASCULE (*(volatile uint8_t *)0xA130F1)

static const char MD_MAGIE[6] = {'M','D','T','R','K','1'};

// ── Le morceau de travail ─────────────────────────────────────────────────
// 28 Ko dans la RAM de la console. On n'en utilisait presque rien, et ça
// libère TOUTE la mémoire de sauvegarde pour la bibliothèque.
static uint8_t travail[MD_TAILLE_TOTALE];

// Un SEUL tampon pour le morceau comprimé, partagé par l'enregistrement, le
// chargement et le tassement : ils ne s'exécutent jamais en même temps, et la
// RAM de la console est comptée.
#define PAQUET_MAX 8192
static uint8_t paquet[PAQUET_MAX];

uint8_t *md_travail(void)              { return travail; }
uint8_t  md_lit(uint32_t o)            { return travail[o]; }
void     md_ecrit(uint32_t o, uint8_t v) { travail[o] = v; }

int md_song_memoire_presente(void) {
  MD_SRAM_BASCULE = 1;
  // Deux valeurs opposées, relues aussitôt. Une seule ne prouverait rien : un
  // bus qui flotte peut rendre par hasard ce qu'on vient d'écrire.
  const uint8_t garde = md_sram_lit(0);
  md_sram_ecrit(0, 0xA5); const int a = (md_sram_lit(0) == 0xA5);
  md_sram_ecrit(0, 0x5A); const int b = (md_sram_lit(0) == 0x5A);
  md_sram_ecrit(0, garde);
  return a && b;
}

void md_song_vide(void) {
  md_codec_vide(travail);
  for (int k = 0; k < 6; k++) travail[k] = (uint8_t)MD_MAGIE[k];
  travail[6] = 1;      // version de la forme
  travail[7] = 50;     // tempo, pour l'export
  travail[14] = 125;   // BPM, la valeur qui compte
  travail[15] = 0;
  travail[8] = 6;      // vitesse
  travail[9] = MD_CANAUX;
  travail[10] = 1;     // macro_speedup
}

// ── La bibliothèque ───────────────────────────────────────────────────────
// En-tête : "GTLIB1" + version + nombre d'emplacements.
// Puis seize entrées de quatorze octets : nom[10], offset u16, taille u16.
static const char BIB_MAGIE[6] = {'G','T','L','I','B','1'};

static uint32_t entree(int e) { return 8 + (uint32_t)e * 14; }

void md_bib_init(void) {
  MD_SRAM_BASCULE = 1;
  for (int k = 0; k < 6; k++)
    if (md_sram_lit((uint32_t)k) != (uint8_t)BIB_MAGIE[k]) {
      // Bibliothèque neuve : on la formate. Tout ce qui traînait est perdu —
      // mais ce qui traînait n'était pas une bibliothèque.
      for (uint32_t o = 0; o < MD_BIB_DONNEES; o++) md_sram_ecrit(o, 0);
      for (int j = 0; j < 6; j++) md_sram_ecrit((uint32_t)j, (uint8_t)BIB_MAGIE[j]);
      md_sram_ecrit(6, 1);
      md_sram_ecrit(7, MD_BIB_EMPLACEMENTS);
      return;
    }
}

int md_bib_occupe(int e) {
  if (e < 0 || e >= MD_BIB_EMPLACEMENTS) return 0;
  return md_bib_taille(e) != 0;
}

uint16_t md_bib_taille(int e) {
  const uint32_t o = entree(e);
  return (uint16_t)(md_sram_lit(o + 12) | (md_sram_lit(o + 13) << 8));
}

static uint16_t bib_offset(int e) {
  const uint32_t o = entree(e);
  return (uint16_t)(md_sram_lit(o + 10) | (md_sram_lit(o + 11) << 8));
}

void md_bib_nom(int e, char *dest) {
  const uint32_t o = entree(e);
  for (int k = 0; k < MD_BIB_NOM; k++) dest[k] = (char)md_sram_lit(o + (uint32_t)k);
  dest[MD_BIB_NOM] = 0;
}

void md_bib_pose_nom(int e, const char *nom) {
  const uint32_t o = entree(e);
  for (int k = 0; k < MD_BIB_NOM; k++)
    md_sram_ecrit(o + (uint32_t)k, nom[k] ? (uint8_t)nom[k] : (uint8_t)' ');
}

// Ce qui reste après le dernier morceau rangé.
uint32_t md_bib_libre(void) {
  uint32_t haut = MD_BIB_DONNEES;
  for (int e = 0; e < MD_BIB_EMPLACEMENTS; e++) {
    const uint32_t f = (uint32_t)bib_offset(e) + md_bib_taille(e);
    if (md_bib_taille(e) && f > haut) haut = f;
  }
  return MD_BIB_FIN - haut;
}

// Range les morceaux bout à bout, sans trou. Appelé après un effacement : sans
// ça la place libérée resterait perdue au milieu.
static void bib_tasse(void) {

  uint32_t ecrit = MD_BIB_DONNEES;
  for (int e = 0; e < MD_BIB_EMPLACEMENTS; e++) {
    const uint16_t n = md_bib_taille(e);
    if (!n) continue;
    const uint16_t src = bib_offset(e);
    if (src != ecrit) {
      for (uint16_t k = 0; k < n && k < PAQUET_MAX; k++) paquet[k] = md_sram_lit(src + k);
      for (uint16_t k = 0; k < n && k < PAQUET_MAX; k++) md_sram_ecrit(ecrit + k, paquet[k]);
      const uint32_t o = entree(e);
      md_sram_ecrit(o + 10, (uint8_t)(ecrit & 0xFF));
      md_sram_ecrit(o + 11, (uint8_t)(ecrit >> 8));
    }
    ecrit += n;
  }
}

int md_bib_sauve(int e) {
  if (e < 0 || e >= MD_BIB_EMPLACEMENTS) return 0;
  const uint32_t n = md_codec_comprime(travail, paquet, sizeof paquet);
  if (!n) return 0;

  // On libère l'ancien contenu AVANT de mesurer la place : réenregistrer par
  // dessus soi-même ne doit pas échouer faute de place.
  const uint32_t o = entree(e);
  md_sram_ecrit(o + 12, 0); md_sram_ecrit(o + 13, 0);
  bib_tasse();
  if (n > md_bib_libre()) return 0;

  uint32_t haut = MD_BIB_DONNEES;
  for (int j = 0; j < MD_BIB_EMPLACEMENTS; j++) {
    const uint32_t f = (uint32_t)bib_offset(j) + md_bib_taille(j);
    if (md_bib_taille(j) && f > haut) haut = f;
  }
  for (uint32_t k = 0; k < n; k++) md_sram_ecrit(haut + k, paquet[k]);
  md_sram_ecrit(o + 10, (uint8_t)(haut & 0xFF));
  md_sram_ecrit(o + 11, (uint8_t)(haut >> 8));
  md_sram_ecrit(o + 12, (uint8_t)(n & 0xFF));
  md_sram_ecrit(o + 13, (uint8_t)(n >> 8));
  return 1;
}

int md_bib_charge(int e) {
  if (!md_bib_occupe(e)) return 0;
  const uint16_t n = md_bib_taille(e), src = bib_offset(e);
  if (n > sizeof paquet) return 0;
  for (uint16_t k = 0; k < n; k++) paquet[k] = md_sram_lit(src + k);
  md_codec_decomprime(paquet, n, travail);
  return 1;
}

int md_bib_efface(int e) {
  if (e < 0 || e >= MD_BIB_EMPLACEMENTS) return 0;
  const uint32_t o = entree(e);
  md_sram_ecrit(o + 12, 0); md_sram_ecrit(o + 13, 0);
  for (int k = 0; k < MD_BIB_NOM; k++) md_sram_ecrit(o + (uint32_t)k, ' ');
  bib_tasse();
  return 1;
}

// ── Le journal ─────────────────────────────────────────────────────────────
static uint32_t journal_n;
static void journal_pose(void);

// ⚠️ LE JOURNAL S'AJOUTE, IL NE S'EFFACE PAS.
//
// La cartouche ne recopie la mémoire de sauvegarde sur la carte qu'au
// redémarrage, quand son propre menu reprend la main. Or on ne peut pas
// revenir au menu depuis le tracker : la seule sortie est un reset, et un
// reset relance le tracker. Si le démarrage remettait le journal à zéro, il
// effacerait précisément ce qu'on venait de relever — on ne pourrait jamais
// lire une trace qu'une fois, et jamais celle d'un plantage.
//
// Alors chaque démarrage ÉCRIT À LA SUITE. La position vit dans les deux
// octets qui suivent la marque, et on ne repart du début que lorsqu'il ne
// reste plus la place d'un compte rendu entier.
#define JOURNAL_ENTETE 14          /* la marque, puis la position sur 16 bits */
#define JOURNAL_RESERVE 260        /* de quoi loger un démarrage complet */

void md_journal_debut(void) {
  // ⚠️ LA MARQUE PORTE LE NUMÉRO DE DISPOSITION, et ce n'est pas cosmétique.
  // En raccourcissant le journal pour loger l'anneau PCM, j'ai posé celui-ci
  // sur des octets que l'ancien journal occupait encore — et l'anneau s'est
  // mis à rendre du TEXTE en guise de mesures. Changer la marque force le
  // nettoyage ci-dessous, une fois, à la première mise à jour.
  static const char marque[] = "GENETRK-LOG3";
  int deja = 1;
  for (int k = 0; marque[k]; k++)
    if (md_sram_lit(MD_OFF_JOURNAL + (uint32_t)k) != (uint8_t)marque[k]) { deja = 0; break; }

  uint32_t pos = 0;
  if (deja)
    pos = (uint32_t)md_sram_lit(MD_OFF_JOURNAL + 12)
        | ((uint32_t)md_sram_lit(MD_OFF_JOURNAL + 13) << 8);
  if (!deja) {
    // Disposition inconnue : on efface TOUT ce qui suit la bibliothèque.
    // Journal, sonde, fil d'Ariane, anneau PCM — que des instruments de
    // mesure, rien de précieux, et des restes d'une ancienne disposition ne
    // valent pas mieux que du vide : ils se lisent comme des mesures.
    for (uint32_t o = MD_OFF_JOURNAL; o < MD_SRAM_UTILE; o++) md_sram_ecrit(o, 0);
  }
  if (!deja || pos < JOURNAL_ENTETE || pos > MD_JOURNAL_MAX - JOURNAL_RESERVE) {
    for (int k = 0; marque[k]; k++)
      md_sram_ecrit(MD_OFF_JOURNAL + (uint32_t)k, (uint8_t)marque[k]);
    pos = JOURNAL_ENTETE;
  }
  journal_n = pos;
  journal_pose();
}

// La position est relue au démarrage suivant : elle doit suivre l'écriture,
// pas attendre une fin de session qui n'arrive jamais.
static void journal_pose(void) {
  md_sram_ecrit(MD_OFF_JOURNAL + 12, (uint8_t)(journal_n & 0xFF));
  md_sram_ecrit(MD_OFF_JOURNAL + 13, (uint8_t)(journal_n >> 8));
}

void md_journal_txt(const char *s) {
  while (*s && journal_n < MD_JOURNAL_MAX - 1)
    md_sram_ecrit(MD_OFF_JOURNAL + journal_n++, (uint8_t)*s++);
  md_sram_ecrit(MD_OFF_JOURNAL + journal_n, 0);
  journal_pose();
}

void md_journal_hex(uint32_t v, int chiffres) {
  for (int i = chiffres - 1; i >= 0; i--) {
    const uint8_t n = (v >> (i * 4)) & 0xF;
    if (journal_n < MD_JOURNAL_MAX - 1)
      md_sram_ecrit(MD_OFF_JOURNAL + journal_n++,
                    (uint8_t)(n < 10 ? '0' + n : 'A' + n - 10));
  }
  md_sram_ecrit(MD_OFF_JOURNAL + journal_n, 0);
  journal_pose();
}

void md_journal_dec(uint32_t v) {
  char r[12]; int n = 0;
  if (!v) { md_journal_txt("0"); return; }
  while (v) { r[n++] = (char)('0' + v % 10); v /= 10; }
  while (n && journal_n < MD_JOURNAL_MAX - 1)
    md_sram_ecrit(MD_OFF_JOURNAL + journal_n++, (uint8_t)r[--n]);
  md_sram_ecrit(MD_OFF_JOURNAL + journal_n, 0);
  journal_pose();
}

void md_journal_ligne(void) {
  if (journal_n < MD_JOURNAL_MAX - 3) {
    md_sram_ecrit(MD_OFF_JOURNAL + journal_n++, 0x0D);
    md_sram_ecrit(MD_OFF_JOURNAL + journal_n++, 0x0A);
  }
  md_sram_ecrit(MD_OFF_JOURNAL + journal_n, 0);
  journal_pose();
}

// Compte les démarrages, et rend le total. Une signature protège le compteur :
// sans elle, de la mémoire non initialisée passerait pour un décompte.
// ── Fil d'Ariane ──────────────────────────────────────────────────────────
// Six octets : signature, page, activite, compteur d'images. On les ecrit tels
// quels, sans relecture — l'ecriture doit couter le moins possible, elle a
// lieu a chaque image.
static uint32_t miette_images;

void md_miette(uint8_t page, uint8_t activite) {
  miette_images++;
  md_sram_ecrit(MD_OFF_MIETTE + 0, 0x4D);   // 'M'
  md_sram_ecrit(MD_OFF_MIETTE + 1, page);
  md_sram_ecrit(MD_OFF_MIETTE + 2, activite);
  md_sram_ecrit(MD_OFF_MIETTE + 3, (uint8_t)(miette_images));
  md_sram_ecrit(MD_OFF_MIETTE + 4, (uint8_t)(miette_images >> 8));
  md_sram_ecrit(MD_OFF_MIETTE + 5, (uint8_t)(miette_images >> 16));
}

// Les six octets suivants : OU on jouait. Ecrits par la boucle principale,
// juste apres la miette, et relus au demarrage d'apres.
void md_miette_position(uint8_t voie, uint8_t song, uint8_t chain,
                        uint8_t phrase, uint8_t instr, uint8_t pcm) {
  md_sram_ecrit(MD_OFF_MIETTE + 6, voie);
  md_sram_ecrit(MD_OFF_MIETTE + 7, song);
  md_sram_ecrit(MD_OFF_MIETTE + 8, chain);
  md_sram_ecrit(MD_OFF_MIETTE + 9, phrase);
  md_sram_ecrit(MD_OFF_MIETTE + 10, instr);
  md_sram_ecrit(MD_OFF_MIETTE + 11, pcm);
}

void md_miette_relit(uint8_t *page, uint8_t *activite, uint32_t *images) {
  if (md_sram_lit(MD_OFF_MIETTE + 0) != 0x4D) {
    *page = 0xFF; *activite = 0xFF; *images = 0;
    return;
  }
  *page = md_sram_lit(MD_OFF_MIETTE + 1);
  *activite = md_sram_lit(MD_OFF_MIETTE + 2);
  *images = (uint32_t)md_sram_lit(MD_OFF_MIETTE + 3)
          | ((uint32_t)md_sram_lit(MD_OFF_MIETTE + 4) << 8)
          | ((uint32_t)md_sram_lit(MD_OFF_MIETTE + 5) << 16);
}

// Un plantage attrape par les vecteurs. Il survit a la coupure, alors qu'un
// ecran affiche disparait des qu'on redemarre.
void md_miette_exception(uint32_t vecteur, uint32_t pc) {
  md_sram_ecrit(MD_OFF_MIETTE + 8, 0x58);   // 'X'
  md_sram_ecrit(MD_OFF_MIETTE + 9, (uint8_t)vecteur);
  for (int k = 0; k < 4; k++)
    md_sram_ecrit(MD_OFF_MIETTE + 10 + (uint32_t)k, (uint8_t)(pc >> (k * 8)));
}

int md_miette_exception_relit(uint32_t *vecteur, uint32_t *pc) {
  if (md_sram_lit(MD_OFF_MIETTE + 8) != 0x58) return 0;
  *vecteur = md_sram_lit(MD_OFF_MIETTE + 9);
  *pc = 0;
  for (int k = 0; k < 4; k++)
    *pc |= (uint32_t)md_sram_lit(MD_OFF_MIETTE + 10 + (uint32_t)k) << (k * 8);
  md_sram_ecrit(MD_OFF_MIETTE + 8, 0);   // on ne le raconte qu'une fois
  return 1;
}

// Le prochain emplacement libre de l'anneau vit dans son premier octet : il
// doit survivre à la coupure comme le reste, sinon on ne saurait pas où le
// dernier événement s'arrête.
void md_pcm_anneau_pose(const uint8_t *evt) {
  uint8_t n = md_sram_lit(MD_OFF_PCM_ANNEAU);
  if (n >= MD_PCM_ANNEAU) n = 0;
  md_sram_ecrit(MD_OFF_PCM_ANNEAU + 1, MD_PCM_EVT);   // comment me lire
  const uint32_t b = MD_OFF_PCM_ANNEAU + 2 + (uint32_t)n * MD_PCM_EVT;
  for (int k = 0; k < MD_PCM_EVT; k++) md_sram_ecrit(b + (uint32_t)k, evt[k]);
  md_sram_ecrit(MD_OFF_PCM_ANNEAU, (uint8_t)((n + 1) % MD_PCM_ANNEAU));
}

uint16_t md_sonde_demarrages(void) {
  static const char sig[4] = {'S','O','N','D'};
  int bonne = 1;
  for (int k = 0; k < 4; k++)
    if (md_sram_lit(MD_OFF_SONDE + (uint32_t)k) != (uint8_t)sig[k]) { bonne = 0; break; }
  uint16_t n = 0;
  if (bonne)
    n = (uint16_t)(md_sram_lit(MD_OFF_SONDE + 4) | (md_sram_lit(MD_OFF_SONDE + 5) << 8));
  else
    for (int k = 0; k < 4; k++) md_sram_ecrit(MD_OFF_SONDE + (uint32_t)k, (uint8_t)sig[k]);
  n++;
  md_sram_ecrit(MD_OFF_SONDE + 4, (uint8_t)(n & 0xFF));
  md_sram_ecrit(MD_OFF_SONDE + 5, (uint8_t)(n >> 8));
  return n;
}

// L'emplacement 0 est celui du TRAVAIL : c'est là que le morceau en cours est
// reporté tout seul. Ouvrir le tracker, c'est le recharger.
int md_song_ouvre(void) {
  md_bib_init();
  if (md_bib_charge(0)) return 1;
  md_song_vide();
  return 0;
}

// ── SONG ───────────────────────────────────────────────────────────────────
uint8_t md_song_lit(int canal, int ligne) {
  return md_lit(MD_OFF_SONG + (uint32_t)canal * MD_SONG_LIGNES + (uint32_t)ligne);
}
void md_song_pose(int canal, int ligne, uint8_t chain) {
  md_ecrit(MD_OFF_SONG + (uint32_t)canal * MD_SONG_LIGNES + (uint32_t)ligne, chain);
}

// ── CHAIN ──────────────────────────────────────────────────────────────────
void md_chain_lit(int chain, int ligne, uint8_t *phrase, int8_t *transpose) {
  const uint32_t o = MD_OFF_CHAINS + (uint32_t)(chain * MD_LIGNES_CHAIN + ligne) * 2;
  if (phrase)    *phrase = md_lit(o);
  if (transpose) *transpose = (int8_t)md_lit(o + 1);
}
void md_chain_pose(int chain, int ligne, uint8_t phrase, int8_t transpose) {
  const uint32_t o = MD_OFF_CHAINS + (uint32_t)(chain * MD_LIGNES_CHAIN + ligne) * 2;
  md_ecrit(o, phrase);
  md_ecrit(o + 1, (uint8_t)transpose);
}

// ── PHRASE ─────────────────────────────────────────────────────────────────
void md_phrase_lit(int phrase, int ligne, md_ligne_phrase *l) {
  const uint32_t o = MD_OFF_PHRASES +
                     (uint32_t)(phrase * MD_LIGNES_PHRASE + ligne) * MD_PHRASE_OCTETS;
  l->note  = md_lit(o + 0); l->instr = md_lit(o + 1);
  l->vel   = md_lit(o + 2); l->cmd   = md_lit(o + 3);
  l->val   = md_lit(o + 4); l->mdcmd = md_lit(o + 5);
  l->mdval = md_lit(o + 6);
}
void md_phrase_pose(int phrase, int ligne, const md_ligne_phrase *l) {
  const uint32_t o = MD_OFF_PHRASES +
                     (uint32_t)(phrase * MD_LIGNES_PHRASE + ligne) * MD_PHRASE_OCTETS;
  md_ecrit(o + 0, l->note);  md_ecrit(o + 1, l->instr);
  md_ecrit(o + 2, l->vel);   md_ecrit(o + 3, l->cmd);
  md_ecrit(o + 4, l->val);   md_ecrit(o + 5, l->mdcmd);
  md_ecrit(o + 6, l->mdval);
}

// ── Réglages ───────────────────────────────────────────────────────────────
uint8_t md_song_tempo(void)              { return md_lit(7); }
void    md_song_pose_tempo(uint8_t t)    { md_ecrit(7, t); }
uint8_t md_song_vitesse(void)            { return md_lit(8); }
void    md_song_pose_vitesse(uint8_t v)  { md_ecrit(8, v); }

uint16_t md_song_bpm(void) {
  const uint16_t b = (uint16_t)(md_lit(14) | (md_lit(15) << 8));
  if (b >= 20 && b <= 400) return b;
  // Un morceau venu d'un .mdm n'a pas ce champ : on le déduit une fois.
  const int t = md_song_tempo(), v = md_song_vitesse();
  return (v < 1) ? 125 : (uint16_t)((t * 60) / (v * MD_LIGNES_PAR_TEMPS));
}

void md_song_pose_bpm(int bpm) {
  if (bpm < 20) bpm = 20;
  if (bpm > 400) bpm = 400;
  md_ecrit(14, (uint8_t)(bpm & 0xFF));
  md_ecrit(15, (uint8_t)(bpm >> 8));
  // On tient le tempo à jour pour l'export .mdm, au cran le plus proche.
  int v = md_song_vitesse(); if (v < 1) v = 6;
  int t = (bpm * v * MD_LIGNES_PAR_TEMPS + 30) / 60;
  if (t < 1) t = 1;
  if (t > 255) t = 255;
  md_song_pose_tempo((uint8_t)t);
}

uint16_t md_song_macro(void) {
  return (uint16_t)(md_lit(10) | (md_lit(11) << 8));
}
int16_t md_song_finetune(void) {
  return (int16_t)(md_lit(12) | (md_lit(13) << 8));
}

// ── Premier emplacement libre ──────────────────────────────────────────────
// C'est ce que sert le double-appui sur C : créer un élément NEUF plutôt que
// de reprendre le dernier employé.
// Meme regle pour les chaines : libre = vide ET designee par aucune case du
// SONG. Voir md_phrase_libre pour ce que coutait l'ancienne definition.
static int chain_referencee(int c) {
  for (int canal = 0; canal < MD_CANAUX; canal++)
    for (int l = 0; l < MD_SONG_LIGNES; l++)
      if (md_song_lit(canal, l) == (uint8_t)c) return 1;
  return 0;
}

uint8_t md_chain_libre(void) {
  for (int c = 0; c < MD_MAX_CHAINS; c++) {
    int vide = 1;
    for (int l = 0; l < MD_LIGNES_CHAIN && vide; l++) {
      uint8_t p; md_chain_lit(c, l, &p, 0);
      if (p != MD_VIDE) vide = 0;
    }
    if (vide && !chain_referencee(c)) return (uint8_t)c;
  }
  for (int c = 0; c < MD_MAX_CHAINS; c++) {
    if (chain_referencee(c)) continue;
    for (int l = 0; l < MD_LIGNES_CHAIN; l++) md_chain_pose(c, l, MD_VIDE, 0);
    return (uint8_t)c;
  }
  return MD_VIDE;
}

// Une table est LIBRE si aucun de ses seize pas ne dit quoi que ce soit : ni
// volume, ni transposition, ni commande. Le neutre est zéro pour les deux
// premiers et MD_VIDE pour les lettres — voir md_codec_vide.
uint8_t md_table_libre(void) {
  for (int t = 0; t < MD_MAX_TABLES; t++) {
    int vide = 1;
    for (int l = 0; l < MD_LIGNES_TABLE && vide; l++) {
      const uint32_t b = MD_OFF_TABLES
                       + ((uint32_t)t * MD_LIGNES_TABLE + l) * MD_TABLE_OCTETS;
      if (md_lit(b) || md_lit(b + 1)) vide = 0;
      if (md_lit(b + 2) != MD_VIDE || md_lit(b + 4) != MD_VIDE
          || md_lit(b + 6) != MD_VIDE) vide = 0;
    }
    if (vide) return (uint8_t)t;
  }
  return MD_VIDE;
}

// ── « LIBRE » VEUT DIRE « QUE PERSONNE N'UTILISE » ────────────────────────
// ⚠️ CE N'ETAIT PAS LE CAS, ET CA DETRUISAIT DU TRAVAIL.
//
// On ne regardait QUE le contenu : une phrase sans notes etait declaree libre
// meme si une chaine s'en servait. Le clonage profond se l'appropriait et y
// ecrivait ses notes — et l'endroit qui l'utilisait ailleurs dans le morceau
// se mettait a jouer ces notes-la. Un morceau entier perdu en un geste.
//
// Une phrase n'est donc libre que si AUCUNE CHAINE ne la designe ET qu'elle
// est vide. Les deux conditions comptent : la premiere protege ce qui sert, la
// seconde protege ce qu'on a ecrit sans l'avoir encore place.
static int phrase_referencee(int p) {
  for (int c = 0; c < MD_MAX_CHAINS; c++)
    for (int l = 0; l < MD_LIGNES_CHAIN; l++) {
      uint8_t ph; md_chain_lit(c, l, &ph, 0);
      if (ph == (uint8_t)p) return 1;
    }
  return 0;
}

uint8_t md_phrase_libre(void) {
  for (int p = 0; p < MD_MAX_PHRASES; p++) {
    int vide = 1;
    for (int l = 0; l < MD_LIGNES_PHRASE && vide; l++) {
      md_ligne_phrase r; md_phrase_lit(p, l, &r);
      // TOUS les champs, pas seulement la note : une ligne qui ne porte
      // qu'une velocite ou qu'une valeur de commande est du travail aussi.
      if (r.note || r.instr || r.vel != MD_VIDE || r.cmd != MD_VIDE
          || r.val || r.mdcmd != MD_VIDE || r.mdval) vide = 0;
    }
    if (vide && !phrase_referencee(p)) return (uint8_t)p;
  }
  // Rien de vide ET libre : on prend le premier que personne ne designe et on
  // le nettoie. On ne detruit donc jamais ce qui SERT — au pire on reprend un
  // brouillon oublie, ce qui vaut mieux que « plus de place ».
  for (int p = 0; p < MD_MAX_PHRASES; p++) {
    if (phrase_referencee(p)) continue;
    md_ligne_phrase v = { 0, 0, MD_VIDE, MD_VIDE, 0, MD_VIDE, 0 };
    for (int l = 0; l < MD_LIGNES_PHRASE; l++) md_phrase_pose(p, l, &v);
    return (uint8_t)p;
  }
  return MD_VIDE;
}
