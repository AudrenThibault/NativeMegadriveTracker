#include "md_puces.h"
#include "md_song.h"
#include "banque_pcm.h"
#include "pilote_pcm.h"

// ── Compiler cette couche SUR LE MAC ──────────────────────────────────────
// ⚠️ MD_HORS_CONSOLE existe pour UNE raison : pouvoir relever, sans deviner,
// la suite exacte d'écritures que la console envoie au YM2612. On la rejoue
// ensuite dans deux modèles de puce différents et on compare — c'est le seul
// moyen de trancher une question de TIMBRE, où lire le code ne suffit plus
// puisque les deux projets écrivent les mêmes octets.
//
// Hors console, l'outil fournit md_ym_ecrit ; le bus Z80 et les ports n'ont
// plus d'objet. Rien de tout ça n'entre dans la ROM.
#ifdef MD_HORS_CONSOLE
static uint8_t faux_ports[4];
#define YM_A0 (faux_ports[0])
#define YM_D0 (faux_ports[1])
#define YM_A1 (faux_ports[2])
#define YM_D1 (faux_ports[3])
#define PSG   (faux_ports[0])
void md_puces_pose_region(int est_pal);
#else
#define YM_A0 (*(volatile uint8_t *)0xA04000)
#define YM_D0 (*(volatile uint8_t *)0xA04001)
#define YM_A1 (*(volatile uint8_t *)0xA04002)
#define YM_D1 (*(volatile uint8_t *)0xA04003)
#define PSG   (*(volatile uint8_t *)0xC00011)
#endif

static int pal;           // la console est-elle en 50 Hz ?
static int pcm_branche;   // le convertisseur occupe-t-il la 6e voie ?

// ── Les hauteurs ───────────────────────────────────────────────────────────
// F-Numbers du YM2612 pour les douze demi-tons. La table vaut pour le DO de
// l'octave 1 au bloc 0 ; notre C-0 tombe donc un cran plus bas, d'où le
// décalage dans md_fm_note_on.
//
// Les deux jeux ne diffèrent que par l'HORLOGE : 7 670 454 Hz sur une console
// 60 Hz, 7 600 489 Hz sur une 50 Hz. Un pour cent d'écart, soit seize cents —
// audible sur un accord tenu. On lit la région au démarrage plutôt que de
// choisir pour l'utilisateur.
static const uint16_t FNUM_NTSC[12] = {
  644, 681, 722, 765, 810, 858, 910, 964, 1021, 1081, 1146, 1214
};
static const uint16_t FNUM_PAL[12] = {
  650, 687, 729, 772, 817, 866, 918, 973, 1030, 1091, 1157, 1225
};

// ── LA HAUTEUR DU PSG ─────────────────────────────────────────────────────
// ⚠️ DEUX OCTAVES AU-DESSUS DE LA VOIE FM, À NUMÉRO DE NOTE ÉGAL.
//
// Ce n'est pas un choix : c'est ce que fait le tracker DS, qui l'a lui-même
// calibré à l'oreille contre DefleMask (md_replayer.c, MD_PSG_OCTAVE_SHIFT).
// La table d'ici était accordée au diapason, donc DEUX OCTAVES PLUS BAS que
// la DS. Relevé en comparant les périodes écrites pour les mêmes notes : un
// rapport de 3,98 — deux octaves — CONSTANT sur toute l'étendue. Un morceau
// venu de la DS sonnait donc juste dans l'absolu et faux contre lui-même.
//
// Contrepartie, la même que sur la DS : le compteur du SN76489 n'a que dix
// bits, donc les notes les plus graves tapent le plancher de la puce et
// sonnent toutes pareil. C'est la puce, pas le tracker.
//
// Les valeurs sont des périodes AU 1/256 pour l'octave 0, divisées AVEC
// ARRONDI. Une table d'entiers décalée d'octave en octave dérivait jusqu'à
// 113 cents dans l'aigu ; ainsi l'écart avec la DS reste sous 30 cents sur
// C-1..B-6.
static const uint32_t PSG_P256_NTSC[12] = {
  440218, 415964, 392195, 370086, 349525, 329683,
  311330, 293764, 277564, 261690, 247128, 233377
};
static const uint32_t PSG_P256_PAL[12] = {
  436203, 412170, 388617, 366710, 346337, 326676,
  308490, 291085, 275032, 259303, 244873, 231248
};

// ── Écritures ──────────────────────────────────────────────────────────────
// Le drapeau d'occupation se lit sur N'IMPORTE QUEL port du YM2612, bit 7. On
// l'attend AVANT d'écrire — et l'attente est bornée, comme partout dans ce
// projet : une puce muette ne doit pas figer le tracker.
static void ym_attend(void) {
  for (int g = 0; g < 2000; g++)
    if (!(YM_A0 & 0x80)) return;
}

// ── Le relevé des écritures, SUR LA CONSOLE ───────────────────────────────
// ⚠️ Il existe pour une raison précise : outils/comparateur relève la même
// suite sur le Mac, et son rejeu dans deux modèles de puce donne un son que
// le tracker, lui, ne produit pas. Comparer les deux relevés octet par octet
// est le seul moyen de savoir où ça diverge — tout le reste est supposition,
// et j'ai déjà supposé faux trois fois sur cette question.
#define TRACE_YM_MAX 48
static uint8_t trace_ym[TRACE_YM_MAX][3];
static int trace_ym_n = -1;          // -1 : désarmé

void md_ym_trace_arme(void) { trace_ym_n = 0; }
int  md_ym_trace_nombre(void) { return trace_ym_n < 0 ? 0 : trace_ym_n; }
void md_ym_trace_lit(int i, int *banc, uint8_t *reg, uint8_t *val) {
  if (i < 0 || i >= TRACE_YM_MAX) return;
  *banc = trace_ym[i][0]; *reg = trace_ym[i][1]; *val = trace_ym[i][2];
}

#ifndef MD_HORS_CONSOLE
void md_ym_ecrit(int banc, uint8_t reg, uint8_t valeur) {
  if (trace_ym_n >= 0 && trace_ym_n < TRACE_YM_MAX) {
    trace_ym[trace_ym_n][0] = (uint8_t)banc;
    trace_ym[trace_ym_n][1] = reg;
    trace_ym[trace_ym_n][2] = valeur;
    trace_ym_n++;
  }
  md_bus_prend();
  ym_attend();
  if (banc) { YM_A1 = reg; ym_attend(); YM_D1 = valeur; }
  else      { YM_A0 = reg; ym_attend(); YM_D0 = valeur; }
  md_bus_rend();
}
#else
void md_puces_pose_region(int est_pal) { pal = est_pal; }
void md_bus_prend(void) { }
void md_bus_rend(void)  { }
#endif

void md_psg_ecrit(uint8_t octet) { PSG = octet; }

// Le dernier mode de bruit REELLEMENT pose dans la puce. Voir md_psg_bruit :
// le reposer remettrait son registre a decalage a zero.
static int bruit_mode_pose = -1;

// ── Le Z80, et pourquoi on y revient ──────────────────────────────────────
// Sa ligne de reset commande AUSSI celle du YM2612 : la maintenir basse laisse
// la puce FM à l'arrêt. La bonne configuration est bus PRIS et reset RELÂCHÉ —
// le Z80 ne peut pas s'exécuter sans le bus, et la puce vit.
//
// ⚠️ Et il ne suffit PAS de le faire une fois au démarrage. Tout ce qui
// réinitialise le Z80 par la suite — le bouton reset de la console, un cœur
// d'émulation qui applique une option — remet le YM2612 en sommeil, et le son
// ne revient jamais. Mesuré : le simple fait de forcer la région dans
// l'émulateur suffisait à faire disparaître tout le son, définitivement.
// On rétablit donc l'état avant chaque lecture. Ça ne coûte rien.
static int verrou;

#ifndef MD_HORS_CONSOLE
// ── LE COMPTEUR D'EMBOÎTEMENT DOIT ÊTRE ATOMIQUE ──────────────────────────
// ⚠️ CE N'EST PAS UNE PRÉCAUTION THÉORIQUE, C'EST LE DÉFAUT QUI FAISAIT
// GRATTER LE PCM ET PLANTER LA ROM.
//
// md_lecture_tick tourne dans l'interruption vidéo, donc md_pcm_joue aussi.
// Si elle tombe entre `verrou++` et la demande de bus, l'appelant de
// l'interruption voit verrou déjà à 1 : il croit le bus PRIS alors que
// personne ne l'a demandé, et il écrit dans la RAM du Z80 pendant que le Z80
// y lit. On obtient un échantillon en bouillie, et tôt ou tard une écriture
// FM au mauvais moment qui emporte la machine.
//
// Le masque d'interruption tient sur les quelques instructions du compteur et
// de la demande — pas sur la recopie, qui reste interruptible.
static inline uint16_t irq_coupe(void) {
  uint16_t s;
  __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(s) :: "memory");
  return s;
}
static inline void irq_remet(uint16_t s) {
  __asm__ volatile ("move.w %0,%%sr" :: "d"(s) : "memory");
}

void md_bus_prend(void) {
  const uint16_t s = irq_coupe();
  if (verrou++) { irq_remet(s); return; }
  *(volatile uint16_t *)0xA11100 = 0x0100;
  for (int g = 0; g < 10000; g++)
    if (!(*(volatile uint16_t *)0xA11100 & 0x0100)) break;
  irq_remet(s);
}

void md_bus_rend(void) {
  const uint16_t s = irq_coupe();
  if (--verrou > 0) { irq_remet(s); return; }
  verrou = 0;

  // ⚠️ ON REPOSE LE REGISTRE DU CONVERTISSEUR AVANT DE RENDRE LE BUS.
  //
  // Le pilote Z80 écrit en deux temps : l'adresse (0x2A) puis la donnée. Ces
  // deux écritures NE SONT PAS atomiques vis-à-vis du 68000 : quand celui-ci
  // réclame le bus, le Z80 s'arrête à la fin de l'instruction en cours — donc
  // parfois ENTRE les deux. Le 68000 écrit alors ses propres registres FM, ce
  // qui change l'adresse verrouillée dans la puce ; le Z80 reprend et envoie
  // son octet d'échantillon dans un registre de synthèse au hasard.
  //
  // Le résultat n'est pas un silence, c'est un son sale et imprévisible —
  // exactement ce qu'on entendait. En remettant 0x2A avant de rendre le bus,
  // l'octet du Z80 retombe toujours sur le convertisseur.
  if (pcm_branche > 0) { ym_attend(); YM_A0 = 0x2A; }

  *(volatile uint16_t *)0xA11100 = 0x0000;   // le Z80 repart
  irq_remet(s);
}

void md_z80_prepare(void) {
  // On prend le bus pour téléverser le pilote.
  *(volatile uint16_t *)0xA11200 = 0x0000;   // reset : le Z80 se tait
  for (volatile int d = 0; d < 200; d++) { }
  *(volatile uint16_t *)0xA11100 = 0x0100;
  for (int g = 0; g < 10000; g++)
    if (!(*(volatile uint16_t *)0xA11100 & 0x0100)) break;
  *(volatile uint16_t *)0xA11200 = 0x0100;   // hors reset — et le YM2612 avec

  for (int i = 0; i < PILOTE_Z80_TAILLE; i++)
    *(volatile uint8_t *)(0xA00000 + i) = pilote_pcm[i];
  // ⚠️ La banque de l'essai : celle ou commence la banque d'echantillons.
  // Elle est alignee sur 32 Ko, donc 0x8000 vu par le Z80 tombe exactement
  // sur son premier octet.
  *(volatile uint8_t *)0xA00412 = (uint8_t)(((uint32_t)pcm_banque) >> 15);
  *(volatile uint8_t *)0xA00400 = 0;         // état : arrêté

  // Un coup de reset pour qu'il reparte du début de son code, puis on lui rend
  // le bus : à partir de là il tourne pour de bon.
  *(volatile uint16_t *)0xA11200 = 0x0000;
  for (volatile int d = 0; d < 200; d++) { }
  *(volatile uint16_t *)0xA11200 = 0x0100;
  verrou = 0;
  *(volatile uint16_t *)0xA11100 = 0x0000;
}


// ── L'ESSAI DE LECTURE CARTOUCHE ──────────────────────────────────────────
// Ce que le Z80 a lu en 0x8000-0x8002 juste apres son demarrage, une fois la
// banque posee bit a bit. Le 68000 sait ce qui DEVRAIT s'y trouver : c'est la
// seule facon de trancher si le Z80 atteint la cartouche.
void md_z80_essai(uint8_t *lu) {
  md_bus_prend();
  lu[0] = *(volatile uint8_t *)0xA00420;
  lu[1] = *(volatile uint8_t *)0xA00421;
  lu[2] = *(volatile uint8_t *)0xA00422;
  md_bus_rend();
}

// ── Init ───────────────────────────────────────────────────────────────────
void md_puces_init(void) {
  md_z80_prepare();
  pcm_branche = -1;   // force la première écriture du registre 0x2B
  pal = (*(volatile uint8_t *)0xA10001 & 0x40) ? 1 : 0;

  md_ym_ecrit(0, 0x22, 0x00);   // LFO éteint
  md_ym_ecrit(0, 0x27, 0x00);   // pas de mode spécial sur la voie 3
  md_ym_ecrit(0, 0x2B, 0x00);   // convertisseur débranché : les six voies sont FM
  md_puces_silence();
}
#endif   /* MD_HORS_CONSOLE : pas de bus Z80 ni d'init hors console */

void md_puces_silence(void) {
  for (int v = 0; v < 6; v++) {
    md_fm_note_off(v);
    const int banc = v / 3, i = v % 3;
    // Toutes les porteuses au silence : couper la note ne suffit pas si
    // l'extinction est lente, et une voix qui traîne masque la suivante.
    for (int op = 0; op < 4; op++) {
      static const uint8_t dec[4] = {0, 8, 4, 12};
      md_ym_ecrit(banc, (uint8_t)(0x40 + dec[op] + i), 127);
    }
    md_ym_ecrit(banc, (uint8_t)(0xB4 + i), 0xC0);   // gauche et droite
  }
  for (int v = 0; v < 4; v++)
    md_psg_ecrit((uint8_t)(0x9F | (v << 5)));       // atténuation maximale
  bruit_mode_pose = -1;   // le mode de bruit sera reposé à la prochaine note
}

// ── Instruments FM ─────────────────────────────────────────────────────────
// L'ordre des opérateurs dans les REGISTRES n'est pas 1-2-3-4 : le matériel
// les range 1-3-2-4. Se tromper là-dessus donne un timbre plausible mais faux,
// et c'est très difficile à entendre — d'où la table explicite.
static const uint8_t DEC_OP[4] = {0, 8, 4, 12};

// ⚠️ ON NE RALENTIT PAS LES ENVELOPPES ICI, ET C'EST DÉLIBÉRÉ.
//
// Le tracker DS fait tourner sa puce à mi-cadence (MD_YM_DIVISEUR 288, voir
// le commentaire dans nintendo DS/moteur/MegaDrive/md_chip.h) : tout y court
// deux fois moins vite. J'ai essayé de reproduire ça sur Mega Drive en
// retranchant 2 à chaque vitesse d'enveloppe. Écouté sur la console : c'était
// PIRE. Le remède ne vaut pas la maladie, et la Mega Drive garde les vitesses
// que le morceau lui donne.
//
// ⚠️ LE YM2612 NE SE RELIT PAS. Une commande qui ne veut changer qu'une
// moitié de registre — le multiple sans le détune, l'attaque sans l'échelle —
// doit donc savoir ce qu'il y a dedans. On garde le souvenir de ce que la
// dernière voix y a mis, voie par voie et opérateur par opérateur.
static uint8_t vu_detune[6][4], vu_rs[6][4];
// ── DE QUOI RÉGLER LE VOLUME D'UNE VOIE FM ────────────────────────────────
// Le YM2612 n'a pas de registre de volume : on atténue les OPÉRATEURS
// PORTEUSES, et lesquels le sont dépend de l'algorithme. Il faut donc garder
// sous la main le Total Level d'origine de chaque opérateur — sinon chaque
// changement de volume s'ajouterait au précédent et la voix s'éteindrait
// définitivement — et l'algorithme en cours.
static uint8_t vu_tl[6][4], vu_alg[6];

// Bit 0 = OP1 … bit 3 = OP4. Les huit algorithmes du YM2612.
static const uint8_t PORTEUSES[8] = {
  0x8, 0x8, 0x8, 0x8, 0xA, 0xE, 0xE, 0xF
};

// Volume 0-15 vers atténuation 0-63. Reprise de la table du projet DS, prise
// de seize en seize : elle est logarithmique, parce que l'oreille l'est. Une
// rampe droite passait presque tout son chemin dans les trois derniers crans.
static const uint8_t VOL_ATT[16] = {
  63, 16, 12, 9, 8, 6, 5, 4, 4, 3, 2, 2, 1, 1, 0, 0
};

// niveau 0-15, 15 = le volume écrit dans l'instrument.
void md_fm_volume(int voie, uint8_t niveau) {
  if (voie < 0 || voie >= 6) return;
  const uint8_t masque = PORTEUSES[vu_alg[voie] & 7];
  const int sup = ((int)VOL_ATT[niveau & 15] * 127) / 63;
  const int banc = voie / 3, i = voie % 3;
  md_bus_prend();
  for (int op = 0; op < 4; op++) {
    if (!(masque & (1u << op))) continue;
    int tl = (int)vu_tl[voie][op] + sup;
    if (tl > 127) tl = 127;
    md_ym_ecrit(banc, (uint8_t)(0x40 + DEC_OP[op] + i), (uint8_t)tl);
  }
  md_bus_rend();
}
uint8_t md_fm_detune_vu(int voie, int op) {
  return (voie >= 0 && voie < 6 && op >= 0 && op < 4) ? vu_detune[voie][op] : 0;
}
uint8_t md_fm_rs_vu(int voie, int op) {
  return (voie >= 0 && voie < 6 && op >= 0 && op < 4) ? vu_rs[voie][op] : 0;
}

void md_fm_charge(int voie, uint32_t base) {
  md_bus_prend();   // une seule prise pour toute la voix
  const int banc = voie / 3, i = voie % 3;

  for (int op = 0; op < 4; op++) {
    const uint32_t o = base + (uint32_t)op * 11;
    const uint8_t d = DEC_OP[op];
    const uint8_t detune  = md_lit(o + 0) & 7;
    const uint8_t mul     = md_lit(o + 1) & 15;
    const uint8_t tl      = md_lit(o + 2) & 127;
    const uint8_t rs      = md_lit(o + 3) & 3;
    const uint8_t ar      = md_lit(o + 4) & 31;
    const uint8_t d1r     = md_lit(o + 5) & 31;
    const uint8_t d2r     = md_lit(o + 6) & 31;
    const uint8_t d1l     = md_lit(o + 7) & 15;
    const uint8_t rr      = md_lit(o + 8) & 15;
    const uint8_t am      = md_lit(o + 9) & 1;
    const uint8_t ssg     = md_lit(o + 10) & 15;

    if (voie >= 0 && voie < 6) {
      vu_detune[voie][op] = detune; vu_rs[voie][op] = rs;
      vu_tl[voie][op] = tl;   // le TL de RÉFÉRENCE, celui de l'instrument
    }
    md_ym_ecrit(banc, (uint8_t)(0x30 + d + i), (uint8_t)((detune << 4) | mul));
    md_ym_ecrit(banc, (uint8_t)(0x40 + d + i), tl);
    md_ym_ecrit(banc, (uint8_t)(0x50 + d + i),
                (uint8_t)((rs << 6) | ar));
    md_ym_ecrit(banc, (uint8_t)(0x60 + d + i),
                (uint8_t)((am << 7) | d1r));
    md_ym_ecrit(banc, (uint8_t)(0x70 + d + i), d2r);
    md_ym_ecrit(banc, (uint8_t)(0x80 + d + i),
                (uint8_t)((d1l << 4) | rr));
    md_ym_ecrit(banc, (uint8_t)(0x90 + d + i), ssg);
  }

  const uint8_t alg = md_lit(base + 44) & 7;
  const uint8_t fb  = md_lit(base + 45) & 7;
  const uint8_t ams = md_lit(base + 46) & 3;
  const uint8_t pms = md_lit(base + 47) & 7;
  const uint8_t pan = md_lit(base + 50);

  md_ym_ecrit(banc, (uint8_t)(0xB0 + i), (uint8_t)((fb << 3) | alg));
  if (voie >= 0 && voie < 6) vu_alg[voie] = alg & 7;
  // Panoramique : 0 = centre, 1 = gauche, 2 = droite. Une voie sans aucun
  // côté actif est INAUDIBLE — l'erreur classique quand cet octet vaut zéro
  // par accident, et on cherche longtemps une panne de séquenceur.
  const uint8_t cotes = (pan == 1) ? 0x80 : (pan == 2) ? 0x40 : 0xC0;
  md_ym_ecrit(banc, (uint8_t)(0xB4 + i), (uint8_t)(cotes | (ams << 4) | pms));

  // ── Le LFO ────────────────────────────────────────────────────────────
  // ⚠️ Il MANQUAIT, et ça s'entend. AMS et PMS ne font strictement rien tant
  // que le LFO global n'est pas allumé : l'instrument 1 de la démo porte
  // PMS 7 — la profondeur de vibrato MAXIMALE — et sonnait complètement plat.
  //
  // Le LFO du YM2612 est GLOBAL : une seule vitesse pour la puce entière. Un
  // instrument peut donc l'allumer, mais on ne le laisse pas l'ÉTEINDRE —
  // sinon la note suivante, jouée par un instrument sans LFO, couperait le
  // vibrato de tous les autres canaux. Ceux qui ne s'en servent pas ont de
  // toute façon AMS = PMS = 0, donc il ne les affecte pas.
  const uint8_t lfo_on = md_lit(base + 48);
  const uint8_t lfo_hz = md_lit(base + 49) & 7;
  if (lfo_on) md_ym_ecrit(0, 0x22, (uint8_t)(0x08 | lfo_hz));
  md_bus_rend();
}

static uint32_t psg_periode(uint8_t note, int grave);

// ── La hauteur FINE ───────────────────────────────────────────────────────
// ⚠️ Vibrato, portamento et pitch bend ne se règlent PAS en demi-tons : il
// leur faut une hauteur continue. Le YM2612 la donne — son F-Number est un
// nombre, pas une note — mais nos tables ne portent que les douze demi-tons.
// On INTERPOLE donc entre deux voisins.
//
// `fin` est en 256ᵉ de demi-ton, signé. C'est assez fin pour que le vibrato
// le plus lent ne s'entende pas par marches, et assez grossier pour tenir
// dans un entier 16 bits sans multiplication longue sur 68000.
//
// L'interpolation est LINÉAIRE alors que la hauteur est exponentielle. Sur un
// demi-ton l'écart est de moins d'un millième de ton : inaudible, et ça évite
// une exponentielle sur une machine qui n'a pas de virgule flottante.
void md_fm_hauteur(int voie, int note, int fin) {
  if (note < 1) note = 1; else if (note > 108) note = 108;
  // On ramène le désaccord dans [0,256[ en déplaçant la note.
  while (fin < 0)    { fin += 256; note--; }
  while (fin >= 256) { fin -= 256; note++; }
  if (note < 1) { note = 1; fin = 0; }
  if (note > 107) { note = 107; fin = 0; }

  const uint16_t *T = pal ? FNUM_PAL : FNUM_NTSC;
  const int demi = (note - 1) % 12;
  int octave = (note - 1) / 12;
  int32_t f0 = T[demi], f1;
  if (demi == 11) f1 = (int32_t)T[0] * 2;   // le voisin est dans l'octave d'après
  else            f1 = T[demi + 1];
  int32_t fnum = f0 + ((f1 - f0) * fin) / 256;

  int bloc = octave;
  while (fnum > 2047 && bloc < 7) { fnum >>= 1; bloc++; }
  if (bloc > 7) { fnum <<= (bloc - 7); bloc = 7; }
  if (fnum > 2047) fnum = 2047;
  if (fnum < 1) fnum = 1;

  const int banc = voie / 3, i = voie % 3;
  md_ym_ecrit(banc, (uint8_t)(0xA4 + i), (uint8_t)((bloc << 3) | (fnum >> 8)));
  md_ym_ecrit(banc, (uint8_t)(0xA0 + i), (uint8_t)(fnum & 0xFF));
}

// La même chose sur le SN76489, dont le registre est une PÉRIODE : elle
// diminue quand la hauteur monte, d'où l'interpolation à l'envers.
void md_psg_hauteur(int voie, int note, int fin) {
  if (voie < 0 || voie > 2) return;
  if (note < 1) note = 1; else if (note > 108) note = 108;
  while (fin < 0)    { fin += 256; note--; }
  while (fin >= 256) { fin -= 256; note++; }
  if (note < 1) { note = 1; fin = 0; }
  if (note > 107) { note = 107; fin = 0; }

  const uint32_t p0 = psg_periode((uint8_t)note, 0);
  const uint32_t p1 = psg_periode((uint8_t)(note + 1), 0);
  int32_t p = (int32_t)p0 + (((int32_t)p1 - (int32_t)p0) * fin) / 256;
  if (p > 1023) p = 1023;
  if (p < 1) p = 1;
  md_psg_ecrit((uint8_t)(0x80 | (voie << 5) | (p & 0x0F)));
  md_psg_ecrit((uint8_t)((p >> 4) & 0x3F));
}

// La HAUTEUR seule, sans relancer l'enveloppe. C'est ce dont une table a
// besoin : un arpège change de note à chaque tick, et réattaquer à chaque
// fois ne donnerait qu'un grésillement.
void md_fm_frequence(int voie, uint8_t note) {
  if (!note || note == MD_VIDE) return;
  const int banc = voie / 3, i = voie % 3;
  const int demi = (note - 1) % 12;
  uint16_t fnum = (pal ? FNUM_PAL : FNUM_NTSC)[demi];
  int bloc = (note - 1) / 12;
  if (bloc > 7) { fnum <<= (bloc - 7); bloc = 7; if (fnum > 2047) fnum = 2047; }
  md_ym_ecrit(banc, (uint8_t)(0xA4 + i), (uint8_t)((bloc << 3) | (fnum >> 8)));
  md_ym_ecrit(banc, (uint8_t)(0xA0 + i), (uint8_t)(fnum & 0xFF));
}

// ── Retouches en cours de jeu ─────────────────────────────────────────────
// ⚠️ Elles ne rechargent PAS la voix. Une commande posée au milieu d'une note
// doit la modifier, pas la relancer : recharger réattaquerait l'enveloppe et
// on n'entendrait qu'un hoquet. Un registre, une valeur, rien d'autre.
//
// ⚠️ Et elles ne touchent PAS l'instrument. Une commande vaut pour la note en
// cours ; la note suivante recharge la voix et efface l'effet. C'est ce qu'on
// veut — sinon une commande abîmerait le patch pour tout le morceau.
void md_fm_pose_alg_fb(int voie, uint8_t alg, uint8_t fb) {
  // Changer d'algorithme change QUI est porteuse : le volume suivant doit
  // atténuer les bons opérateurs.
  if (voie >= 0 && voie < 6) vu_alg[voie] = alg & 7;
  const int banc = voie / 3, i = voie % 3;
  md_ym_ecrit(banc, (uint8_t)(0xB0 + i), (uint8_t)(((fb & 7) << 3) | (alg & 7)));
}

void md_fm_pose_tl(int voie, int op, uint8_t tl) {
  if (op < 0 || op > 3) return;
  // Un TL posé à la main devient la nouvelle référence : sinon le prochain
  // réglage de volume le remplacerait par celui de l'instrument.
  if (voie >= 0 && voie < 6) vu_tl[voie][op] = tl & 127;
  const int banc = voie / 3, i = voie % 3;
  md_ym_ecrit(banc, (uint8_t)(0x40 + DEC_OP[op] + i), (uint8_t)(tl & 127));
}

void md_fm_pose_mul(int voie, int op, uint8_t mul) {
  if (op < 0 || op > 3) return;
  const int banc = voie / 3, i = voie % 3;
  // ⚠️ Le détune partage ce registre : on le RELIT pour ne pas l'écraser.
  // Le YM2612 ne se lit pas, mais le tracker sait ce qu'il y a écrit — il
  // vient de charger la voix.
  const uint8_t det = md_fm_detune_vu(voie, op);
  md_ym_ecrit(banc, (uint8_t)(0x30 + DEC_OP[op] + i),
              (uint8_t)((det << 4) | (mul & 15)));
}

void md_fm_pose_ar(int voie, int op, uint8_t ar) {
  if (op < 0 || op > 3) return;
  const int banc = voie / 3, i = voie % 3;
  const uint8_t rs = md_fm_rs_vu(voie, op);
  md_ym_ecrit(banc, (uint8_t)(0x50 + DEC_OP[op] + i),
              (uint8_t)((rs << 6) | (ar & 31)));
}

void md_fm_pose_pan(int voie, uint8_t pan, uint8_t ams, uint8_t pms) {
  const int banc = voie / 3, i = voie % 3;
  const uint8_t cotes = (pan == 1) ? 0x80 : (pan == 2) ? 0x40 : 0xC0;
  md_ym_ecrit(banc, (uint8_t)(0xB4 + i),
              (uint8_t)(cotes | ((ams & 3) << 4) | (pms & 7)));
}

void md_fm_pose_lfo(int marche, uint8_t vitesse) {
  md_ym_ecrit(0, 0x22, (uint8_t)(marche ? (0x08 | (vitesse & 7)) : 0));
}

void md_fm_note_on(int voie, uint8_t note) {
  if (!note || note == MD_VIDE) return;
  const int banc = voie / 3, i = voie % 3;
  const int demi = (note - 1) % 12;
  int octave = (note - 1) / 12;

  // ⚠️ LE BLOC EST L'OCTAVE, directement. Première version : `octave - 1`,
  // et tout sonnait UNE OCTAVE TROP BAS. Le calcul le dit sans ambiguïté —
  //     f = fnum · 2^(bloc-1) · horloge / (144 · 2^20)
  // donc fnum 644 au bloc 4 donne 261,7 Hz, soit exactement le C-4 du
  // tracker. Au bloc 3 on obtenait 130,9 Hz : le do du dessous.
  uint16_t fnum = (pal ? FNUM_PAL : FNUM_NTSC)[demi];
  int bloc = octave;
  // L'octave 8 déborde du bloc 7 : on double le F-Number à la place, ce que
  // ses onze bits permettent encore.
  if (bloc > 7) { fnum <<= (bloc - 7); bloc = 7; if (fnum > 2047) fnum = 2047; }

  const uint8_t cle = (uint8_t)(i + (banc ? 4 : 0));
  md_ym_ecrit(0, 0x28, cle);                       // couper avant de réattaquer

  // ⚠️ Laisser à la puce le temps de VOIR la coupure. Le moteur de référence
  // attend quatre échantillons — 75 µs — et explique pourquoi : sans ce délai,
  // l'enveloppe ne se réarme pas et la note ne réattaque jamais vraiment. On
  // enchaînait les deux écritures.
  for (volatile int d = 0; d < 120; d++) { }
  // Le poids FORT d'abord : le YM2612 ne prend la hauteur en compte qu'à
  // l'écriture du registre BAS. L'inverse donne un glissando parasite.
  md_ym_ecrit(banc, (uint8_t)(0xA4 + i), (uint8_t)((bloc << 3) | (fnum >> 8)));
  md_ym_ecrit(banc, (uint8_t)(0xA0 + i), (uint8_t)(fnum & 0xFF));
  md_ym_ecrit(0, 0x28, (uint8_t)(0xF0 | cle));     // les quatre opérateurs
}

void md_fm_note_off(int voie) {
  const int banc = voie / 3, i = voie % 3;
  md_ym_ecrit(0, 0x28, (uint8_t)(i + (banc ? 4 : 0)));
}

// ── La voie PCM ───────────────────────────────────────────────────────────
#ifndef MD_HORS_CONSOLE
void md_pcm_actif(int actif) {
  if (actif == pcm_branche) return;
  pcm_branche = actif;
  if (actif) {
    // On coupe la note FM de la voie avant de la débrancher : sinon son
    // enveloppe continue de sonner sous l'échantillon.
    md_fm_note_off(MD_PCM_VOIE);
    md_ym_ecrit(0, 0x2B, 0x80);
    md_ym_ecrit(0, 0x2A, 0x80);   // milieu d'échelle = silence
  } else {
    md_ym_ecrit(0, 0x2B, 0x00);
  }
}

void md_pcm_octet(uint8_t v) { md_ym_ecrit(0, 0x2A, v); }

// ⚠️ Le registre de banque du Z80 n'est PLUS utilisé : la cartouche ne répond
// pas aux lectures qu'il initie (voir plus bas). Le code est retiré plutôt que
// laissé en sommeil — un chemin mort finit toujours par être repris par erreur.

// Le pas de lecture, en virgule fixe 8 bits : 256 = vitesse d'origine.
//
// On ne ralentit PAS les ecritures vers le convertisseur pour descendre la
// note — ce serait echanger la hauteur contre la qualite. On garde la cadence
// de sortie et on parcourt l'echantillon plus ou moins vite.
//
// Le pas modifie lui-meme la duree de la boucle Z80 : chaque avancee d'un
// octet coute 7 cycles, et l'avancee moyenne vaut pas/256, donc la periode
// vaut exactement 114 + 7*pas/256 cycles. C'est lineaire, donc inversible
// sans approximation : on resout le pas qui donne le rapport voulu APRES
// cet effet en retour. Sans ca l'echelle se tasse dans les aigus.
// ⚠️ Recalculé depuis que `de` compte les SORTIES : la decrementation est
// sortie des branches, donc chaque avancee d'un octet ne coute plus que 1
// cycle au lieu de 7, et le corps de boucle en vaut 120. La periode reste
// exactement lineaire — 120 + pas/256 — donc toujours inversible sans
// approximation.
// ── LA CADENCE DU CONVERTISSEUR ───────────────────────────────────────────
// ⚠️ La boucle du pilote dure maintenant un temps CONSTANT : depuis que
// l'avance se fait par add/adc, elle ne depend plus du pas. Plus d'equation
// implicite a resoudre — le pas est une simple regle de trois.
// ⚠️ MESURE, PAS COMPTAGE. J'avais compte 131 a la main ; la duree reelle
// d'une audition donne 142. Onze cycles d'ecart, soit un echantillon joue une
// bonne demi-note trop bas. On mesure : on chronometre une audition dont on
// connait le nombre de sorties, et on en deduit la boucle.
#define BOUCLE_Z80  142         /* cycles par sortie, MESURES */
// La cadence a laquelle les echantillons du .mdm sont ecrits : 3546893/121.
// Elle vient du tracker DS et ne se decide pas ici.
#define CAD_BANQUE  121

static const uint16_t RATIO[12] = {   /* 256 * 2^(n/12) */
  256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483
};

static uint16_t pas_lecture(int demi_tons) {
  int oct = demi_tons / 12, reste = demi_tons % 12;
  if (reste < 0) { reste += 12; oct--; }
  int32_t r = RATIO[reste];
  if (oct > 0) { if (oct > 4) oct = 4; r <<= oct; }
  else { while (oct++ < 0) { r >>= 1; if (!r) { r = 1; break; } } }
  // La boucle sort un echantillon toutes les BOUCLE_Z80 periodes ; la banque
  // est ecrite pour CAD_BANQUE. Le pas est le rapport des deux, en 8.8.
  // ⚠️ Plus de plafond a sept demi-tons : la partie entiere du pas est un
  // octet plein depuis que le pilote avance par adc. C'est ce plafond qui
  // figeait la machine un octave au-dessus de la note de base.
  int32_t pas = r * BOUCLE_Z80 / CAD_BANQUE;
  if (pas < 1) pas = 1;
  if (pas > 0xFFFF) pas = 0xFFFF;
  return (uint16_t)pas;
}

// La commande TOURNE, ce n'est pas un drapeau. Voir pilote_pcm.z80 : pendant
// qu'on tient le bus le Z80 est arrete, donc un passage a 0 puis a 1 lui
// serait invisible et il poursuivrait l'echantillon precedent.
static uint8_t pcm_commande;
uint8_t pcm_commande_vue(void) { return pcm_commande; }

// Ce qu'on a demande la derniere fois, garde pour le mouchard.
static uint32_t dern_adresse;
static uint16_t dern_banque, dern_ptr, dern_len, dern_pas;

// ── LE Z80 LIT L'ÉCHANTILLON DANS LA CARTOUCHE ───────────────────────────
// ⚠️ ET C'EST CE QUI CHANGE TOUT. On avait conclu que le Z80 n'obtenait pas
// les données de la cartouche à travers sa fenêtre de banque, et bâti sur
// cette conclusion un anneau de 4 Ko que le 68000 réapprovisionnait. C'était
// faux, et le prix en était double : 0,17 seconde d'échantillon utile, et un
// hoquet à chaque réapprovisionnement — écrire la RAM du Z80 ARRÊTE le Z80,
// donc chaque octet recopié était du son qui ne sortait pas.
//
// La vraie cause du relevé fautif : le registre de banque en 0x6000 est un
// REGISTRE À DÉCALAGE. Il prend neuf écritures, un bit chacune. Écrit comme un
// octet ordinaire il n'en retient qu'un, la fenêtre pointe ailleurs, et les
// données reviennent fausses — exactement le symptôme.
//
// Vérifié au démarrage, et le journal l'écrit à chaque fois :
//     Z80 LIT ROM : 7F 80 80   ATTENDU 7F 80 80   -> OUI
//
// Le 68000 ne touche donc plus à rien pendant la lecture. Aucune longueur
// maximale : le pilote enchaîne les banques tout seul. C'est ainsi que les
// jeux Mega Drive jouent une seconde de voix sans coupure.

// ⚠️ Le bloc de commande vit en 0x0400 : DERRIERE le code du pilote, avec de
// la marge. La verification ci-dessous n'est pas decorative — le jour ou le
// pilote a depasse 0x0080, le 68000 s'est mis a ecrire dans son code.
// ⚠️ Le bloc de commande vit en 0x0400 : DERRIERE le code du pilote, avec de
// la marge. La verification ci-dessous n'est pas decorative — le jour ou le
// pilote a depasse 0x0080, le 68000 s'est mis a ecrire dans son code.
#define PCM_COMMANDE   0x0400
#define PCM_GAIN       0x0500   /* la table de volume, alignee sur une page */
typedef char md_verif_pilote[(PILOTE_Z80_TAILLE < PCM_COMMANDE) ? 1 : -1];

// ── LE VOLUME DU PCM ──────────────────────────────────────────────────────
// ⚠️ 0x7F EST L'UNITE, PAS LE MAXIMUM. C'est la regle du projet DS, ou le
// volume PCM avait ete explicitement releve : au-dela de 7F le son est
// vraiment pousse plus fort, avec ecretage, jusqu'au double a FF.
//
// La table vit dans la RAM du Z80, qui la consulte a chaque octet. On ne la
// reecrit QUE si le volume a change : la reposer arrete le Z80 le temps de
// 256 ecritures, et ce serait un trou dans le son a chaque note.
static int gain_pose = -1;

static void gain_prepare(int vol) {
  if (vol < 0) vol = 0;
  if (vol > 255) vol = 255;
  if (vol == gain_pose) return;
  gain_pose = vol;
  volatile uint8_t *t = (volatile uint8_t *)(0xA00000 + PCM_GAIN);
  for (int k = 0; k < 256; k++) {
    int centre = k - 128;
    int v = 128 + ((centre * vol * 516) >> 16);   /* 516/65536 = 1/127 */
    if (v < 0) v = 0;            /* l'ecretage est voulu : c'est le son pousse */
    if (v > 255) v = 255;
    t[k] = (uint8_t)v;
  }
}

void md_pcm_joue(uint32_t adresse, uint32_t longueur, int demi_tons, int volume) {
  if (!longueur) return;
  const uint16_t pas = pas_lecture(demi_tons);
  // ⚠️ On donne au Z80 un nombre de SORTIES, pas d'octets : c'est ce qu'il
  // decompte, un par tour. Convertir ici evite qu'il ait a diviser.
  uint32_t sorties = (longueur * 256u) / pas;
  if (!sorties) sorties = 1;
  if (sorties > 0xFFFFFF) sorties = 0xFFFFFF;

  // ⚠️ LA BANQUE ET LA FENETRE. Le Z80 voit 32 Ko a la fois, en 0x8000. La
  // banque est le numero de ce bloc de 32 Ko dans l'espace du 68000 ; le
  // pointeur est le reste. Le pilote enchaine les banques tout seul quand
  // l'echantillon en depasse une.
  const uint8_t banque = (uint8_t)(adresse >> 15);
  const uint16_t ptr = (uint16_t)(0x8000 + (adresse & 0x7FFF));

  md_pcm_actif(1);
  md_bus_prend();
  gain_prepare(volume);
  *(volatile uint8_t *)0xA00412 = banque;
  *(volatile uint8_t *)0xA00402 = (uint8_t)(ptr & 0xFF);
  *(volatile uint8_t *)0xA00403 = (uint8_t)(ptr >> 8);
  *(volatile uint8_t *)0xA00404 = (uint8_t)(sorties & 0xFF);
  *(volatile uint8_t *)0xA00405 = (uint8_t)((sorties >> 8) & 0xFF);
  *(volatile uint8_t *)0xA00411 = (uint8_t)(sorties >> 16);
  *(volatile uint8_t *)(0xA00000 + PILOTE_FRAC) = (uint8_t)(pas & 0xFF);
  *(volatile uint8_t *)(0xA00000 + PILOTE_ENT)  = (uint8_t)(pas >> 8);
  if (++pcm_commande == 0) pcm_commande = 1;
  *(volatile uint8_t *)0xA00400 = pcm_commande;
  dern_adresse = adresse; dern_banque = banque;
  dern_ptr = ptr; dern_len = (uint16_t)longueur; dern_pas = pas;
  md_bus_rend();
}

// Plus rien a entretenir : le Z80 lit la cartouche lui-meme.
uint8_t md_pcm_etat(void) { return 0; }

void md_pcm_arrete(void) {
  md_bus_prend();
  *(volatile uint8_t *)0xA00400 = 0;
  md_bus_rend();
  md_ym_ecrit(0, 0x2A, 0x80);   // milieu d'echelle : sans ca la voie garde le
                                // dernier octet et laisse un souffle continu
}


// ── Le mouchard du PCM ────────────────────────────────────────────────────
// On relit DANS la mémoire du Z80 ce qu'on lui a écrit, plus ce que lui y a
// laissé : où il s'est arrêté, ce qui lui restait, combien de fois il a
// commencé et fini. C'est l'instrument de mesure — sans lui on en est réduit
// à écouter et à supposer, ce qui a déjà coûté trois diagnostics faux.
void md_pcm_bilan(md_pcm_bilan_t *b) {
  b->adresse = dern_adresse;  b->banque = dern_banque;
  b->pointeur = dern_ptr;     b->longueur = dern_len;  b->pas = dern_pas;
  md_bus_prend();
  b->commande    = *(volatile uint8_t *)0xA00400;
  b->ptr_relu_lo = *(volatile uint8_t *)0xA00402;
  b->ptr_relu_hi = *(volatile uint8_t *)0xA00403;
  b->frac_relu   = *(volatile uint8_t *)(0xA00000 + PILOTE_FRAC);
  b->ent_relu    = *(volatile uint8_t *)(0xA00000 + PILOTE_ENT);
  b->fin_hl      = (uint16_t)(*(volatile uint8_t *)0xA00408)
                 | (uint16_t)(*(volatile uint8_t *)0xA00409 << 8);
  b->reste_de    = (uint16_t)(*(volatile uint8_t *)0xA0040A)
                 | (uint16_t)(*(volatile uint8_t *)0xA0040B << 8);
  b->finis       = *(volatile uint8_t *)0xA0040C;
  b->commences   = *(volatile uint8_t *)0xA0040D;
  b->dernier_lu  = *(volatile uint8_t *)0xA0040E;
  b->premier_lu  = *(volatile uint8_t *)0xA0040F;
  md_bus_rend();
}

#endif   /* MD_HORS_CONSOLE : la voie PCM tient entierement au Z80 */

// ── PSG ────────────────────────────────────────────────────────────────────
// La période du SN76489 pour une note. `grave` la descend d'une octave.
static uint32_t psg_periode(uint8_t note, int grave) {
  const int demi = (note - 1) % 12;
  int octave = (note - 1) / 12;
  if (octave < 0) octave = 0;
  if (octave > 9) octave = 9;
  // Huit crans de décimales, plus une octave par cran. `grave` en retire un :
  // c'est l'octave que le générateur de bruit gagne à période égale.
  int d = octave + 8 - (grave ? 1 : 0);
  uint32_t p = ((pal ? PSG_P256_PAL : PSG_P256_NTSC)[demi]
                + (1u << (d - 1))) >> d;
  if (p > 1023) p = 1023;   // la puce sature : les graves se confondent
  if (p < 1) p = 1;
  return p;
}

// La derniere periode ecrite, pour le banc d'essai : c'est le seul moyen de
// verifier l'accord du PSG sans console et sans recopier la formule ailleurs.
static uint32_t derniere_periode;
uint32_t md_periode_vue(void) { return derniere_periode; }

void md_psg_note_on(int voie, uint8_t note, uint8_t volume) {
  if (voie < 0 || voie > 2) return;
  if (volume > 15) volume = 15;
  // Le registre est une ATTÉNUATION : 0 est le maximum, 15 le silence. On
  // raisonne en volume partout ailleurs, donc on inverse ici et une seule fois.
  const uint8_t att = (uint8_t)(15 - volume);
  if (!note || note == MD_VIDE) return;

  const uint32_t p = psg_periode(note, 0);
  derniere_periode = p;
  md_psg_ecrit((uint8_t)(0x80 | (voie << 5) | (p & 0x0F)));
  md_psg_ecrit((uint8_t)((p >> 4) & 0x3F));
  md_psg_ecrit((uint8_t)(0x90 | (voie << 5) | att));
}

// ── La voie de bruit ──────────────────────────────────────────────────────
// ⚠️ ELLE PREND UNE NOTE, et c'est tout l'enjeu.
//
// Le SN76489 n'offre que trois périodes FIXES pour son bruit, plus un mode
// « verrouillé sur le ton 3 ». Dans les trois premières, la note écrite ne
// change strictement rien — c'est la puce, pas nous. Seuls les modes 3 et 7
// (bits 0-1 à 3, périodique ou blanc) suivent une hauteur, et ils la lisent
// dans le registre de PÉRIODE DE LA VOIE PSG3, qu'il faut donc écrire.
//
// C'est ce qui manquait : on envoyait le mode et on jetait la note, si bien
// qu'aucun mode ne réagissait à la hauteur.
//
// ⚠️ Conséquence matérielle assumée : dans ces deux modes, la colonne PSG3
// partage son registre de période avec le bruit.
//
// Une octave PLUS BAS que les voies de ton : à période égale le bruit
// s'entend une octave au-dessus, mesuré sur la DS contre DefleMask.
// ⚠️ ÉCRIRE LE REGISTRE DE BRUIT REMET SON REGISTRE À DÉCALAGE À ZÉRO.
// C'est le piège du SN76489 : on le réécrivait à CHAQUE TICK, donc la suite
// pseudo-aléatoire repartait cinquante fois par seconde du même point. Le
// résultat n'est plus un souffle, c'est un motif qui se répète — un son de
// synthèse, exactement ce qu'on entendait, et sur toutes les voies de bruit.
//
// On ne le repose donc QUE s'il change. C'est ce que fait le tracker DS, et
// son commentaire le dit noir sur blanc : « la macro continue d'avancer, mais
// ne pose rien tant qu'une commande tient le mode ».
//
// La période et l'atténuation, elles, se réécrivent librement : elles ne
// touchent pas au registre à décalage.
void md_psg_bruit(uint8_t mode, uint8_t note, uint8_t volume) {
  if (volume > 15) volume = 15;
  const uint8_t att = (uint8_t)(15 - volume);
  mode &= 7;

  if ((mode & 3) == 3 && note && note != MD_VIDE) {
    const uint32_t p = psg_periode(note, 1);
    derniere_periode = p;
    md_psg_ecrit((uint8_t)(0x80 | (2 << 5) | (p & 0x0F)));
    md_psg_ecrit((uint8_t)((p >> 4) & 0x3F));
  }
  if ((int)mode != bruit_mode_pose) {
    bruit_mode_pose = (int)mode;
    md_psg_ecrit((uint8_t)(0xE0 | mode));
  }
  md_psg_ecrit((uint8_t)(0xF0 | att));
}

void md_psg_note_off(int voie) {
  if (voie < 0 || voie > 3) return;
  md_psg_ecrit((uint8_t)(0x9F | (voie << 5)));
}
