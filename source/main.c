// ============================================================================
//  GeneTrackerMD — un tracker de musique qui tourne sur la Sega Mega Drive.
//  Copyright (C) 2026 Audren Thibault
//  https://github.com/AudrenThibault/NativeMegadriveTracker
//
//  Ce programme est un logiciel libre : vous pouvez le redistribuer et le
//  modifier selon les termes de la GNU General Public License, version 3,
//  telle que publiée par la Free Software Foundation.
//
//  Il est distribué dans l'espoir qu'il sera utile, mais SANS AUCUNE GARANTIE,
//  sans même la garantie implicite de QUALITÉ MARCHANDE ou d'ADÉQUATION À UN
//  USAGE PARTICULIER. Voir la GNU General Public License pour les détails.
//
//  Vous devriez avoir reçu une copie de la licence avec ce programme : c'est
//  le fichier LICENSE. Sinon : https://www.gnu.org/licenses/
//
//  ⚠️ TERME ADDITIONNEL, AU TITRE DE L'ARTICLE 7(b) DE LA GPL v3 :
//  vous devez conserver, dans le code source ET dans les avis légaux affichés
//  par le programme (la page ABOUT), la mention de l'auteur « Audren Thibault »
//  et l'adresse du dépôt d'origine ci-dessus.
//
//  L'article 5(d) impose déjà qu'une version modifiée continue d'afficher les
//  avis légaux ; l'article 7(b) y ajoute le nom et le lien. Voir APROPOS[] plus
//  bas — on peut y ajouter des lignes, on n'en retire pas.
// ============================================================================
// ============================================================================
//  GeneTracker — un tracker qui tourne sur la Mega Drive.
//
//  La chaîne de navigation de LSDJ : SONG donne des chains, un chain donne des
//  phrases, une phrase donne des notes. On descend avec A+droite, on remonte
//  avec A+gauche — et on REFUSE de descendre dans une case vide, comme LSDJ :
//  passer bêtement à l'écran suivant afficherait toujours le chain 00.
//
//  Le morceau vit dans la SRAM et s'édite en place. Mais cette cartouche n'a
//  pas de pile (voir md_song.h), d'où le rappel permanent en bas d'écran.
// ============================================================================
#include <stdint.h>

#include "md_ecran.h"
#include "md_manette.h"
#include "md_song.h"
#include "md_codec.h"
#include "md_lecture.h"
#include "md_commandes.h"
#include "md_puces.h"
#include "banque_pcm.h"
#include "morceaux_rom.h"

enum { PAGE_APROPOS = 7 };
enum { PAGE_SONG = 0, PAGE_CHAIN = 1, PAGE_PHRASE = 2,
       PAGE_INSTR = 3, PAGE_TABLE = 4, PAGE_PROJECT = 5,
       PAGE_FICHIER = 6 };

// ⚠️ TROIS RANGÉES DE RESPIRATION EN HAUT, ET CHACUNE SERT.
//   0  le titre de la page, seul — le mot « GENETRACKER » a disparu, il
//      n'apprenait rien et volait la place aux intitulés de colonne.
//   1  vide : sépare le titre des intitulés.
//   2  les intitulés de colonne.
//   3  vide : sépare les intitulés de la ligne 00.
//   4  la grille.
#define LIG_ENTETE   2
#define LIG_PREMIERE 4
#define LIGNES_VUES  22
#define COL_CANAL(i) (3 + (i) * 3)

// Le ton d'un numéro de ligne. LSDJ alterne deux teintes PAR GROUPES DE
// QUATRE : c'est ce qui donne le rythme à l'œil sur deux cent cinquante-six
// lignes, et sans quoi on les compte une par une. C'est la règle de la DS
// (numeroLigne : `((idx / 4) % 2) == 0`).
static uint16_t ton_ligne(int l) {
  // Un groupe sur deux porte le PAVÉ, l'autre reste sur le noir de l'écran
  // avec une encre plus sourde. Le pavé est ce qui se voit de loin.
  return ((l / 4) % 2) ? (uint16_t)(MD_NUM | MD_TON2)
                       : (uint16_t)(MD_NUM | MD_FOND2);
}

static const char *NOMS_CANAUX[MD_CANAUX] = {
  // ⚠️ « PC », pas « F6 ». Dans ce tracker la sixième voie FM est TOUJOURS
  // le convertisseur : l'appeler F6 laissait croire qu'on pouvait y poser de
  // la synthèse comme sur les cinq autres.
  "F1","F2","F3","F4","F5","PC","P1","P2","P3","NO"
};
static const char NOTES[12][3] = {
  {'C','-',0},{'C','#',0},{'D','-',0},{'D','#',0},{'E','-',0},{'F','-',0},
  {'F','#',0},{'G','-',0},{'G','#',0},{'A','-',0},{'A','#',0},{'B','-',0}
};

static int page = PAGE_SONG;
static int redessiner = 1;

// SONG
static int song_canal, song_ligne, song_haut;
static int song_ancien_canal, song_ancienne_ligne;
static int instr_ancienne_ligne;
static uint8_t dernier_chain;

// CHAIN — l'identifiant vient de la case SONG d'où l'on a plongé.
static int voie_courante;   // la colonne SONG d'où l'on est descendu
static int chain_id, chain_ligne, chain_col;   // col 0 = phrase, 1 = transposition
static uint8_t derniere_phrase;

// PHRASE
static int phrase_id, phrase_ligne, phrase_col;  // 0..6
// ⚠️ UNE dernière note PAR VOIE, et C-4 quand la voie n'a rien joué.
// Une seule mémoire pour tout le morceau faisait qu'une phrase neuve
// s'ouvrait sur la hauteur touchée en dernier N'IMPORTE OÙ : on posait une
// note sur une voie vierge et il en sortait celle de la voie d'à côté.
// Zéro veut dire « cette voie n'a encore rien joué », comme pour
// dernier_instr_voie juste en dessous.
#define NOTE_C4 49
static uint8_t derniere_note_voie[MD_CANAUX];

// ⚠️ UN dernier instrument PAR VOIE, pas un pour tout le morceau.
// Une voie porte un timbre : reposer une note sur la colonne de basse doit
// reprendre la basse, pas l'instrument qu'on vient de choisir sur la caisse
// claire d'à côté. Zéro veut dire « cette voie n'a encore rien joué ».
static uint8_t dernier_instr_voie[MD_CANAUX];

static uint8_t instr_voie_courante(void);

// INSTR et TABLE
static int instr_id = 1, instr_ligne, instr_col;
static int table_id, table_ligne, table_col;
static int projet_ligne;
// ⚠️ DEUX retours distincts. Une seule variable servait aux deux, si bien
// qu'entrer dans FILE depuis PROJECT écrasait la page d'où l'on venait : on se
// retrouvait ENFERMÉ dans PROJECT, A+bas y ramenant indéfiniment.
static int retour_projet = PAGE_SONG;    // la page d'avant PROJECT
static int retour_fichier = PAGE_PROJECT; // la page d'avant FILE

// Double appui sur C : crée un élément neuf plutôt que de reprendre le dernier.
static int der_case = -1, der_age = 9999;

// ── Les paramètres d'un opérateur FM ──────────────────────────────────────
// Un tableau plutôt que onze cas particuliers : l'affichage, le curseur et
// l'édition lisent tous le même, donc ils ne peuvent pas diverger.

// ── Le TYPE d'un instrument suit sa COLONNE ───────────────────────────────
// Règle reprise mot pour mot de la DS. La page dépend de la colonne d'où l'on
// est descendu, pas de l'instrument : un même numéro posé sur une colonne FM
// ou PSG ne pilote pas la même puce, et on ne montre que ce qui agit.
enum { GENRE_FM = 0, GENRE_PCM, GENRE_PSG, GENRE_BRUIT };

static int instr_voie = 0;

static int genre_instr(int voie, int ins) {
  if (voie == 9) return GENRE_BRUIT;
  if (voie >= 6) return GENRE_PSG;
  if (voie == MD_PCM_VOIE && ins &&
      md_lit(MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS + 60) != 1)
    return GENRE_PCM;
  return GENRE_FM;
}

// ── Comment une valeur s'affiche ──────────────────────────────────────────
// Reprise de la DS, où chaque réglage se montre sous la forme qui se lit : une
// note s'écrit « C-4 » et pas 49, une sortie montre ses trois positions L C R
// dont une seule est allumée, une table absente dit « OFF ».
enum { REND_NOMBRE = 0, REND_NOTE, REND_LCR, REND_TABLE, REND_ECH,
       REND_ENV, REND_BOUCLE, REND_MAC_VOL, REND_MAC_ARP, REND_BRUIT };

// ── Les macros à l'écran ──────────────────────────────────────────────────
// La DS n'en montre que seize pas ; ici la fenêtre SUIT LE CURSEUR, sinon un
// instrument dont la macro fait trente pas aurait la moitié de son dessin
// hors d'atteinte. Un chiffre par pas pour le volume (0-F), deux pour
// l'arpège (des demi-tons signés).
#define MAC_VUE_VOL 16
#define MAC_VUE_ARP 12

// inv : la puce range une ATTÉNUATION là où l'intitulé annonce un NIVEAU, ou
// une VITESSE là où il annonce une DURÉE. On retourne alors la valeur à
// l'affichage ET à l'édition, sinon le réglage marche à l'envers — et, buté
// contre sa borne, il paraît simplement mort.
//
// ⚠️ LES QUATRE TEMPS DE L'ENVELOPPE SE LISENT TOUS COMME DES DURÉES.
// La puce range des VITESSES pour l'attaque, le déclin, le déclin de maintien
// et l'extinction : plus le nombre est grand, plus c'est rapide. On les
// retourne donc tous les quatre, et on les nomme comme des durées — monter la
// valeur allonge le son. « SUSTAIN RATE » était le seul à échapper à la règle,
// et il marchait donc à l'envers des trois autres ; il s'appelle maintenant
// SUSTAIN DECAY, comme DECAY et RELEASE à côté de lui.
//
// Les autres ne s'inversent PAS, et il y a une raison à chaque fois :
//   MULTIPLIER, DETUNE   des nombres, pas des vitesses
//   RATE SCALING         une QUANTITÉ de mise à l'échelle, pas une durée
//   AM ENABLE            un drapeau
//   SSG-EG               un numéro de mode
//   OUTPUT LEVEL         atténuation dans la puce, niveau à l'écran → inversé
//   SUSTAIN LEVEL        idem : 0 dans la puce = maintien le plus fort
typedef struct {
  const char *nom; uint8_t dec; uint8_t maxi; uint8_t rendu; uint8_t inv;
} champ_t;

static const champ_t CHAMPS_PCM[] = {
  {"SAMPLE",    61, 31,  REND_ECH, 0},
  {"BASE NOTE",  0, 0,   REND_NOTE, 0},
  {"LOOP",       0, 0,   REND_BOUCLE, 0},
  // ⚠️ Jusqu'a FF, comme sur la DS : 255 est la pleine echelle et 127
  // la moitie. La borne a 127 empechait de monter le son.
  {"VOLUME",    62, 255, REND_NOMBRE, 0},
  {"OUTPUT",    50, 2,   REND_LCR, 0},
  {"TABLE",     59, MD_MAX_TABLES, REND_TABLE, 0},
};
// ⚠️ L'ORDRE EST CELUI DE LA PAGE PSG DE LA DS, ligne pour ligne : ENV,
// OUTPUT, FINETUNE, [NOISE], les deux macros, puis TABLE tout en bas. NOISE
// MODE était placé en deuxième ici, il descend à sa place.
//
// Les macros manquaient entièrement : un instrument PSG venu du tracker DS
// arrivait sans son enveloppe, donc plat — c'est ce qu'on entendait.
static const champ_t CHAMPS_PSG[] = {
  {"ENV",       53, 15,  REND_ENV, 0},
  {"OUTPUT",    50, 2,   REND_LCR, 0},
  {"FINETUNE",  51, 127, REND_NOMBRE, 0},
  {"VOL LENGTH",MD_OFF_VOL_LEN,    MD_MACRO_VOL_PAS, REND_NOMBRE, 0},
  {"VOL LOOP",  MD_OFF_VOL_BOUCLE, 255, REND_NOMBRE, 0},
  {"VOL MACRO", MD_OFF_VOL_MAC,    15,  REND_MAC_VOL, 0},
  {"ARP LENGTH",MD_OFF_ARP_LEN,    MD_MACRO_ARP_PAS, REND_NOMBRE, 0},
  {"ARP LOOP",  MD_OFF_ARP_BOUCLE, 255, REND_NOMBRE, 0},
  {"ARP FIXED", MD_OFF_ARP_FIXE,   1,   REND_NOMBRE, 0},
  {"ARP MACRO", MD_OFF_ARP_MAC,    255, REND_MAC_ARP, 0},
  {"TABLE",     59, MD_MAX_TABLES, REND_TABLE, 0},
};
// Le bruit porte EN PLUS sa macro de grain : c'est elle qui fait un bruit qui
// change de couleur pendant la note, et elle n'existait nulle part ici.
static const champ_t CHAMPS_BRUIT[] = {
  {"ENV",        53, 15,  REND_ENV, 0},
  {"OUTPUT",     50, 2,   REND_LCR, 0},
  {"FINETUNE",   51, 127, REND_NOMBRE, 0},
  {"NOISE MODE", 52, 7,   REND_BRUIT, 0},
  {"VOL LENGTH", MD_OFF_VOL_LEN,    MD_MACRO_VOL_PAS, REND_NOMBRE, 0},
  {"VOL LOOP",   MD_OFF_VOL_BOUCLE, 255, REND_NOMBRE, 0},
  {"VOL MACRO",  MD_OFF_VOL_MAC,    15,  REND_MAC_VOL, 0},
  {"ARP LENGTH", MD_OFF_ARP_LEN,    MD_MACRO_ARP_PAS, REND_NOMBRE, 0},
  {"ARP LOOP",   MD_OFF_ARP_BOUCLE, 255, REND_NOMBRE, 0},
  {"ARP FIXED",  MD_OFF_ARP_FIXE,   1,   REND_NOMBRE, 0},
  {"ARP MACRO",  MD_OFF_ARP_MAC,    255, REND_MAC_ARP, 0},
  {"NOISE LENGTH", MD_OFF_NZ_LEN,     MD_MACRO_NZ_PAS, REND_NOMBRE, 0},
  {"NOISE LOOP", MD_OFF_NZ_BOUCLE,  255, REND_NOMBRE, 0},
  {"NOISE MACRO", MD_OFF_NZ_MAC,     7,   REND_MAC_VOL, 0},
  {"TABLE",      59, MD_MAX_TABLES, REND_TABLE, 0},
};

// La grille FM, recopiée de la page FM de la DS : mêmes intitulés, même
// ordre, mêmes bornes, mêmes inversions. Les quatre opérateurs sont en
// colonnes, un paramètre par ligne.
static const champ_t CHAMPS_OP[] = {
  {"MULTIPLIER",     1, 15,  REND_NOMBRE, 0},
  {"DETUNE",         0, 7,   REND_NOMBRE, 0},
  {"OUTPUT LEVEL",   2, 127, REND_NOMBRE, 1},
  {"ATTACK",         4, 31,  REND_NOMBRE, 1},
  {"DECAY",          5, 31,  REND_NOMBRE, 1},
  {"SUSTAIN LEVEL",  7, 15,  REND_NOMBRE, 1},
  {"SUSTAIN DECAY",  6, 31,  REND_NOMBRE, 1},
  {"RELEASE",        8, 15,  REND_NOMBRE, 1},
  {"RATE SCALING",   3, 3,   REND_NOMBRE, 0},
  {"AM ENABLE",      9, 1,   REND_NOMBRE, 0},
  {"SSG-EG",        10, 15,  REND_NOMBRE, 0},
};
#define N_CHAMPS_OP ((int)(sizeof CHAMPS_OP / sizeof CHAMPS_OP[0]))

// Ce qui encadre la grille. Même ordre que la DS, où ce bloc occupe l'écran
// du bas : les cinq réglages du LFO et du désaccord, la sortie, puis la table
// — toujours la dernière, précédée d'une rangée de vide.
static const champ_t CHAMPS_FM_HAUT[] = {
  {"ALGORITHM", 44, 7, REND_NOMBRE, 0}, {"FEEDBACK", 45, 7, REND_NOMBRE, 0},
};
static const champ_t CHAMPS_FM_BAS[] = {
  {"AM SENS",   46, 3,   REND_NOMBRE, 0},
  {"PM SENS",   47, 7,   REND_NOMBRE, 0},
  {"LFO",       48, 1,   REND_NOMBRE, 0},
  {"LFO SPEED", 49, 7,   REND_NOMBRE, 0},
  {"FINETUNE",  51, 127, REND_NOMBRE, 0},
  {"OUTPUT",    50, 2,   REND_LCR,    0},
  {"TABLE",     59, MD_MAX_TABLES, REND_TABLE, 0},
};
#define N_FM_HAUT 2
#define N_FM_BAS  ((int)(sizeof CHAMPS_FM_BAS / sizeof CHAMPS_FM_BAS[0]))
#define INSTR_LIGNES (N_FM_HAUT + N_CHAMPS_OP + N_FM_BAS)

static const champ_t *champs_type(int *nb) {
  switch (genre_instr(instr_voie, instr_id)) {
    case GENRE_PCM:   *nb = 6; return CHAMPS_PCM;
    case GENRE_PSG:   *nb = 11; return CHAMPS_PSG;
    case GENRE_BRUIT: *nb = 15; return CHAMPS_BRUIT;
    default:          *nb = 0; return 0;
  }
}

static const char *NOM_GENRE[4] = { "INSTR FM", "INSTR PCM", "INSTR PSG", "INSTR NOISE" };

static uint32_t instr_base(void) {
  return MD_OFF_INSTR + (uint32_t)((instr_id ? instr_id : 1) - 1) * MD_INSTR_OCTETS;
}

// La valeur d'un paramètre d'opérateur, telle qu'elle se LIT — inversion
// comprise. Une seule fonction pour l'écran et pour l'édition : c'est la
// seule façon qu'ils ne divergent jamais.
static uint8_t op_lit(int k, int op) {
  const uint8_t v = md_lit(instr_base() + (uint32_t)op * 11 + CHAMPS_OP[k].dec);
  return CHAMPS_OP[k].inv ? (uint8_t)(CHAMPS_OP[k].maxi - v) : v;
}

static int borne(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

// ── Un rôle par état de case ───────────────────────────────────────────────
// Une case vide s'affiche en DATA atténué, une case pleine en TITRE : on voit
// d'un coup d'œil où il y a quelque chose, sans lire les valeurs. Ce que la
// lecture est en train de jouer passe en ACCENT — et le curseur, qui inverse,
// prime sur tout puisqu'il faut toujours savoir où l'on est.
static int dans_selection(int col, int lig);

// ⚠️ La lecture ne COLORE PAS la case. Elle se marque par un chevron rouge à
// gauche de la colonne, comme sur la DS et comme dans LSDJ : colorer la ligne
// entière noie les valeurs et se confond avec la sélection.
static int sel_active, sel_ancre_l, sel_ancre_c;

// ⚠️ PENDANT UNE SÉLECTION, LE CURSEUR NE BOUGE PAS.
// La croix étend le rectangle depuis l'ancre ; le pavé du curseur, lui, reste
// posé là où on a armé. Sinon deux repères se déplacent en même temps et on
// ne sait plus lequel on regarde — ni où l'on retombera en relâchant.
static int est_curseur(int col, int lig) {
  int cl, cc;
  if (page == PAGE_SONG)       { cl = song_ligne;   cc = song_canal; }
  else if (page == PAGE_CHAIN) { cl = chain_ligne;  cc = chain_col; }
  else                         { cl = phrase_ligne; cc = phrase_col; }
  if (sel_active) { cl = sel_ancre_l; cc = sel_ancre_c; }
  return lig == cl && col == cc;
}

static uint16_t style_case(int plein, int curseur, int lecture) {
  (void)lecture;
  if (curseur) return (uint16_t)(MD_INVERSE | MD_TITRE);
  return plein ? MD_TITRE : MD_DATA;
}

// La sélection se distingue du curseur (inversé clair) et du repère de lecture
// (rouge) : un pavé inversé plus sourd. Trois états, trois apparences — sinon
// on ne sait plus lequel on regarde.
static uint16_t style_sel(uint16_t st, int col, int lig) {
  return dans_selection(col, lig) && !(st & MD_INVERSE)
         ? (uint16_t)(MD_INVERSE | MD_NUM) : st;
}

// ── Sélection et presse-papier ────────────────────────────────────────────
// A+B arme la sélection, la croix l'étend, B seul copie, A+bas colle.
//
// ⚠️ A+bas n'a rien de LSDJesque : il n'y a pas de SELECT sur cette manette et
// il ne restait pas de combinaison libre. C'est pourquoi l'écran ANNONCE le
// geste dès qu'il y a quelque chose à coller — un raccourci qu'on n'a pas
// deviné doit être écrit quelque part.
static int accord_ab;      // reste valable tant que A est tenu, comme sur la DS
static int b_utilise;      // ce B-là a servi à autre chose : il ne copiera pas
static int b_avant_a;      // B est arrivé AVANT A : c'est le geste de coupure
static int c_utilise;      // ce C-là a collé ou cloné : il ne posera rien

#define CLIP_MAX (MD_CANAUX * 64)
static uint8_t clip[CLIP_MAX];
static int clip_page = -1, clip_lignes, clip_cols;
// Le rappel s'efface dès qu'on s'en est servi : il a fait son office.
static int clip_montre;

static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }

// Bornes de la sélection sur la page courante, curseur compris.
static void sel_bornes(int *l0, int *l1, int *c0, int *c1) {
  int cl, cc;
  if (page == PAGE_SONG)        { cl = song_ligne;   cc = song_canal; }
  else if (page == PAGE_CHAIN)  { cl = chain_ligne;  cc = chain_col; }
  else                          { cl = phrase_ligne; cc = phrase_col; }
  if (!sel_active) { *l0 = *l1 = cl; *c0 = *c1 = cc; return; }
  *l0 = mini(sel_ancre_l, cl); *l1 = maxi(sel_ancre_l, cl);
  *c0 = mini(sel_ancre_c, cc); *c1 = maxi(sel_ancre_c, cc);
}

static int dans_selection(int col, int lig) {
  if (!sel_active) return 0;
  int l0, l1, c0, c1; sel_bornes(&l0, &l1, &c0, &c1);
  return lig >= l0 && lig <= l1 && col >= c0 && col <= c1;
}

// Où en est la lecture, par page. -1 quand rien ne joue.
static int lu_song[MD_CANAUX], lu_chain = -1, lu_phrase = -1;
static int lu_song_prec[MD_CANAUX], lu_chain_prec = -1, lu_phrase_prec = -1;

// ============================================================================
//  Page SONG
// ============================================================================
static void song_case(int c, int l) {
  if (l < song_haut || l >= song_haut + LIGNES_VUES) return;
  const int y = LIG_PREMIERE + (l - song_haut);
  const uint8_t v = md_song_lit(c, l);
  uint16_t st = style_case(v != MD_VIDE,
                           page == PAGE_SONG && est_curseur(c, l),
                           lu_song[c] == l);
  if (page == PAGE_SONG) st = style_sel(st, c, l);
  if (v == MD_VIDE) { md_ecran_car(COL_CANAL(c), y, st, '-');
                      md_ecran_car(COL_CANAL(c) + 1, y, st, '-'); }
  else              { md_ecran_hex(COL_CANAL(c), y, st, v, 2); }
}

// ── L'EN-TÊTE D'UNE VOIE, COUPÉE OU NON ───────────────────────────────────
// ⚠️ Une voie coupée doit se voir SANS AMBIGUÏTÉ et sans rien déplacer : la
// grille est déjà serrée, et un signe qui décale les colonnes obligerait à
// tout redessiner. Le nom de la voie passe donc dans la couleur ÉTEINTE, celle
// des numéros de ligne : la colonne garde sa place et l'œil trouve la voie
// muette d'un coup. Deux tirets auraient marché aussi, mais ils se confondent
// avec une case vide.
static void entete_canal(int i) {
  // Le nom en INVERSE VIDÉO : c'est le signe le plus franc qui n'occupe pas
  // une case de plus. Une simple couleur éteinte se voyait mal.
  md_ecran_texte(COL_CANAL(i), LIG_ENTETE,
                 md_lecture_muette(i) ? (MD_NUM | MD_INVERSE) : MD_DATA,
                 NOMS_CANAUX[i]);
}

static void song_dessine(void) {
  md_ecran_texte(36, 0, MD_ACCENT, "SONG");
  for (int i = 0; i < MD_CANAUX; i++) entete_canal(i);
  // ⚠️ La colonne des chevrons se repeint AVEC la grille. Sans ça, faire
  // glisser la fenêtre pendant la lecture laissait les anciens chevrons là où
  // ils étaient dessinés : ils se dupliquaient à chaque défilement.
  for (int v = 0; v < LIGNES_VUES; v++) {
    const int l = song_haut + v;
    // Un repère tous les seize : sans lui on perd le compte dans 256 lignes.
    md_ecran_hex(0, LIG_PREMIERE + v, ton_ligne(l), (uint32_t)l, 2);
    for (int c = 0; c < MD_CANAUX; c++) {
      const int lj = md_lecture_en_cours() ? md_lecture_ligne_song(c) : -1;
      md_ecran_car(COL_CANAL(c) - 1, LIG_PREMIERE + v, MD_ACCENT,
                   (lj == l) ? '>' : ' ');
      song_case(c, l);
    }
  }
  // On resynchronise le suivi : sinon suit_lecture croirait le chevron encore
  // à son ancienne place et l'effacerait au mauvais endroit.
  for (int c = 0; c < MD_CANAUX; c++)
    lu_song[c] = lu_song_prec[c] = md_lecture_en_cours() ? md_lecture_ligne_song(c) : -1;
}

// ============================================================================
//  Page CHAIN — seize lignes : une phrase et sa transposition
// ============================================================================
#define COL_PHR 4
#define COL_TSP 9

static void chain_case(int l) {
  const int y = LIG_PREMIERE + l;
  uint8_t ph; int8_t tsp;
  md_chain_lit(chain_id, l, &ph, &tsp);
  // ⚠️ PENDANT UNE SÉLECTION, LE REPÈRE RESTE SUR L'ANCRE.
  // C'est la plage qui s'étend, pas le curseur qui court. Deux repères
  // mobiles et on ne sait plus lequel lit quoi — c'est la règle de la DS,
  // écrite dans son code. La page SONG l'appliquait déjà (est_curseur), pas
  // celle-ci.
  const int cur_l = sel_active ? sel_ancre_l : chain_ligne;
  const int cur_c = sel_active ? sel_ancre_c : chain_col;
  const int ici = (page == PAGE_CHAIN && l == cur_l);

  const int joue = (lu_chain == l);
  const uint16_t sp = style_sel(style_case(ph != MD_VIDE, ici && cur_c == 0, joue), 0, l);
  if (ph == MD_VIDE) { md_ecran_car(COL_PHR, y, sp, '-');
                       md_ecran_car(COL_PHR + 1, y, sp, '-'); }
  else               { md_ecran_hex(COL_PHR, y, sp, ph, 2); }

  // La transposition se lit en hexadécimal signé, comme dans le fichier : 00
  // est le neutre, FF vaut moins un demi-ton.
  const uint16_t st = style_sel(style_case(tsp != 0, ici && cur_c == 1, joue), 1, l);
  md_ecran_hex(COL_TSP, y, st, (uint8_t)tsp, 2);
}

static void chain_dessine(void) {
  md_ecran_texte(32, 0, MD_ACCENT, "CHAIN");
  md_ecran_hex(38, 0, MD_ACCENT, (uint32_t)chain_id, 2);
  md_ecran_texte(COL_PHR, LIG_ENTETE, MD_DATA, "PHR");
  md_ecran_texte(COL_TSP, LIG_ENTETE, MD_DATA, "TSP");
  for (int l = 0; l < MD_LIGNES_CHAIN; l++) {
    md_ecran_hex(1, LIG_PREMIERE + l, ton_ligne(l), (uint32_t)l, 1);
    chain_case(l);
  }
}

// ============================================================================
//  Page PHRASE — seize lignes : note, instrument, vélocité, commandes
// ============================================================================
#define COL_NOTE 3
#define COL_INS  8
#define COL_VEL  12
#define COL_CMD  16
#define COL_MDC  22

static void phrase_note(int col, int y, uint16_t st, uint8_t n) {
  if (n == 0)            { md_ecran_texte(col, y, st, "---"); return; }
  if (n == MD_VIDE)      { md_ecran_texte(col, y, st, "OFF"); return; }
  const int demi = (n - 1) % 12, octave = (n - 1) / 12;
  md_ecran_car(col,     y, st, NOTES[demi][0]);
  md_ecran_car(col + 1, y, st, NOTES[demi][1]);
  md_ecran_car(col + 2, y, st, (char)('0' + (octave > 9 ? 9 : octave)));
}

static void phrase_case(int l) {
  const int y = LIG_PREMIERE + l;
  md_ligne_phrase r; md_phrase_lit(phrase_id, l, &r);
  // Même règle que sur CHAIN : pendant une sélection le repère tient l'ancre.
  const int cur_l = sel_active ? sel_ancre_l : phrase_ligne;
  const int cur_c = sel_active ? sel_ancre_c : phrase_col;
  const int ici = (page == PAGE_PHRASE && l == cur_l);
  const int joue = (lu_phrase == l);
  #define CUR(n) (ici && cur_c == (n))

  phrase_note(COL_NOTE, y, style_sel(style_case(r.note != 0, CUR(0), joue), 0, l), r.note);

  const uint16_t si = style_sel(style_case(r.instr != 0, CUR(1), joue), 1, l);
  if (r.instr == 0) { md_ecran_car(COL_INS, y, si, '-'); md_ecran_car(COL_INS+1, y, si, '-'); }
  else              { md_ecran_hex(COL_INS, y, si, r.instr, 2); }

  const uint16_t sv = style_sel(style_case(r.vel != MD_VIDE, CUR(2), joue), 2, l);
  if (r.vel == MD_VIDE) { md_ecran_car(COL_VEL, y, sv, '-'); md_ecran_car(COL_VEL+1, y, sv, '-'); }
  else                  { md_ecran_hex(COL_VEL, y, sv, r.vel, 2); }

  // ── UNE COMMANDE EST UN SEUL MOT DE TROIS CARACTÈRES : « C01 » ─────────
  // ⚠️ Lettre et valeur se touchent, comme dans LSDJ et comme sur la DS. La
  // valeur était écrite trois cases plus loin : la case vide donnait « -- -- »
  // au lieu de « --- », et poser une commande faisait apparaître un trou au
  // milieu du champ. Deux moitiés séparées ne se lisent pas comme une
  // commande.
  const uint16_t sc = style_sel(style_case(r.cmd != MD_VIDE, CUR(3), joue), 3, l);
  if (r.cmd == MD_VIDE) md_ecran_car(COL_CMD, y, sc, '-');
  else {
    // Un rang hors table s'affiche « ? » plutôt que de faire croire à une
    // commande qui existerait.
    const char le = md_cmd_lettre(r.cmd);
    md_ecran_car(COL_CMD, y, sc, le ? le : '?');
  }
  // La valeur n'a pas de sens sans sa commande : quand celle-ci est vide, on
  // ne montre pas un 00 qui laisserait croire à un paramètre posé.
  const uint16_t sc2 = style_sel(style_case(r.cmd != MD_VIDE, CUR(4), joue), 4, l);
  if (r.cmd == MD_VIDE) { md_ecran_car(COL_CMD+1, y, sc2, '-'); md_ecran_car(COL_CMD+2, y, sc2, '-'); }
  else                  { md_ecran_hex(COL_CMD + 1, y, sc2, r.val, 2); }

  // Même règle pour la commande MD : code et valeur collés, « 0A40 ». Elle
  // s'écrit par son CODE — celui de DefleMask, sous lequel on la retrouve
  // dans une documentation.
  const uint16_t sm = style_sel(style_case(r.mdcmd != MD_VIDE, CUR(5), joue), 5, l);
  if (r.mdcmd == MD_VIDE) { md_ecran_car(COL_MDC, y, sm, '-'); md_ecran_car(COL_MDC+1, y, sm, '-'); }
  else                    { md_ecran_hex(COL_MDC, y, sm, md_mdcmd_code(r.mdcmd), 2); }
  const uint16_t sm2 = style_sel(style_case(r.mdcmd != MD_VIDE, CUR(6), joue), 6, l);
  if (r.mdcmd == MD_VIDE) { md_ecran_car(COL_MDC+2, y, sm2, '-'); md_ecran_car(COL_MDC+3, y, sm2, '-'); }
  else                    { md_ecran_hex(COL_MDC + 2, y, sm2, r.mdval, 2); }
  #undef CUR
}

static void phrase_dessine(void) {
  md_ecran_texte(31, 0, MD_ACCENT, "PHRASE");
  md_ecran_hex(38, 0, MD_ACCENT, (uint32_t)phrase_id, 2);
  md_ecran_texte(COL_NOTE, LIG_ENTETE, MD_DATA, "NOTE");
  md_ecran_texte(COL_INS,  LIG_ENTETE, MD_DATA, "INS");
  md_ecran_texte(COL_VEL,  LIG_ENTETE, MD_DATA, "VEL");
  md_ecran_texte(COL_CMD,  LIG_ENTETE, MD_DATA, "CMD");
  md_ecran_texte(COL_MDC,  LIG_ENTETE, MD_DATA, "MD CMD");
  for (int l = 0; l < MD_LIGNES_PHRASE; l++) {
    md_ecran_hex(1, LIG_PREMIERE + l, ton_ligne(l), (uint32_t)l, 1);
    phrase_case(l);
  }
}

// ============================================================================
//  Page INSTR — les paramètres de la voix, ceux qui agissent vraiment
// ============================================================================
// Combien d'échantillons la banque contient réellement.
static int pcm_nombre(void) {
  int n = 0;
  for (int k = 0; k < 32; k++) if (pcm_longueur[k]) n++;
  return n;
}

// ── Une valeur, sous la forme qui se LIT ──────────────────────────────────
// Le curseur se pose sur la VALEUR, jamais sur l'intitulé : c'est la valeur
// qu'on modifie. C'est la règle de la DS, et je l'avais prise à l'envers.
static void instr_valeur(int col, int y, const champ_t *c, int ici) {
  const uint32_t b = instr_base();
  const uint16_t st = ici ? MD_ACCENT : MD_DATA;
  md_ecran_texte(col, y, MD_DATA, "                      ");
  switch (c->rendu) {
    case REND_LCR: {
      // Les trois positions restent lisibles, seule celle qui est choisie est
      // en pleine couleur — c'est ainsi que LSDJ montre un choix, et ça évite
      // de faire défiler une valeur invisible.
      static const char L[3] = {'L','C','R'};
      static const uint8_t V[3] = {1, 0, 2};   // gauche, centre, droite
      const uint8_t pan = md_lit(b + c->dec);
      for (int i = 0; i < 3; i++)
        md_ecran_car(col + i * 2, y,
                     (pan == V[i]) ? (ici ? MD_ACCENT : MD_TITRE) : MD_DATA, L[i]);
      break;
    }
    case REND_TABLE: {
      const uint8_t v = md_lit(b + c->dec);
      if (v >= MD_MAX_TABLES) md_ecran_texte(col, y, st, "OFF");
      else md_ecran_hex(col, y, st, v, 2);
      break;
    }
    case REND_ECH: {
      // Le rang DANS LA BANQUE et le NOM : on montre ce qu'on joue, pas un
      // numéro qu'il faudrait aller vérifier ailleurs.
      const uint8_t si = md_lit(b + c->dec);
      if (si < 32 && pcm_longueur[si]) {
        md_ecran_dec(col, y, st, (uint32_t)si + 1, 2);
        md_ecran_car(col + 2, y, st, '/');
        md_ecran_dec(col + 3, y, st, (uint32_t)pcm_nombre(), 2);
        md_ecran_texte(col + 6, y, st, pcm_nom[si]);
      } else {
        md_ecran_texte(col, y, st, "-- (NONE)");
      }
      break;
    }
    case REND_NOTE: {
      // La note de base vient de la BANQUE, pas de l'instrument : c'est une
      // propriété de l'échantillon. Elle se lit, elle ne se règle pas ici.
      const uint8_t si = md_lit(b + 61);
      if (si < 32 && pcm_longueur[si]) {
        const uint8_t n = md_pcm_note(si);
        if (n >= 1 && n <= 108) {
          const int demi = (n - 1) % 12, oct = (n - 1) / 12;
          md_ecran_car(col,     y, st, NOTES[demi][0]);
          md_ecran_car(col + 1, y, st, NOTES[demi][1]);
          md_ecran_car(col + 2, y, st, (char)('0' + (oct > 9 ? 9 : oct)));
        } else md_ecran_texte(col, y, st, "---");
      } else md_ecran_texte(col, y, st, "---");
      break;
    }
    case REND_BOUCLE: {
      const uint8_t si = md_lit(b + 61);
      if (si < 32 && pcm_longueur[si] && pcm_boucle[si] >= 0)
        md_ecran_hex(col, y, st, (uint32_t)pcm_boucle[si], 2);
      else md_ecran_texte(col, y, st, "--");
      break;
    }
    case REND_ENV: {
      // Trois points, chacun une amplitude et une vitesse. Une paire à MD_VIDE
      // est désactivée, et désactive les suivantes.
      for (int pt = 0; pt < 3; pt++) {
        // Entrelacees : (53,54), (55,56), (57,58). C'est la serialisation du
        // .mdm qui fait foi — voir env_pas() dans md_lecture.c.
        const uint8_t amp = md_lit(b + 53 + (uint32_t)pt * 2);
        const uint8_t vit = md_lit(b + 54 + (uint32_t)pt * 2);
        const uint16_t sp = (ici && instr_col == pt) ? MD_ACCENT : MD_DATA;
        if (amp == MD_VIDE) md_ecran_texte(col + pt * 4, y, sp, "--");
        else { md_ecran_hex(col + pt * 4,     y, sp, amp & 15, 1);
               md_ecran_hex(col + pt * 4 + 1, y, sp, vit & 15, 1); }
      }
      break;
    }
    case REND_BRUIT: {
      // Les huit grains du SN76489, nommés comme sur la DS : périodique ou
      // blanc, trois divisions plus « suit la voie 3 ». Un chiffre de 0 à 7
      // ne dit rien de ce qu'on va entendre.
      static const char *NZ[8] = { "P-HI","P-MD","P-LO","P-T3",
                                   "W-HI","W-MD","W-LO","W-T3" };
      md_ecran_texte(col, y, st, NZ[md_lit(b + c->dec) & 7]);
      break;
    }
    case REND_MAC_VOL:
    case REND_MAC_ARP: {
      // La fenêtre suit le curseur : seize pas tiennent à l'écran, la macro
      // peut en compter soixante-quatre. Sans ça la fin d'une macro serait
      // affichée nulle part et modifiable par personne.
      const int arp = (c->rendu == REND_MAC_ARP);
      const int vus = arp ? MAC_VUE_ARP : MAC_VUE_VOL;
      const int larg = arp ? 2 : 1;
      const int n = md_lit(b + (uint32_t)(arp ? MD_OFF_ARP_LEN
                                              : (c->dec == MD_OFF_NZ_MAC
                                                 ? MD_OFF_NZ_LEN
                                                 : MD_OFF_VOL_LEN)));
      const int deb = ici ? (instr_col / vus) * vus : 0;
      for (int k = 0; k < vus; k++) {
        const int pas = deb + k;
        const int x = col + k * larg;
        const uint16_t sp = (ici && instr_col == pas) ? MD_ACCENT : MD_DATA;
        if (pas >= n) {
          md_ecran_texte(x, y, sp, arp ? "--" : "-");
        } else if (arp) {
          md_ecran_hex(x, y, sp, md_lit(b + c->dec + (uint32_t)pas), 2);
        } else {
          md_ecran_hex(x, y, sp, md_lit(b + c->dec + (uint32_t)pas) & 15, 1);
        }
      }
      break;
    }
    default:
      // Deux chiffres hexadécimaux, jamais du décimal : c'est l'écriture de
      // la DS, et la seule qui aligne quatre colonnes d'opérateurs.
      md_ecran_hex(col, y, st, md_lit(b + c->dec), 2);
  }
}

// L'intitulé, calé sur une largeur FIXE. Écrire des blancs puis le mot était
// deux passes de VRAM par rangée ; sur quinze rangées ça sortait du retour
// vertical et se voyait comme une barre noire qui court. Une seule écriture,
// et le mot le plus long donne la largeur.
// Les rangées de la page FM. Deux respirations : une entre FEEDBACK et la
// grille, une entre le dernier réglage et TABLE. La dernière rangée de
// l'écran porte le rappel FRAM, d'où le départ en 3 plutôt qu'en 4.
#define FM_Y_HAUT 3
#define FM_Y_OP   (FM_Y_HAUT + N_FM_HAUT + 1)
#define FM_Y_BAS  (FM_Y_OP + 1 + N_CHAMPS_OP + 1)

static void instr_nom(int y, const char *nom) {
  char t[15];
  int i = 0;
  while (i < 14 && nom[i]) { t[i] = nom[i]; i++; }
  while (i < 14) t[i++] = ' ';
  t[14] = 0;
  md_ecran_texte(0, y, MD_TITRE, t);
}

// La rangée d'écran d'un champ. La page FM reprend EXACTEMENT la disposition
// de la DS : ALGORITHM et FEEDBACK en haut, la grille des quatre opérateurs
// au milieu, OUTPUT et TABLE en bas.
// ── OÙ TOMBE CHAQUE RANGÉE ────────────────────────────────────────────────
// ⚠️ UNE LIGNE VIDE ENTRE LES GROUPES, PAS ENTRE CHAQUE RANGÉE. Une ligne sur
// deux partout était lisible tant que la page en comptait six ; la page NOISE
// en a quinze depuis qu'elle porte ses trois macros, et elle débordait de
// l'écran. Tout serrer l'a rendue illisible. Les groupes — réglages de base,
// macro de volume, macro d'arpège, macro de bruit, table — respirent donc
// entre eux, et se tiennent serrés à l'intérieur.
static int instr_saut(int genre, int lig) {
  if (!lig) return 0;
  switch (genre) {
    case GENRE_PCM:   return 1;                 /* six rangées : tout espacer */
    case GENRE_PSG:   return lig == 3 || lig == 6 || lig == 10;
    case GENRE_BRUIT: return lig == 4 || lig == 7 || lig == 11 || lig == 14;
  }
  return 0;
}

static int instr_y(int lig) {
  const int g = genre_instr(instr_voie, instr_id);
  int y = 4;
  for (int k = 1; k <= lig; k++) y += instr_saut(g, k) ? 2 : 1;
  return y;
}

static void instr_case(int lig) {
  const int ici = (page == PAGE_INSTR && lig == instr_ligne);
  int nb; const champ_t *tbl = champs_type(&nb);

  if (tbl) {
    // Hors FM : une liste, intitulé à gauche en TITRE, valeur à droite.
    const int y = instr_y(lig);
    instr_nom(y, tbl[lig].nom);
    instr_valeur(15, y, &tbl[lig], ici);
    return;
  }

  if (lig < N_FM_HAUT) {
    instr_nom(FM_Y_HAUT + lig, CHAMPS_FM_HAUT[lig].nom);
    instr_valeur(15, FM_Y_HAUT + lig, &CHAMPS_FM_HAUT[lig], ici);
  } else if (lig < N_FM_HAUT + N_CHAMPS_OP) {
    const int k = lig - N_FM_HAUT, y = FM_Y_OP + 1 + k;
    instr_nom(y, CHAMPS_OP[k].nom);
    for (int op = 0; op < 4; op++)
      md_ecran_hex(15 + op * 4, y,
                   (ici && instr_col == op) ? MD_ACCENT : MD_DATA,
                   op_lit(k, op), 2);
  } else {
    const int g = lig - N_FM_HAUT - N_CHAMPS_OP;
    // Une rangée de vide avant TABLE, la même qu'entre FEEDBACK et OP.
    const int y = FM_Y_BAS + g + ((g == N_FM_BAS - 1) ? 1 : 0);
    instr_nom(y, CHAMPS_FM_BAS[g].nom);
    instr_valeur(15, y, &CHAMPS_FM_BAS[g], ici);
  }
}

static int instr_lignes(void) {
  int nb; const champ_t *t = champs_type(&nb);
  return t ? nb : INSTR_LIGNES;
}

static void instr_dessine(void) {
  const int g = genre_instr(instr_voie, instr_id);
  // ⚠️ Le titre se cale sur sa DROITE, une case avant le numéro. Écrit à
  // gauche fixe, « INSTR NOISE » finissait sous le numéro d'instrument et on
  // lisait « INSTR NOIS04 ».
  { const char *t = NOM_GENRE[g];
    int n = 0; while (t[n]) n++;
    md_ecran_texte(22, 0, MD_ACCENT, "                ");
    md_ecran_texte(37 - n, 0, MD_ACCENT, t);
    md_ecran_hex(38, 0, MD_ACCENT, (uint32_t)instr_id, 2); }

  if (g == GENRE_FM) {
    md_ecran_texte(0, FM_Y_OP, MD_TITRE, "OP");
    for (int op = 0; op < 4; op++) {
      char t[2] = {(char)('1' + op), 0};
      md_ecran_texte(15 + op * 4, FM_Y_OP, MD_TITRE, t);
    }
  }

  for (int l = 0; l < instr_lignes(); l++) instr_case(l);
}

// ============================================================================
//  Page TABLE — seize pas déroulés un par tick, façon LSDJ
// ============================================================================
// Les colonnes de la page TABLE de la DS, aux mêmes places. « C1 V1 C2 V2
// MC MV » ne voulait rien dire : une commande et sa valeur forment UNE
// colonne à deux arrêts de curseur, exactement comme dans une phrase.
static const uint8_t COLS_TABLE[8] = {4, 8, 12, 13, 17, 18, 22, 24};
static const uint8_t LARG_TABLE[8] = {2, 2,  1,  2,  1,  2,  2,  2};

static uint32_t table_base(int lig) {
  return MD_OFF_TABLES + (uint32_t)(table_id * MD_LIGNES_TABLE + lig) * MD_TABLE_OCTETS;
}

static void table_case(int lig) {
  const int y = 3 + lig, ici = (page == PAGE_TABLE && lig == table_ligne);
  const uint32_t b = table_base(lig);
  for (int c = 0; c < 8; c++) {
    const uint8_t v = md_lit(b + (uint32_t)c);
    // 0 veut dire « ne touche à rien » pour VOL et TSP, MD_VIDE pour les
    // commandes : une table neuve est donc entièrement neutre, et l'affichage
    // doit le montrer plutôt que d'aligner des zéros trompeurs.
    // Une valeur n'a pas de sens sans sa lettre : les colonnes V1, V2 et MV
    // suivent l'état de C1, C2 et MC. Sinon on affiche un 00 qui laisse croire
    // à un paramètre posé — le même piège que sur la page PHRASE.
    const int vide = (c <= 1) ? (v == 0)
                   : (c & 1)  ? (md_lit(b + (uint32_t)c - 1) == MD_VIDE)
                              : (v == MD_VIDE);
    const uint16_t st = style_case(!vide, ici && table_col == c, 0);
    // Les colonnes de LETTRE ne font qu'un caractère : y écrire deux chiffres
    // débordait sur la valeur d'à côté.
    if (vide) {
      for (int k = 0; k < LARG_TABLE[c]; k++)
        md_ecran_car(COLS_TABLE[c] + k, y, st, '-');
    } else if (c == 2 || c == 4) {
      // ⚠️ Une commande s'écrit par sa LETTRE — « C », pas « 01 ». C'est ce
      // que fait LSDJ, c'est ce que fait la DS, et c'est la seule écriture
      // qu'on retienne. Un rang hors table donne « ? » plutôt qu'un chiffre
      // qui laisserait croire à une commande existante.
      const char le = md_cmd_lettre(v);
      md_ecran_car(COLS_TABLE[c], y, st, le ? le : '?');
    } else if (c == 6) {
      md_ecran_hex(COLS_TABLE[c], y, st, md_mdcmd_code(v), 2);
    } else {
      md_ecran_hex(COLS_TABLE[c], y, st, v, LARG_TABLE[c]);
    }
  }
}

// ── LE NOM DE LA COMMANDE, SOUS LA GRILLE ─────────────────────────────────
// ⚠️ Une lettre seule ne se retient pas. Tant que le curseur est sur une
// colonne de commande, son nom s'écrit CENTRÉ sous la grille, séparé d'elle
// par une rangée vide. Il suit la commande dès qu'elle change, et disparaît
// dès que le curseur quitte la colonne — sinon il désignerait autre chose que
// ce qu'on regarde.
#define LIG_NOM_CMD 21

static void cmd_nom_dessine(void) {
  // ⚠️ ON NE REPEINT QUE SI LE NOM CHANGE. Réécrire quarante cases à chaque
  // image ferait clignoter la ligne — c'est le piège habituel.
  static const char *vu = 0;
  const char *n = 0;
  if (page == PAGE_PHRASE) {
    md_ligne_phrase r; md_phrase_lit(phrase_id, phrase_ligne, &r);
    if (phrase_col == 3 || phrase_col == 4) {
      if (r.cmd != MD_VIDE) n = md_cmd_nom(r.cmd);
    } else if (phrase_col == 5 || phrase_col == 6) {
      if (r.mdcmd != MD_VIDE) n = md_mdcmd_nom(r.mdcmd);
    }
  } else if (page == PAGE_TABLE) {
    const uint32_t b = table_base(table_ligne);
    // Les colonnes de la table : 2-3 la première commande, 4-5 la seconde,
    // 6-7 la commande MD.
    if (table_col == 2 || table_col == 3) {
      const uint8_t v = md_lit(b + 2); if (v != MD_VIDE) n = md_cmd_nom(v);
    } else if (table_col == 4 || table_col == 5) {
      const uint8_t v = md_lit(b + 4); if (v != MD_VIDE) n = md_cmd_nom(v);
    } else if (table_col == 6 || table_col == 7) {
      const uint8_t v = md_lit(b + 6); if (v != MD_VIDE) n = md_mdcmd_nom(v);
    }
  }
  if (!n || !n[0]) n = 0;
  if (n == vu) return;
  vu = n;
  md_ecran_texte(0, LIG_NOM_CMD, MD_DATA, "                                        ");
  if (!n) return;
  int lg = 0; while (n[lg]) lg++;
  md_ecran_texte((40 - lg) / 2, LIG_NOM_CMD, MD_TITRE, n);
}

// Appelée à chaque image sur les deux pages qui portent des commandes : elle
// ne coûte rien tant que rien ne change, et elle attrape ainsi l'édition comme
// le déplacement, sans avoir à la greffer sur chaque geste.
static void cmd_nom_suit(void) {
  if (page == PAGE_PHRASE || page == PAGE_TABLE) cmd_nom_dessine();
}

static void table_dessine(void) {
  md_ecran_texte(32, 0, MD_ACCENT, "TABLE");
  md_ecran_hex(38, 0, MD_ACCENT, (uint32_t)table_id, 2);
  md_ecran_texte(4,  2, MD_DATA, "VOL");
  md_ecran_texte(8,  2, MD_DATA, "TSP");
  md_ecran_texte(12, 2, MD_DATA, "CMD");
  md_ecran_texte(17, 2, MD_DATA, "CMD");
  md_ecran_texte(22, 2, MD_DATA, "MD CMD");
  for (int l = 0; l < MD_LIGNES_TABLE; l++) {
    md_ecran_hex(1, 3 + l, ton_ligne(l), (uint32_t)l, 1);
    table_case(l);
  }
}

// ============================================================================
//  Page PROJECT
// ============================================================================
// ============================================================================
//  Page PROJECT — une liste de réglages, et rien d'autre
//
//  C'est la forme de la DS et celle de LSDJ : un curseur « > » qui descend une
//  liste, les valeurs alignées à droite. Ma première version mélangeait les
//  réglages et la bibliothèque sur le même écran — illisible. La bibliothèque
//  a maintenant sa propre page, comme l'écran FILE de LSDJ.
// ============================================================================
// ⚠️ PLUS DE « LOAD DEMO ». Un morceau se charge PAR LA LISTE, dans
// LOAD/SAVE SONG, et par nulle autre porte. Une entrée de menu qui charge un
// morceau particulier était un second chemin, contraire à la règle — et la
// démo n'a jamais été demandée. Un morceau versé depuis le Mac apparaît dans
// la liste comme les autres, marqué ROM.
enum { PJ_BPM = 0, PJ_NOUVEAU, PJ_FICHIER, PJ_APROPOS, PJ_NOMBRE };

static const char *PROJ_NOMS[PJ_NOMBRE] = {
  "BPM", "NEW SONG", "LOAD/SAVE SONG", "ABOUT"
};

static int message_reste;
static const char *message;
static int modifie_depuis;
// ⚠️ UN MESSAGE DOIT SE REPEINDRE TOUT DE SUITE. Il ne s'affichait que si la
// page se redessinait pour une autre raison : sur l'écran FILE, rien ne le
// faisait, si bien que charger un morceau ne disait RIEN — le morceau était
// bien chargé, mais on croyait que la touche n'avait pas pris.
static void dit_message(const char *m) {
  message = m; message_reste = 120; redessiner = 1;
}

// ⚠️ On ne VIDE PAS l'écran pour redessiner cette page. Chaque champ est écrit
// sur une largeur FIXE, donc réécrire par-dessus suffit. Passer par un
// effacement complet à chaque déplacement du curseur faisait clignoter la page.
static void projet_dessine(void) {
  md_ecran_texte(31, 0, MD_ACCENT, "PROJECT");
  for (int i = 0; i < PJ_NOMBRE; i++) {
    const int y = 3 + i * 2;
    const int ici = (projet_ligne == i);
    md_ecran_car(1, y, MD_ACCENT, ici ? '>' : ' ');
    md_ecran_texte(3, y, ici ? MD_TITRE : MD_DATA, "               ");
    md_ecran_texte(3, y, ici ? MD_TITRE : MD_DATA, PROJ_NOMS[i]);
  }
  md_ecran_dec(22, 3, MD_TITRE, md_song_bpm(), 3);
  md_ecran_texte(3, 20, MD_ACCENT, "                            ");
  if (message_reste) md_ecran_texte(3, 20, MD_ACCENT, message);
}

// ============================================================================
//  Page FILE — la bibliothèque, sur le modèle de l'écran FILE de LSDJ
// ============================================================================
enum { FIC_CHARGER = 0, FIC_ENREGISTRER, FIC_EFFACER, FIC_MODES };
static const char *FIC_NOMS[FIC_MODES] = { "LOAD", "SAVE", "ERASE" };
static const int   FIC_COLS[FIC_MODES] = { 2, 10, 18 };
// ── Le flux de l'écran FILE, celui de LSDJ ────────────────────────────────
// On choisit D'ABORD ce qu'on veut faire — LOAD, SAVE ou ERASE — puis on
// descend dans la liste, et C agit sur le morceau désigné. On ne navigue pas
// dans la liste tant qu'un mode n'a pas été validé.
static int fic_mode, fic_ligne;
// Le nom du dernier morceau chargé : c'est celui qu'on propose en
// l'enregistrant. Charger TUTU, le retoucher, puis devoir retaper son nom
// lettre par lettre n'a aucun sens.
static char dernier_charge[MD_BIB_NOM + 1];
// D'où vient le morceau qu'on a sous les doigts : un emplacement, un morceau
// de la ROM, ou rien du tout. C'est lui que l'étoile désigne.
static int emplacement_courant = -1;
// L'emplacement dont on vient de demander l'effacement, -1 si aucun. Tant
// qu'il est posé, la question tient l'écran.
static int efface_demande = -1;
static int fic_zone;      // 0 = la rangée des modes, 1 = la liste

// ── La fenêtre de nom, reprise de la DS ───────────────────────────────────
// Même grille que là-bas : « < » efface la dernière lettre, « * » valide.
static const char *GRILLE_NOM[4] = { "0123456789", "ABCDEFGHIJ",
                                     "KLMNOPQRST", "UVWXYZ-_<*" };
static int nom_ouvert, nom_lig, nom_col, nom_pour;
static char nom_saisi[9];

// ── LE CHAMP DU NOM, ET UNE SEULE CASE DE LA GRILLE ───────────────────────
// ⚠️ ON NE REPEINT QUE CE QUI A CHANGÉ, ET C'EST TOUT L'ENJEU. Redessiner la
// fenêtre entière à chaque déplacement du curseur, c'est cinq cents écritures
// de VRAM hors du retour vertical : l'écran clignote. Le cadre se trace UNE
// fois à l'ouverture ; ensuite on ne touche qu'à la case quittée et à la case
// prise.
#define NOM_X0 3
#define NOM_Y0 5

static void nom_champ(void) {
  md_ecran_texte(NOM_X0 + 2, NOM_Y0 + 4, MD_DATA,  "________");
  md_ecran_texte(NOM_X0 + 2, NOM_Y0 + 4, MD_TITRE, nom_saisi);
}

// La grille est ESPACÉE comme celle de la DS : trois colonnes par case, deux
// rangées par ligne. Serrée, on ne distinguait pas la case pointée de sa
// voisine.
static void nom_case(int l, int c) {
  const char g = GRILLE_NOM[l][c];
  const int cx = NOM_X0 + 2 + c * 3, cy = NOM_Y0 + 7 + l * 2;
  const uint16_t st = (uint16_t)((l == nom_lig && c == nom_col)
                                 ? (MD_INVERSE | MD_TITRE) : MD_DATA);
  // ⚠️ LA CASE « * » S'AFFICHE « OK ». C'est ce que fait la DS, et pour une
  // bonne raison : une étoile ne dit pas qu'on valide en se posant dessus, et
  // se lit comme une touche de manette qui n'existe pas.
  if (g == '*')      md_ecran_texte(cx, cy, st, "OK");
  else if (g == '<') md_ecran_texte(cx, cy, st, "<");
  else { const char t[2] = { g, 0 }; md_ecran_texte(cx, cy, st, t); }
}

// ── « ÊTES-VOUS SÛR ? » AVANT D'EFFACER ───────────────────────────────────
// ⚠️ Un effacement ne se rattrape pas. Il partait sur un seul appui, au même
// endroit que le chargement : un mode de travers et un morceau disparaissait.
// La DS pose la même question avant ses gestes destructeurs, dans les mêmes
// mots — on reprend sa formulation.
static void efface_dessine(void) {
  const int x0 = NOM_X0, y0 = NOM_Y0 + 2, x1 = 37, y1 = y0 + 8;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const int bord = (y == y0 || y == y1 || x == x0 || x == x1);
      md_ecran_car(x, y, (uint16_t)(bord ? (MD_INVERSE | MD_TITRE) : MD_DATA), ' ');
    }
  char nom[MD_BIB_NOM + 1];
  md_bib_nom(efface_demande, nom);
  md_ecran_texte(x0 + 2, y0 + 2, MD_TITRE, "ERASE THIS SONG ?");
  md_ecran_texte(x0 + 2, y0 + 3, MD_ACCENT, nom);
  md_ecran_texte(x0 + 2, y0 + 5, MD_DATA,  "IT CANNOT BE GOT BACK.");
  md_ecran_texte(x0 + 2, y0 + 7, MD_TITRE, "C YES          B NO");
}

static void nom_dessine(void) {
  // ── UNE FENÊTRE, AVEC SON CADRE ────────────────────────────────────────
  // ⚠️ La DS en trace un (fenetre() dans son main.cpp) : un rectangle plein,
  // bordé d'un trait. Sans lui la grille flottait au-dessus de la liste et on
  // ne voyait pas où commençait la fenêtre. En tuiles de huit points on ne
  // peut pas tracer un trait d'un pixel : le bord est fait d'espaces en
  // INVERSE VIDÉO, ce qui donne les mêmes lignes franches.
  const int x0 = NOM_X0, y0 = NOM_Y0, x1 = 37, y1 = 20;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const int bord = (y == y0 || y == y1 || x == x0 || x == x1);
      md_ecran_car(x, y, (uint16_t)(bord ? (MD_INVERSE | MD_TITRE) : MD_DATA), ' ');
    }

  md_ecran_texte(x0 + 2, y0 + 2, MD_TITRE, "SAVE AS:");
  nom_champ();
  for (int l = 0; l < 4; l++)
    for (int c = 0; c < 10; c++) nom_case(l, c);
}

static int nom_longueur(void) {
  int n = 0; while (n < 8 && nom_saisi[n]) n++; return n;
}

// La liste se lit comme celle de LSDJ : les morceaux d'abord, puis UNE seule
// ligne « (EMPTY) » juste en dessous. Charger cette ligne-là démarre un projet
// vierge — c'est de là qu'on repart de zéro, et nulle part ailleurs.
// ── Les morceaux EMBARQUÉS DANS LA ROM ────────────────────────────────────
// ⚠️ Ils sont là parce que la cartouche n'a pas d'autre entrée. Mesuré : elle
// n'ouvre JAMAIS le fichier EDMD/SAVE/ — sa mémoire est de la FRAM, non
// volatile, et ce fichier n'est qu'une copie de SORTIE réécrite à chaque
// extinction. Le seul chemin qui ENTRE dans la machine est la ROM.
//
// Ils s'ajoutent donc à la bibliothèque au lieu de la remplacer : on les
// CHARGE, on ne peut ni les enregistrer ni les effacer — ils vivent dans une
// mémoire morte. Pour les garder, on les charge puis on les enregistre dans un
// emplacement à soi.
//
// Un rang au-delà de MD_BIB_EMPLACEMENTS désigne un morceau de ROM.
#define FIC_EST_ROM(e) ((e) >= MD_BIB_EMPLACEMENTS)
#define FIC_ROM_RANG(e) ((e) - MD_BIB_EMPLACEMENTS)

static int fic_liste[MD_BIB_EMPLACEMENTS + 1 + MORCEAUX_ROM_MAX];
static int fic_nb;

// ── LA LISTE, C'EST LES EMPLACEMENTS. RIEN D'AUTRE. ──────────────────────
// ⚠️ Les morceaux versés depuis le Mac ne vivent plus dans une liste à part.
// Ils entrent dans la bibliothèque au démarrage (voir rom_vers_bibliotheque)
// et se retrouvent dans un emplacement numéroté, chargeable ET enregistrable
// comme les autres. Une section « ROM » en bas de page n'apprenait rien et
// empêchait de garder son travail.
static void fichier_liste(void) {
  fic_nb = MD_BIB_EMPLACEMENTS;
  for (int e = 0; e < MD_BIB_EMPLACEMENTS; e++) fic_liste[e] = e;
  if (fic_ligne >= fic_nb) fic_ligne = fic_nb - 1;
  if (fic_ligne < 0) fic_ligne = 0;
}

// ── LES MORCEAUX DE LA ROM ENTRENT DANS LA BIBLIOTHÈQUE ───────────────────
// ⚠️ CETTE FONCTION N'EXISTAIT PAS. J'avais retiré la section « ROM » de la
// liste en la remplaçant par un commentaire qui renvoyait ici — et rien ne
// remplissait la bibliothèque. Les morceaux versés depuis le Mac étaient donc
// simplement introuvables.
//
// Un morceau versé doit se trouver là où on cherche les morceaux : dans un
// emplacement numéroté, chargeable ET enregistrable. Tant qu'il restait en
// mémoire morte, on ne pouvait pas garder son travail dessus.
//
// On ne recopie que ce qui manque, et on reconnaît un morceau à SON NOM :
// sans ça chaque allumage en ajouterait un exemplaire de plus.
static void rom_vers_bibliotheque(void) {
  for (int k = 0; k < morceaux_rom_n; k++) {
    char n[MD_BIB_NOM + 1];
    int deja = 0;
    for (int e = 0; e < MD_BIB_EMPLACEMENTS && !deja; e++) {
      if (!md_bib_occupe(e)) continue;
      md_bib_nom(e, n);
      int i = 0;
      while (i < MD_BIB_NOM && morceaux_rom_nom[k][i]
             && n[i] == morceaux_rom_nom[k][i]) i++;
      if (!morceaux_rom_nom[k][i] && (n[i] == ' ' || n[i] == 0)) deja = 1;
    }
    if (deja) continue;

    int libre = -1;
    for (int e = 0; e < MD_BIB_EMPLACEMENTS; e++)
      if (!md_bib_occupe(e)) { libre = e; break; }
    if (libre < 0) return;                   // plus une place : on s'arrête là

    md_codec_decomprime(morceaux_rom_data + morceaux_rom_offset[k],
                        morceaux_rom_taille[k], md_travail());
    if (!md_bib_sauve(libre)) return;         // plus de place : inutile d'insister
    char plein[MD_BIB_NOM + 1];
    for (int i = 0; i < MD_BIB_NOM; i++)
      plein[i] = morceaux_rom_nom[k][i] ? morceaux_rom_nom[k][i] : ' ';
    plein[MD_BIB_NOM] = 0;
    md_bib_pose_nom(libre, plein);
  }
}

// ── LA PAGE ABOUT ──────────────────────────────────────────────────────────
// ⚠️ CE QUI EST ÉCRIT ICI N'EST PAS DÉCORATIF, C'EST L'AVIS LÉGAL.
//
// La GNU GPL version 3 dit, article 5(d) : si le programme affiche des
// « Appropriate Legal Notices », TOUTE VERSION MODIFIÉE DOIT CONTINUER À LES
// AFFICHER. C'est ce qui rend le crédit opposable — aucune licence permissive
// ne l'obtient, et la GPL elle-même ne l'obtient pas sans cette page.
//
// En retirer le nom, le lien ou l'avis de garantie revient donc à sortir des
// termes sous lesquels ce code est distribué. On peut ajouter des lignes
// ici ; on n'en enlève pas.
static const char *APROPOS[] = {
  "GENETRACKERMD",
  "A MUSIC TRACKER THAT RUNS ON THE",
  "SEGA MEGA DRIVE.",
  "",
  "COPYRIGHT (C) 2026 AUDREN THIBAULT",
  "",
  "GITHUB.COM/AUDRENTHIBAULT/",
  "  NATIVEMEGADRIVETRACKER",
  "",
  "THIS PROGRAM COMES WITH ABSOLUTELY",
  "NO WARRANTY. IT IS FREE SOFTWARE",
  "UNDER THE GNU GPL VERSION 3, AND",
  "YOU ARE WELCOME TO REDISTRIBUTE IT",
  "UNDER ITS TERMS. THE FULL LICENCE",
  "IS IN THE FILE NAMED LICENSE, IN",
  "THE SOURCE REPOSITORY ABOVE.",
  "",
  "GPL 7(B) TERM : KEEP THE AUTHOR",
  "NAME AND THE LINK ABOVE, IN THE",
  "SOURCE AND ON THIS PAGE.",
  "",
  "B  BACK"
};

static void apropos_dessine(void) {
  md_ecran_texte(0, 0, MD_TITRE, "ABOUT");
  for (int i = 0; i < (int)(sizeof(APROPOS) / sizeof(APROPOS[0])); i++)
    md_ecran_texte(2, 3 + i,
                   (i == 0) ? MD_ACCENT : (i == 4 || i == 6 || i == 7)
                              ? MD_TITRE : MD_DATA,
                   APROPOS[i]);
}

static void fichier_dessine(void) {
  char nom[MD_BIB_NOM + 1];
  fichier_liste();

  md_ecran_texte(0, 0, MD_TITRE, "FILE");
  md_ecran_dec(24, 0, MD_TITRE, (MD_BIB_FIN - MD_BIB_DONNEES) - md_bib_libre(), 5);
  md_ecran_car(29, 0, MD_DATA, '/');
  md_ecran_dec(30, 0, MD_DATA, MD_BIB_FIN - MD_BIB_DONNEES, 5);

  for (int m2 = 0; m2 < FIC_MODES; m2++)
    md_ecran_texte(FIC_COLS[m2], 2,
                   (uint16_t)((m2 == fic_mode) ? (MD_INVERSE | MD_TITRE) : MD_DATA),
                   FIC_NOMS[m2]);
  md_ecran_car(0, 2, MD_ACCENT, fic_zone == 0 ? '>' : ' ');
  md_ecran_texte(24, 2, MD_ACCENT, "                ");
  if (message_reste) md_ecran_texte(24, 2, MD_ACCENT, message);

  // ⚠️ ON N'EFFACE RIEN AVANT DE REDESSINER. Chaque champ a une largeur FIXE,
  // donc réécrire par-dessus suffit. Blanchir la rangée d'abord faisait
  // clignoter toute la liste dès qu'on bougeait le curseur — c'est le même
  // piège que sur la page PROJECT, déjà signalé là-bas.
  for (int e = 0; e < MD_BIB_EMPLACEMENTS; e++) {
    const int y = 4 + e, ici = (fic_zone == 1 && fic_ligne == e);
    md_ecran_car(1, y, MD_ACCENT, ici ? '>' : ' ');
    md_ecran_hex(3, y, MD_NUM, (uint32_t)e, 2);
    // L'étoile désigne le morceau qu'on a sous les doigts : celui qu'on a
    // chargé, ou le dernier qu'on a enregistré.
    md_ecran_car(6, y, MD_ACCENT, (e == emplacement_courant) ? '*' : ' ');
    if (md_bib_occupe(e)) {
      md_bib_nom(e, nom);
      md_ecran_texte(8, y, ici ? MD_TITRE : MD_DATA, nom);
      md_ecran_dec(21, y, MD_DATA, md_bib_taille(e), 5);
    } else {
      // ⚠️ LA POLICE N'A PAS DE MINUSCULES : md_ecran_car convertit, et les
      // glyphes s'arrêtent à 0x5F. Pour que la ligne vide s'efface devant les
      // noms, on l'écrit donc dans la couleur ÉTEINTE — c'est l'effet
      // recherché, une ligne qui ne retient pas l'oeil.
      md_ecran_texte(8, y, ici ? MD_TITRE : MD_NUM, "(EMPTY)   ");
      md_ecran_texte(21, y, MD_DATA, "     ");
    }
  }
}

// Ce que fait C sur la page FILE, selon le mode choisi en haut.
static void fichier_agit(void) {
  fichier_liste();
  const int e = fic_liste[fic_ligne];

  const int vide = !md_bib_occupe(e);
  if (fic_mode == FIC_CHARGER) {
    if (vide) {
      // Un emplacement libre : on repart d'un morceau VIERGE. C'est le
      // « nouveau projet » de LSDJ, et il n'y en a pas d'autre.
      md_song_vide(); modifie_depuis = 1; emplacement_courant = -1;
      dit_message("NEW SONG");
    } else if (md_bib_charge(e)) {
      md_bib_nom(e, dernier_charge);
      emplacement_courant = e;
      dit_message("LOADED");
    } else {
      dit_message("UNREADABLE");
    }
  } else if (fic_mode == FIC_ENREGISTRER) {
    // On DEMANDE le nom, comme LSDJ. Enregistrer sans nommer laisserait une
    // liste d'emplacements qu'on ne saurait plus distinguer.
    nom_ouvert = 1; nom_pour = e;
    // Le nom de l'emplacement visé s'il en a un ; sinon celui du morceau
    // qu'on a chargé — c'est presque toujours celui qu'on veut garder.
    char n[MD_BIB_NOM + 1];
    if (md_bib_occupe(e)) md_bib_nom(e, n);
    else { int j = 0; while (j <= MD_BIB_NOM) { n[j] = dernier_charge[j]; if (!n[j]) break; j++; }
           n[MD_BIB_NOM] = 0; }
    int k = 0;
    while (k < 8 && n[k] && n[k] != ' ') { nom_saisi[k] = n[k]; k++; }
    nom_saisi[k] = 0;
    // ⚠️ LE CURSEUR PART SUR « OK » QUAND LE NOM EST DÉJÀ LE BON.
    // Réenregistrer une version plus récente du morceau qu'on a sous les
    // doigts est le geste le plus fréquent : il doit se faire en deux appuis
    // sur C, sans traverser la grille. C'est ce que font la DS et LSDJ.
    // Ailleurs, on arrive sur la première lettre.
    int pareil = (k > 0);
    for (int i = 0; i < k && pareil; i++)
      if (nom_saisi[i] != dernier_charge[i]) pareil = 0;
    if (pareil && (dernier_charge[k] == 0 || dernier_charge[k] == ' ')) {
      nom_lig = 3; nom_col = 9;            // la case OK
    } else { nom_lig = 1; nom_col = 0; }
  } else {
    // On ne fait qu'ARMER la question : c'est elle qui effacera, ou pas.
    if (!vide && e != 0) { efface_demande = e; }
    else if (e == 0)     dit_message("WORK SLOT");
  }
  redessiner = 1;
}

// ── Lire et écrire une case, quelle que soit la page ──────────────────────
// Le copier-coller a besoin d'un accès uniforme. Sans lui il faudrait trois
// copies du même code, et elles finiraient par diverger.
static uint8_t cellule_lit(int col, int lig) {
  if (page == PAGE_SONG) return md_song_lit(col, lig);
  if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, lig, &ph, &tsp);
    return col == 0 ? ph : (uint8_t)tsp;
  }
  md_ligne_phrase r; md_phrase_lit(phrase_id, lig, &r);
  switch (col) {
    case 0: return r.note;  case 1: return r.instr; case 2: return r.vel;
    case 3: return r.cmd;   case 4: return r.val;   case 5: return r.mdcmd;
    default: return r.mdval;
  }
}

static void cellule_ecrit(int col, int lig, uint8_t v) {
  if (page == PAGE_SONG) { md_song_pose(col, lig, v); return; }
  if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, lig, &ph, &tsp);
    if (col == 0) ph = v; else tsp = (int8_t)v;
    md_chain_pose(chain_id, lig, ph, tsp);
    return;
  }
  md_ligne_phrase r; md_phrase_lit(phrase_id, lig, &r);
  switch (col) {
    case 0: r.note = v; break;  case 1: r.instr = v; break;
    case 2: r.vel = v; break;   case 3: r.cmd = v; break;
    case 4: r.val = v; break;   case 5: r.mdcmd = v; break;
    default: r.mdval = v; break;
  }
  md_phrase_pose(phrase_id, lig, &r);
}

static int lignes_de_page(void) {
  return page == PAGE_SONG ? MD_SONG_LIGNES
       : page == PAGE_CHAIN ? MD_LIGNES_CHAIN : MD_LIGNES_PHRASE;
}
static int cols_de_page(void) {
  return page == PAGE_SONG ? MD_CANAUX : page == PAGE_CHAIN ? 2 : 7;
}

static void copier(void) {
  int l0, l1, c0, c1; sel_bornes(&l0, &l1, &c0, &c1);
  int nl = l1 - l0 + 1, nc = c1 - c0 + 1;
  // Le presse-papier est borné : mieux vaut tronquer franchement et le dire
  // que déborder en silence sur le reste de la mémoire.
  if (nl * nc > CLIP_MAX) nl = CLIP_MAX / nc;
  for (int i = 0; i < nl; i++)
    for (int j = 0; j < nc; j++)
      clip[i * nc + j] = cellule_lit(c0 + j, l0 + i);
  clip_page = page; clip_lignes = nl; clip_cols = nc;
  clip_montre = 1;

  // ⚠️ LE CURSEUR REVIENT SUR L'ANCRE, ET C'EST INDISPENSABLE.
  //
  // Pendant la sélection, seul l'AFFICHAGE tenait l'ancre : la position réelle,
  // elle, suivait les déplacements. Au moment de copier, la sélection tombait
  // et l'écran rattrapait cette position — le curseur se faisait aspirer d'un
  // coup à l'autre bout de la grille. On armait en F1, on étendait jusqu'à NO,
  // et B envoyait le curseur sur NO.
  //
  // On le repose donc là où la sélection a commencé, comme le fait la DS
  // (poseCurseur(selAncreLigne, selAncreCol) dans sa propre copie).
  if (page == PAGE_SONG)       { song_ligne = sel_ancre_l;   song_canal = sel_ancre_c; }
  else if (page == PAGE_CHAIN) { chain_ligne = sel_ancre_l;  chain_col = sel_ancre_c; }
  else if (page == PAGE_PHRASE){ phrase_ligne = sel_ancre_l; phrase_col = sel_ancre_c; }
  sel_active = 0;
}

// ── Le clone profond ──────────────────────────────────────────────────────
// Copier un chain ne copie que son NUMÉRO : deux cases qui portent le chain 04
// désignent le même chain, et modifier l'une modifie l'autre. Le clone profond
// fabrique une COPIE INDÉPENDANTE — un chain neuf, et des phrases neuves à
// l'intérieur — pour qu'on puisse varier une reprise sans abîmer l'original.
// C'est le geste de LSDJ, et il n'existait pas ici.
static int clone_profond(void) {
  if (page == PAGE_SONG) {
    const uint8_t src = md_song_lit(song_canal, song_ligne);
    if (src == MD_VIDE) return 0;
    const uint8_t neuf = md_chain_libre();
    if (neuf == MD_VIDE) return 0;
    for (int r = 0; r < MD_LIGNES_CHAIN; r++) {
      uint8_t ph; int8_t tsp; md_chain_lit(src, r, &ph, &tsp);
      if (ph != MD_VIDE && ph < MD_MAX_PHRASES) {
        // Chaque phrase est dupliquée elle aussi : sans ça le chain serait
        // neuf mais pointerait sur les mêmes phrases, et on n'aurait rien
        // gagné.
        const uint8_t pn = md_phrase_libre();
        if (pn != MD_VIDE) {
          for (int l = 0; l < MD_LIGNES_PHRASE; l++) {
            md_ligne_phrase g; md_phrase_lit(ph, l, &g);
            md_phrase_pose(pn, l, &g);
          }
          ph = pn;
        }
      }
      md_chain_pose(neuf, r, ph, tsp);
    }
    md_song_pose(song_canal, song_ligne, neuf);
    dernier_chain = neuf;
    return 1;
  }
  if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, chain_ligne, &ph, &tsp);
    if (ph == MD_VIDE || ph >= MD_MAX_PHRASES) return 0;
    const uint8_t pn = md_phrase_libre();
    if (pn == MD_VIDE) return 0;
    for (int l = 0; l < MD_LIGNES_PHRASE; l++) {
      md_ligne_phrase g; md_phrase_lit(ph, l, &g);
      md_phrase_pose(pn, l, &g);
    }
    md_chain_pose(chain_id, chain_ligne, pn, tsp);
    derniere_phrase = pn;
    return 1;
  }
  return 0;   // une phrase ne contient rien qui puisse être cloné plus bas
}

// La valeur « rien » d'une colonne. Elle n'est pas la même partout : une note
// absente vaut zéro, une commande absente vaut MD_VIDE, et une transposition
// nulle vaut zéro. Vider une rangée sans le savoir y écrirait des commandes 00.
static uint8_t cellule_vide(int col) {
  if (page == PAGE_SONG) return MD_VIDE;
  if (page == PAGE_CHAIN) return col == 0 ? MD_VIDE : 0;
  switch (col) {
    case 0: case 1: case 4: return 0;   /* note, instrument, valeur de commande */
    case 6: return 0;                   /* valeur de MD CMD */
    default: return MD_VIDE;            /* vélocité et les deux commandes */
  }
}

static void coller(void) {
  // On ne colle QUE dans la page d'où l'on a copié : une phrase versée dans un
  // chain n'aurait aucun sens, et les octets n'ont pas la même signification.
  if (clip_page != page || clip_lignes <= 0) return;
  int cl, cc;
  if (page == PAGE_SONG)       { cl = song_ligne;   cc = song_canal; }
  else if (page == PAGE_CHAIN) { cl = chain_ligne;  cc = chain_col; }
  else                         { cl = phrase_ligne; cc = phrase_col; }

  // ── SUR SONG, ET SUR SONG SEULEMENT, LE COLLAGE INSÈRE ─────────────────
  // Tout ce qui se trouve sous le curseur descend d'autant de rangées que le
  // presse-papier en contient ; ce qui déborde en bas est perdu. Coller une
  // mesure ne doit pas effacer la suivante, elle doit la repousser.
  //
  // ⚠️ TOUTES LES VOIES DESCENDENT, pas seulement celles qu'on a copiées.
  // Ne décaler que les voies collées désynchroniserait le morceau — les
  // autres garderaient leur ancienne position.
  //
  // ⚠️ ET NULLE PART AILLEURS. Une chain et une phrase ont seize rangées
  // fixes qui sont une mesure : y insérer décalerait le rythme au lieu de
  // remplacer ce qu'on vise. Là, le collage écrase.
  const int nl = lignes_de_page(), nc = cols_de_page(), n = clip_lignes;
  if (page == PAGE_SONG) {
    for (int l = nl - 1; l >= cl + n; l--)
      for (int c = 0; c < nc; c++) cellule_ecrit(c, l, cellule_lit(c, l - n));
    for (int l = cl; l < cl + n && l < nl; l++)
      for (int c = 0; c < nc; c++) cellule_ecrit(c, l, cellule_vide(c));
  }

  // Puis le presse-papier, dans les colonnes d'où il vient.
  for (int i = 0; i < n; i++) {
    const int l = cl + i;
    if (l >= nl) break;
    for (int j = 0; j < clip_cols; j++) {
      const int c = cc + j;
      if (c >= nc) break;
      cellule_ecrit(c, l, clip[i * clip_cols + j]);
    }
  }
}

// Les deux dernières rangées : le rappel des raccourcis qu'on ne peut pas
// deviner, et le compte rendu du dernier geste. Elles s'écrivent seules, sans
// toucher au reste de la page — un message ne doit jamais coûter un
// clignotement.
// ⚠️ LE RAPPEL NE DOIT PAS MORDRE SUR LA GRILLE.
// Il était en bas, par-dessus les dernières lignes du tracker : illisible, et
// ça salit la seule chose qu'on regarde. La colonne 31 est libre sur les trois
// pages de séquence — la grille de SONG s'arrête à la colonne 30, celle de
// PHRASE bien avant. C'est une bande noire de neuf colonnes, et elle ne sert
// à rien d'autre.
//
// ⚠️ Et il n'apparaît QUE quand il y a quelque chose à coller. Un raccourci
// affiché en permanence devient du décor qu'on ne lit plus.
// ⚠️ 33 et non 31 : la colonne NO occupe 30 ET 31, et le rappel mangeait son
// second chiffre. Sept colonnes restent, ce qui suffit — à condition que les
// comptes rendus tiennent en six lettres.
#define COL_RAPPEL 33

static void hint_dessine(void) {
  if (page > PAGE_PHRASE) return;
  for (int l = 2; l <= 12; l++)
    md_ecran_texte(COL_RAPPEL, l, MD_NUM, "       ");
  if (message_reste)
    md_ecran_texte(COL_RAPPEL, 2, MD_ACCENT, message);
  if (clip_page == page && clip_lignes > 0 && clip_montre) {
    md_ecran_texte(COL_RAPPEL, 5, MD_NUM, "PASTE:");
    md_ecran_texte(COL_RAPPEL, 6, MD_TITRE, " A+C");
    md_ecran_texte(COL_RAPPEL, 8, MD_NUM, "CLONE:");
    md_ecran_texte(COL_RAPPEL, 9, MD_TITRE, " A+B");
    md_ecran_texte(COL_RAPPEL, 10, MD_TITRE, " THEN C");
  }
}

// Repeindre la page SANS effacer l'écran. C'est la différence entre un
// rafraîchissement et un clignotement : md_ecran_vide() vide toute la carte
// de noms, et le temps qu'on la réécrive le balayage est passé — on voit un
// éclair noir. Tout ce qui n'est pas un CHANGEMENT DE PAGE passe par ici.
static void redessine_page(void) {
  if (page == PAGE_SONG)         song_dessine();
  else if (page == PAGE_CHAIN)   chain_dessine();
  else if (page == PAGE_PHRASE)  phrase_dessine();
  else if (page == PAGE_INSTR)   instr_dessine();
  else if (page == PAGE_TABLE)   table_dessine();
  else if (page == PAGE_PROJECT) projet_dessine();
  else if (page == PAGE_APROPOS)    apropos_dessine();
  else                           fichier_dessine();
}

// ============================================================================
static void dessine_tout(void) {
  md_ecran_vide();
  if (page == PAGE_SONG)         song_dessine();
  else if (page == PAGE_CHAIN)   chain_dessine();
  else if (page == PAGE_PHRASE)  phrase_dessine();
  else if (page == PAGE_INSTR)   instr_dessine();
  else if (page == PAGE_TABLE)   table_dessine();
  else if (page == PAGE_PROJECT) projet_dessine();
  else if (page == PAGE_APROPOS)    apropos_dessine();
  else                           fichier_dessine();
  if (nom_ouvert) nom_dessine();
  if (efface_demande >= 0) efface_dessine();
  // ⚠️ « PLAY » APPARTIENT À LA PAGE, pas à la touche. Il n'était écrit que
  // par START, donc le moindre redessin l'effaçait et on croyait la lecture
  // arrêtée alors qu'elle tournait.
  // ⚠️ CENTRÉ : (40 - 4) / 2 = 18. Il était en 20, donc décalé de deux cases
  // vers la droite — assez pour que ça se voie.
  if (page <= PAGE_TABLE)
    md_ecran_texte(18, 0, md_lecture_en_cours() ? MD_ACCENT : MD_DATA,
                   md_lecture_en_cours() ? "PLAY" : "    ");
  hint_dessine();
}

static void suit_curseur(void) {
  // Pas de marge : la fenêtre ne glisse QUE si le curseur sortirait. Avec une
  // marge de deux lignes, l'écran partait alors qu'on était encore à
  // l'avant-avant-dernière ligne — on ne voyait jamais le bas de la page.
  if (song_ligne < song_haut) song_haut = song_ligne;
  if (song_ligne > song_haut + LIGNES_VUES - 1) song_haut = song_ligne - LIGNES_VUES + 1;
  song_haut = borne(song_haut, 0, MD_SONG_LIGNES - LIGNES_VUES);
}

// Ne redessine QUE les cases dont le repère de lecture a bougé. Repeindre la
// page à chaque image coûterait plus d'une image de VDP — c'est exactement ce
// qui ralentissait la lecture avant qu'elle passe sur interruption.
static void suit_lecture(void) {
  if (page == PAGE_SONG) {
    for (int c = 0; c < MD_CANAUX; c++) {
      const int l = md_lecture_en_cours() ? md_lecture_ligne_song(c) : -1;
      if (l == lu_song_prec[c]) continue;
      const int vieux = lu_song_prec[c];
      lu_song[c] = l; lu_song_prec[c] = l;
      // On efface l'ancien chevron et on pose le neuf. Deux caractères par
      // canal et par changement de ligne : rien de plus.
      if (vieux >= song_haut && vieux < song_haut + LIGNES_VUES)
        md_ecran_car(COL_CANAL(c) - 1, LIG_PREMIERE + (vieux - song_haut), MD_DATA, ' ');
      if (l >= song_haut && l < song_haut + LIGNES_VUES)
        md_ecran_car(COL_CANAL(c) - 1, LIG_PREMIERE + (l - song_haut), MD_ACCENT, '>');
    }
  } else if (page == PAGE_CHAIN) {
    const int l = (md_lecture_en_cours() && md_lecture_chain(voie_courante) == chain_id)
                  ? md_lecture_ligne_chain(voie_courante) : -1;
    if (l != lu_chain_prec) {
      if (lu_chain_prec >= 0) md_ecran_car(0, LIG_PREMIERE + lu_chain_prec, MD_DATA, ' ');
      if (l >= 0)             md_ecran_car(0, LIG_PREMIERE + l, MD_ACCENT, '>');
      lu_chain = l; lu_chain_prec = l;
    }
  } else if (page == PAGE_PHRASE) {
    const int l = (md_lecture_en_cours() && md_lecture_phrase(voie_courante) == phrase_id)
                  ? md_lecture_ligne_phrase(voie_courante) : -1;
    if (l != lu_phrase_prec) {
      if (lu_phrase_prec >= 0) md_ecran_car(0, LIG_PREMIERE + lu_phrase_prec, MD_DATA, ' ');
      if (l >= 0)              md_ecran_car(0, LIG_PREMIERE + l, MD_ACCENT, '>');
      lu_phrase = l; lu_phrase_prec = l;
    }
  }
}


// ── Descendre et remonter ─────────────────────────────────────────────────
// On REFUSE de descendre dans une case vide : c'est la règle de LSDJ, et sans
// elle on tomberait toujours sur le chain 00 sans comprendre pourquoi.
static void descend(void) {
  if (page == PAGE_SONG) {
    const uint8_t v = md_song_lit(song_canal, song_ligne);
    if (v == MD_VIDE) return;
    chain_id = v; chain_ligne = 0; chain_col = 0;
    // On RETIENT la colonne : c'est elle qui sonnera en solo depuis CHAIN et
    // PHRASE. Prendre le curseur du moment ferait jouer une AUTRE voie que
    // celle qu'on édite — sur les colonnes PSG on n'entendrait rien.
    voie_courante = song_canal;
    page = PAGE_CHAIN; redessiner = 1;
  } else if (page == PAGE_CHAIN) {
    uint8_t ph; md_chain_lit(chain_id, chain_ligne, &ph, 0);
    if (ph == MD_VIDE) return;
    phrase_id = ph; phrase_ligne = 0; phrase_col = 0;
    page = PAGE_PHRASE; redessiner = 1;
  } else if (page == PAGE_PHRASE) {
    // L'instrument montré est celui de la LIGNE sous le curseur — pas le
    // dernier employé : on descend pour régler ce qu'on entend là.
    md_ligne_phrase r; md_phrase_lit(phrase_id, phrase_ligne, &r);
    if (!r.instr) return;
    instr_id = r.instr; instr_ligne = 0; instr_col = 0;
    instr_voie = voie_courante;   // c'est ELLE qui décide du type affiché
    page = PAGE_INSTR; redessiner = 1;
  } else if (page == PAGE_INSTR) {
    // La page « T » de LSDJ, accessible depuis n'importe quel instrument.
    const uint8_t t = md_lit(instr_base() + 59);
    table_id = (t < MD_MAX_TABLES) ? t : 0;
    table_ligne = 0; table_col = 0;
    page = PAGE_TABLE; redessiner = 1;
  }
}

static void remonte(void) {
  // Depuis la TABLE on revient à l'INSTRUMENT dont elle dépend, pas à la page
  // d'avant : c'est de lui qu'elle tient son sens.
  if (page == PAGE_TABLE)       { page = PAGE_INSTR;  redessiner = 1; }
  else if (page == PAGE_INSTR)  { page = PAGE_PHRASE; redessiner = 1; }
  else if (page == PAGE_PHRASE) { page = PAGE_CHAIN;  redessiner = 1; }
  else if (page == PAGE_CHAIN)  { page = PAGE_SONG;   redessiner = 1; }
}

// La sortie est rangée dans un ordre qui n'est pas celui de l'affichage :
// 0 au centre, 1 à gauche, 2 à droite. Ces deux tables font le passage, et
// c'est sur le RANG VISIBLE qu'on se déplace — sans quoi une flèche saute le
// centre d'un côté et ne bouge pas de l'autre.
static const uint8_t LCR_VERS_RANG[3] = {1, 0, 2};   // valeur rangée -> rang
static const uint8_t RANG_VERS_LCR[3] = {1, 0, 2};   // rang -> valeur rangée

// Le champ que le curseur désigne sur la page INSTR : son descripteur ET son
// adresse. Les deux ensemble, parce qu'un champ d'opérateur a la même
// description pour quatre adresses différentes.
static const champ_t *instr_champ(uint32_t *adresse) {
  int nb; const champ_t *tbl = champs_type(&nb);
  if (tbl) {
    // L'enveloppe a trois points : la colonne choisit lequel.
    if (tbl[instr_ligne].rendu == REND_ENV)
      *adresse = instr_base() + 53 + (uint32_t)borne(instr_col, 0, 2) * 2;
    else if (tbl[instr_ligne].rendu == REND_MAC_VOL
          || tbl[instr_ligne].rendu == REND_MAC_ARP)
      // La colonne EST le pas de la macro.
      *adresse = instr_base() + tbl[instr_ligne].dec + (uint32_t)instr_col;
    else
      *adresse = instr_base() + tbl[instr_ligne].dec;
    return &tbl[instr_ligne];
  }
  if (instr_ligne < N_FM_HAUT) {
    *adresse = instr_base() + CHAMPS_FM_HAUT[instr_ligne].dec;
    return &CHAMPS_FM_HAUT[instr_ligne];
  }
  if (instr_ligne < N_FM_HAUT + N_CHAMPS_OP) {
    const int k = instr_ligne - N_FM_HAUT;
    *adresse = instr_base() + (uint32_t)instr_col * 11 + CHAMPS_OP[k].dec;
    return &CHAMPS_OP[k];
  }
  const int g = instr_ligne - N_FM_HAUT - N_CHAMPS_OP;
  *adresse = instr_base() + CHAMPS_FM_BAS[g].dec;
  return &CHAMPS_FM_BAS[g];
}


// L'instrument que porte la voie éditée. S'il n'y en a pas encore, on en
// PREND UN NEUF plutôt que de retomber sur l'instrument 1 : poser une note
// sur une colonne vierge doit donner un timbre à soi, pas celui du voisin.
static uint8_t instr_libre(void);

// Un emplacement qu'on vient de prendre est REMIS À NEUF. Sans ça il gardait
// ce qu'un morceau plus ancien y avait laissé — y compris les réglages d'usine
// d'une version précédente, où l'instrument neuf était en algorithme 7 et ne
// savait donc pas faire un son sale.
static void instr_remet_a_neuf(uint8_t n) {
  if (n >= 1 && n <= MD_MAX_INSTR)
    md_instr_defaut(md_travail() + MD_OFF_INSTR
                    + (uint32_t)(n - 1) * MD_INSTR_OCTETS);
}

// ⚠️ LE PCM NE SE SOUVIENT DE RIEN, ET C'EST VOULU. Un échantillon joue à sa
// vitesse d'enregistrement en C-4 : c'est la hauteur qu'on veut chaque fois
// qu'on en pose un, pas celle du sample précédent, qui était accordée pour un
// autre son. On la change après si on veut, mais on part toujours de là.
static uint8_t note_voie_courante(void) {
  const int v = borne(voie_courante, 0, MD_CANAUX - 1);
  if (v == MD_PCM_VOIE) return NOTE_C4;
  return derniere_note_voie[v] ? derniere_note_voie[v] : NOTE_C4;
}

static uint8_t instr_voie_courante(void) {
  const int v = borne(voie_courante, 0, MD_CANAUX - 1);
  if (!dernier_instr_voie[v]) {
    const uint8_t n = instr_libre();
    if (n) instr_remet_a_neuf(n);
    dernier_instr_voie[v] = n ? n : 1;
    // Un instrument posé sur la voie du convertisseur EST du PCM : on le dit
    // dans son enregistrement, sinon il naîtrait en FM et hurlerait.
    if (v == MD_PCM_VOIE)
      md_ecrit(MD_OFF_INSTR + (uint32_t)(dernier_instr_voie[v] - 1)
               * MD_INSTR_OCTETS + 60, 3);
  }
  return dernier_instr_voie[v];
}

// Le premier emplacement dont PERSONNE ne se sert. Un instrument est occupé
// dès qu'une phrase le nomme : c'est le seul critère qui ne se trompe pas —
// un enregistrement peut être neuf et pourtant utilisé, ou modifié et
// abandonné.
static uint8_t instr_libre(void) {
  uint8_t vu[MD_MAX_INSTR + 1];
  for (int i = 0; i <= MD_MAX_INSTR; i++) vu[i] = 0;
  for (int p = 0; p < MD_MAX_PHRASES; p++)
    for (int r = 0; r < MD_LIGNES_PHRASE; r++) {
      const uint8_t n = md_lit(MD_OFF_PHRASES + (uint32_t)p * 112
                               + (uint32_t)r * 7 + 1);
      if (n && n <= MD_MAX_INSTR) vu[n] = 1;
    }
  for (int c = 0; c < MD_CANAUX; c++)
    if (dernier_instr_voie[c]) vu[dernier_instr_voie[c]] = 1;
  for (int i = 1; i <= MD_MAX_INSTR; i++) if (!vu[i]) return (uint8_t)i;
  return 0;   // tous pris : on garde celui qu'on a
}

// ── Le relevé des écritures YM2612 ────────────────────────────────────────
// La console écrit ce qu'elle a VRAIMENT envoyé à la puce, pour la première
// note de la lecture. À comparer octet par octet avec ce que
// outils/comparateur relève sur le Mac en compilant la même md_puces.c :
// si les deux diffèrent, la divergence est là, et nulle part ailleurs.
//
// Écriture compacte — six caractères par registre — parce que le journal ne
// fait que 960 octets et qu'il doit aussi porter l'en-tête de démarrage.
static void journal_trace_ym(void) {
  const int n = md_ym_trace_nombre();
  if (!n) return;
  md_journal_txt("TRACE YM "); md_journal_dec((uint32_t)n);
  md_journal_ligne();
  for (int i = 0; i < n; i++) {
    int banc; uint8_t reg, val;
    md_ym_trace_lit(i, &banc, &reg, &val);
    if (banc) md_journal_txt("*");
    md_journal_hex(reg, 2);
    md_journal_txt("=");
    md_journal_hex(val, 2);
    md_journal_txt(" ");
    if ((i & 7) == 7) md_journal_ligne();
  }
  md_journal_ligne();
}

// ── Le relevé PCM ─────────────────────────────────────────────────────────
// ⚠️ IL VA DANS L'ANNEAU, PAS DANS LE JOURNAL, et il n'est plus plafonné.
// En texte il remplissait le journal, le faisait reboucler, et emportait
// l'en-tête de démarrage — on perdait exactement ce qu'on voulait lire. Et
// plafonné à huit événements, il s'arrêtait avant que le défaut n'arrive.
// Ici on garde toujours les trente et un DERNIERS.
//
// Il est écrit à chaque échantillon armé, MÊME QUAND RIEN N'EST ARMÉ : savoir
// qu'un geste n'a rien déclenché vaut autant que de savoir ce qu'il a joué.
static uint32_t images_vues;

static void journal_pcm(void) {
  // ⚠️ ON ATTEND QUE LE Z80 AIT DÉMARRÉ.
  // Relire aussitôt après avoir armé, c'est relire avant que le Z80 n'ait eu
  // le temps d'exécuter sa première lecture : `premier_lu` porterait alors la
  // valeur d'avant, et on accuserait le matériel pour une course qu'on aurait
  // créée soi-même. Le compteur de départs nous dit quand il a vraiment
  // commencé. L'attente est BORNÉE : une puce muette ne doit pas figer le
  // tracker, ici pas plus qu'ailleurs.
  static uint8_t departs_vus;
  md_pcm_bilan_t b;
  for (int g = 0; g < 200; g++) {
    md_pcm_bilan(&b);
    if (b.commences != departs_vus) break;
  }
  departs_vus = b.commences;
  uint8_t e[MD_PCM_EVT];
  e[0]  = (uint8_t)images_vues;
  e[1]  = (uint8_t)(images_vues >> 8);
  // ⚠️ LE VERDICT : le Z80 a-t-il lu ce qu'il fallait ?
  // On compare ce qu'il rapporte de la ROM avec ce qui s'y trouve vraiment.
  // Si ça ne correspond pas, il n'atteint pas la cartouche — et toute la
  // suite est expliquée. Si ça correspond, on cherche ailleurs pour de bon.
  // ⚠️ LE PREMIER octet décrit l'échantillon qu'on vient d'armer ; LE DERNIER
  // décrit le PRÉCÉDENT, puisqu'on relève juste après l'armement et que
  // celui-ci vient à peine de commencer. Les comparer au même échantillon
  // donnerait un « faux » permanent qui ne voudrait rien dire.
  static uint8_t si_avant = 0xFF;
  uint8_t verdict = 0;
  const uint8_t si = md_lit(instr_base() + 61);
  if (si < 32 && pcm_longueur[si]) {
    if (b.premier_lu == pcm_banque[pcm_offset[si]]) verdict |= 1;
  }
  if (si_avant < 32 && pcm_longueur[si_avant]) {
    const uint32_t f = pcm_offset[si_avant] + pcm_longueur[si_avant] - 1;
    if (b.dernier_lu == pcm_banque[f]) verdict |= 2;
  } else verdict |= 2;      // rien avant : on ne juge pas
  si_avant = si;
  if (b.commande == pcm_commande_vue())          verdict |= 4;
  e[2]  = si;
  e[3]  = verdict;
  e[4]  = (uint8_t)b.pointeur;       e[5]  = (uint8_t)(b.pointeur >> 8);
  e[6]  = (uint8_t)b.longueur;       e[7]  = (uint8_t)(b.longueur >> 8);
  e[8]  = (uint8_t)b.pas;            e[9]  = (uint8_t)(b.pas >> 8);
  e[10] = b.commences;               e[11] = b.finis;
  e[12] = (uint8_t)b.fin_hl;         e[13] = (uint8_t)(b.fin_hl >> 8);
  e[14] = (uint8_t)b.reste_de;       e[15] = (uint8_t)(b.reste_de >> 8);
  e[16] = (uint8_t)b.banque;
  e[17] = (si < 32 && pcm_longueur[si]) ? pcm_banque[pcm_offset[si]] : 0;
  e[18] = b.premier_lu;
  e[19] = b.dernier_lu;
  md_pcm_anneau_pose(e);
}

// ── Édition, page par page ────────────────────────────────────────────────
static void pose(void) {
  const int cle = page * 100000 + song_canal * 1000 + song_ligne * 7 +
                  chain_ligne * 3 + chain_col + phrase_ligne * 11 + phrase_col;
  const int deux = (cle == der_case && der_age < 20);
  der_case = cle; der_age = 0;

  if (page == PAGE_SONG) {
    const uint8_t a = md_song_lit(song_canal, song_ligne);
    if (deux) { const uint8_t n = md_chain_libre();
                if (n != MD_VIDE) { md_song_pose(song_canal, song_ligne, n); dernier_chain = n; } }
    else if (a == MD_VIDE) md_song_pose(song_canal, song_ligne, dernier_chain);
    else dernier_chain = a;
  } else if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, chain_ligne, &ph, &tsp);
    if (chain_col == 0) {
      if (deux) { const uint8_t n = md_phrase_libre();
                  if (n != MD_VIDE) { ph = n; derniere_phrase = n; } }
      else if (ph == MD_VIDE) ph = derniere_phrase;
      else derniere_phrase = ph;
    }
    md_chain_pose(chain_id, chain_ligne, ph, tsp);
  } else if (page == PAGE_INSTR) {
    // Le champ TABLE est le seul de cette page qu'on POSE : deux C coup sur
    // coup y attachent une table VIERGE, la première libre. C'est le geste de
    // la DS, et sans lui il fallait deviner quel numéro était encore libre.
    uint32_t o; const champ_t *c = instr_champ(&o);
    if (c->rendu == REND_TABLE) {
      const uint8_t v = md_lit(o);
      if (deux || v >= MD_MAX_TABLES) {
        const uint8_t n = md_table_libre();
        if (n != MD_VIDE) { md_ecrit(o, n); dit_message(deux ? "NEW TABLE" : "TABLE"); }
        else dit_message("NO FREE TABLE");
        instr_case(instr_ligne);
      }
    }
    return;
  } else if (page == PAGE_TABLE) {
    const uint32_t o = table_base(table_ligne) + (uint32_t)table_col;
    if (table_col >= 2 && md_lit(o) == MD_VIDE) md_ecrit(o, 0);
    return;
  } else if (page == PAGE_PROJECT) {
    // Ces trois-là ÉCRASENT le morceau en cours : on ne les déclenche que sur
    // leur ligne, jamais par un appui de trop ailleurs.
    if (projet_ligne == PJ_NOUVEAU) { md_song_vide(); modifie_depuis = 1;
                                      dit_message("NEW SONG"); }
    else if (projet_ligne == PJ_APROPOS) { retour_fichier = PAGE_PROJECT;
                                        page = PAGE_APROPOS; }
    else if (projet_ligne == PJ_FICHIER) {
      retour_fichier = PAGE_PROJECT;
      page = PAGE_FICHIER;
      fic_zone = 0; fic_ligne = 0;
      // ⚠️ ON ARRIVE TOUJOURS SUR « LOAD », JAMAIS SUR LE DERNIER MODE UTILISÉ.
      // C'est le réflexe LSDJ : on entre à gauche, une pression à droite mène
      // à SAVE. Garder le mode d'avant fait qu'un geste machinal — droite puis
      // C — tombe sur ERASE alors qu'on visait SAVE. Un morceau se perd comme
      // ça, et l'habitude ne prévient pas.
      fic_mode = FIC_CHARGER;
    }
    redessiner = 1;
    return;
  } else if (page == PAGE_FICHIER) {
    if (fic_zone == 0) {
      // ⚠️ ON ENTRE DANS LA LISTE SUR LE MORCEAU QU'ON A SOUS LES DOIGTS.
      // En SAVE, arriver en tête de liste met le curseur sur l'emplacement 00
      // — celui d'un autre morceau : un appui de trop et on l'écrase. En
      // entrant sur SA ligne, réenregistrer une version plus récente se fait
      // en trois appuis sur C : SAVE, la ligne, OK.
      fic_zone = 1;
      fic_ligne = (emplacement_courant >= 0
                   && emplacement_courant < MD_BIB_EMPLACEMENTS)
                  ? emplacement_courant : 0;
      redessiner = 1;
    }
    else               fichier_agit();
    return;
  } else {
    md_ligne_phrase r; md_phrase_lit(phrase_id, phrase_ligne, &r);
    switch (phrase_col) {
      // Une note posée sur une case vide reprend la DERNIÈRE, et son
      // instrument : c'est ce qui donne un timbre par colonne au lieu de
      // repartir de l'instrument 1 à chaque fois.
      case 0: if (r.note) derniere_note_voie[borne(voie_courante, 0,
                                                    MD_CANAUX - 1)] = r.note;
              else { r.note = note_voie_courante();
                     if (!r.instr) r.instr = instr_voie_courante(); }
              dernier_instr_voie[borne(voie_courante, 0, MD_CANAUX - 1)] =
                  r.instr ? r.instr : instr_voie_courante();
              // On la fait ENTENDRE, sur la voie du canal édité. C'est ce qui
              // permet de comparer deux instruments sans lancer la lecture.
              // ⚠️ À L'ARRÊT SEULEMENT : pendant la lecture, l'audition
              // détournerait la voie et couperait le morceau.
              if (!md_lecture_en_cours()) {
                md_lecture_audition(voie_courante, r.note,
                                    r.instr ? r.instr : instr_voie_courante());
                if (voie_courante == MD_PCM_VOIE) journal_pcm();
              }
              break;
      case 1: {
        // Deux C coup sur coup sur cette case : on veut un instrument NEUF,
        // pas celui d'à côté. Même geste que le double-C qui alloue un chain
        // sur SONG, et il partage son compteur — il est déjà calculé plus
        // haut, à partir de la case ET du temps écoulé.
        if (deux) {
          const uint8_t n = instr_libre();
          if (n) { instr_remet_a_neuf(n); r.instr = n; }
        } else if (!r.instr) {
          r.instr = instr_voie_courante();
        }
        if (r.instr)
          dernier_instr_voie[borne(voie_courante, 0, MD_CANAUX - 1)] = r.instr;
        break; }
      case 2: if (r.vel == MD_VIDE) r.vel = 0x7F; break;   // plein, l'échelle DefleMask
      case 3: case 4: if (r.cmd == MD_VIDE) { r.cmd = 0; r.val = 0; } break;
      case 5: case 6: if (r.mdcmd == MD_VIDE) { r.mdcmd = 0; r.mdval = 0; } break;
    }
    md_phrase_pose(phrase_id, phrase_ligne, &r);
  }
}

static void modifie(int sens, int grand) {
  const int pas = grand ? 16 : 1;
  if (page == PAGE_SONG) {
    const uint8_t a = md_song_lit(song_canal, song_ligne);
    if (a == MD_VIDE) { if (sens > 0) { md_song_pose(song_canal, song_ligne, 0); dernier_chain = 0; } return; }
    const uint8_t n = (uint8_t)borne((int)a + sens * pas, 0, MD_MAX_CHAINS - 1);
    md_song_pose(song_canal, song_ligne, n); dernier_chain = n;
  } else if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, chain_ligne, &ph, &tsp);
    if (chain_col == 0) {
      if (ph == MD_VIDE) { if (sens > 0) ph = 0; }
      else ph = (uint8_t)borne((int)ph + sens * pas, 0, MD_MAX_PHRASES - 1);
      if (ph != MD_VIDE) derniere_phrase = ph;
    } else {
      tsp = (int8_t)borne((int)tsp + sens * (grand ? 12 : 1), -128, 127);
    }
    md_chain_pose(chain_id, chain_ligne, ph, tsp);
  } else if (page == PAGE_INSTR) {
    uint32_t o; const champ_t *c = instr_champ(&o);

    // La note de base appartient à l'ÉCHANTILLON, pas à l'instrument. Elle est
    // réglable quand même : md_pcm_note garde une copie vivante de la banque,
    // qui est en ROM. Un pas d'une octave sur haut/bas, comme sur la DS.
    if (c->rendu == REND_NOTE) {
      const uint8_t si = md_lit(instr_base() + 61);
      if (si >= 32 || !pcm_longueur[si]) return;
      md_pcm_note_pose(si, (uint8_t)borne((int)md_pcm_note(si)
                                          + sens * (grand ? 12 : 1), 1, 108));
      if (!md_lecture_en_cours())
        md_lecture_audition(MD_PCM_VOIE, md_pcm_note(si), instr_id);
      instr_case(instr_ligne);
      return;
    }
    // ⚠️ Un maximum de ZÉRO marque un champ qui n'appartient PAS à
    // l'instrument — LOOP décrit l'échantillon. Sans ce garde-fou, C + croix
    // écrivait un zéro dans le premier octet de l'instrument.
    if (c->maxi == 0) return;

    // ⚠️ Même piège pour l'ÉCHANTILLON : « aucun » se range en 255, et 0 est
    // un rang VALIDE. `borne(255 + 1, 0, 31)` rendait 31 — un emplacement
    // vide — si bien qu'on ne tombait jamais sur un son et que la banque
    // paraissait absente de la ROM. On parcourt donc les emplacements QUI
    // PORTENT QUELQUE CHOSE, et rien d'autre.
    // ⚠️ L'ENVELOPPE PSG A DEUX AXES, comme sur la DS : haut/bas règle
    // l'AMPLITUDE (le chiffre de gauche), gauche/droite la VITESSE (celui de
    // droite). Les deux tombaient sur l'amplitude, et le chiffre de droite
    // était donc inatteignable.
    if (c->rendu == REND_ENV) {
      const uint32_t base_env = instr_base() + 53
                              + (uint32_t)borne(instr_col, 0, 2) * 2;
      const uint32_t base_vit = base_env + 1;   /* la vitesse suit l'amplitude */
      if (grand) {
        // L'amplitude descend jusqu'à « éteint », un cran sous zéro : c'est
        // ainsi qu'on désactive un point d'enveloppe.
        const uint8_t v = md_lit(base_env);
        const int n = (v == MD_VIDE ? -1 : (int)v) + sens;
        md_ecrit(base_env, (n < 0) ? MD_VIDE : (uint8_t)borne(n, 0, 15));
      } else {
        md_ecrit(base_vit, (uint8_t)borne((int)md_lit(base_vit) + sens, 0, 15));
      }
      instr_case(instr_ligne);
      return;
    }
    if (c->rendu == REND_ECH) {
      const uint8_t v = md_lit(o);
      int i = (v < 32) ? (int)v : (sens > 0 ? -1 : 32);
      for (;;) {
        i += sens;
        if (i < 0 || i >= 32) { md_ecrit(o, MD_VIDE); break; }
        if (pcm_longueur[i]) { md_ecrit(o, (uint8_t)i); break; }
      }
      // Le choix s'ENTEND : feuilleter une banque en silence n'apprend rien.
      const uint8_t si = md_lit(o);
      if (si < 32 && pcm_longueur[si] && !md_lecture_en_cours()) {
        md_lecture_audition(MD_PCM_VOIE, md_pcm_note(si), instr_id);
        journal_pcm();
      }
      instr_case(instr_ligne);
      return;
    }
    // ⚠️ « Aucune table » se range en MD_VIDE (255), pas en zéro : zéro est
    // le numéro d'une table VALIDE. Ajouter 1 à 255 puis borner à 16 rendait
    // 16, qui s'affiche encore « OFF » — le champ paraissait mort alors qu'il
    // changeait de valeur sans jamais sortir de l'état vide.
    if (c->rendu == REND_TABLE) {
      const uint8_t v = md_lit(o);
      if (v >= MD_MAX_TABLES) {
        if (sens > 0) md_ecrit(o, 0);       // OFF -> la première table
      } else {
        const int n = (int)v + sens;
        md_ecrit(o, (n < 0 || n >= MD_MAX_TABLES) ? MD_VIDE : (uint8_t)n);
      }
      instr_case(instr_ligne);
      return;
    }
    if (c->rendu == REND_LCR) {
      // Un pas de UN, toujours : trois positions ne se parcourent pas par huit.
      const uint8_t v = md_lit(o);
      const int rang = borne((v < 3 ? (int)LCR_VERS_RANG[v] : 1) + sens, 0, 2);
      md_ecrit(o, RANG_VERS_LCR[rang]);
      md_lecture_instr_maj((uint8_t)instr_id);
      instr_case(instr_ligne);
      return;
    }

    // ⚠️ UN PAS D'ARPÈGE EST SIGNÉ. Il compte des demi-tons au-dessus ET
    // au-dessous de la note : borné à 0 comme les autres réglages, la moitié
    // basse d'une macro serait impossible à écrire. Son grand pas vaut une
    // octave, comme la colonne TSP d'un chain.
    if (c->rendu == REND_MAC_ARP) {
      const int v = (int8_t)md_lit(o);
      md_ecrit(o, (uint8_t)(int8_t)borne(v + sens * (grand ? 12 : 1), -128, 127));
      instr_case(instr_ligne);
      return;
    }
    // Changer la longueur d'une macro change ce que sa rangée affiche ET
    // jusqu'où le curseur peut aller : on repeint la page entière, et on
    // ramène la colonne dans la macro raccourcie.
    if (c->dec == MD_OFF_VOL_LEN || c->dec == MD_OFF_ARP_LEN
     || c->dec == MD_OFF_NZ_LEN) {
      const int n = borne((int)md_lit(o) + sens * (grand ? 8 : 1), 0, (int)c->maxi);
      md_ecrit(o, (uint8_t)n);
      if (instr_col >= n) instr_col = (n > 0) ? n - 1 : 0;
      instr_dessine();
      return;
    }

    // ⚠️ On modifie la valeur TELLE QU'ELLE S'AFFICHE, puis on la range.
    // Sans ça, sur les champs inversés — ATTACK, DECAY, RELEASE, les deux
    // niveaux — la flèche allait dans le mauvais sens et, une fois butée
    // contre sa borne, le réglage paraissait mort. C'est ce qui se passait
    // sur ATTACK.
    const int brut = (int)md_lit(o);
    const int vu   = c->inv ? (int)c->maxi - brut : brut;
    const int neuf = borne(vu + sens * (grand ? 8 : 1), 0, (int)c->maxi);
    md_ecrit(o, (uint8_t)(c->inv ? (int)c->maxi - neuf : neuf));
    // On l'ENTEND bouger, tout de suite : c'est ce qui rend un réglage FM
    // pilotable au lieu de sauter d'un état à l'autre.
    md_lecture_instr_maj((uint8_t)instr_id);

    // On ENTEND l'échantillon en le choisissant : feuilleter une banque en
    // silence n'apprend rien. À l'arrêt seulement — pendant la lecture, une
    // audition volerait sa voie au morceau. C'est la règle de la DS.
    // Une seule rangée repeinte, pas la page : voir instr_nom().
    instr_case(instr_ligne);

  } else if (page == PAGE_TABLE) {
    const uint32_t o = table_base(table_ligne) + (uint32_t)table_col;
    const uint8_t v = md_lit(o);
    // ⚠️ TSP est une TRANSPOSITION : son grand pas vaut UNE OCTAVE, douze
    // demi-tons, pas seize. C'est ce que fait la colonne TSP d'un chain, et
    // c'est ce qu'attend l'oreille — 00, 0C, 18.
    const int pas_ici = (table_col == 1) ? (grand ? 12 : 1) : pas;
    if (v == MD_VIDE) { if (sens > 0) md_ecrit(o, 0); }
    else if (table_col == 2 || table_col == 4) {
      // ⚠️ Bornée au NOMBRE RÉEL de commandes : au-delà on posait un rang qui
      // n'existe dans aucune table, et la case affichait un chiffre muet.
      md_ecrit(o, (uint8_t)borne((int)v + sens, 0, MD_CMD_NOMBRE - 1));
    }
    else if (table_col == 6)
      md_ecrit(o, (uint8_t)borne((int)v + sens, 0, MD_MDCMD_NOMBRE - 1));
    else md_ecrit(o, (uint8_t)((int)v + sens * pas_ici));
    table_case(table_ligne);
  } else if (page == PAGE_PROJECT) {
    if (projet_ligne == PJ_BPM) {
      // Un pas de 1 à gauche/droite, de 10 en haut/bas. Rien de plus : le BPM
      // est rangé tel quel, donc il n'y a plus de cran à contourner.
      md_song_pose_bpm((int)md_song_bpm() + sens * (grand ? 10 : 1));
      projet_dessine();
    }
  } else {
    md_ligne_phrase r; md_phrase_lit(phrase_id, phrase_ligne, &r);
    switch (phrase_col) {
      // Sur une note, le grand pas vaut UNE OCTAVE : douze demi-tons, pas
      // seize — c'est ce qu'attend l'oreille, et ce que fait l'iPad.
      case 0: if (r.note && r.note != MD_VIDE) {
                r.note = (uint8_t)borne((int)r.note + sens * (grand ? 12 : 1), 1, 108);
                derniere_note_voie[borne(voie_courante, 0, MD_CANAUX - 1)] = r.note;
                // On ENTEND la note qu'on déplace : régler une hauteur en
                // silence, c'est deviner. À l'arrêt seulement — pendant la
                // lecture, l'audition volerait sa voie au morceau.
                if (!md_lecture_en_cours()) {
                  md_lecture_audition(voie_courante, r.note,
                                      r.instr ? r.instr : instr_voie_courante());
                  if (voie_courante == MD_PCM_VOIE) journal_pcm();
                }
              }
              break;
      case 1: r.instr = (uint8_t)borne((int)r.instr + sens * pas, 0, MD_MAX_INSTR); break;
      case 2: if (r.vel != MD_VIDE) r.vel = (uint8_t)borne((int)r.vel + sens * pas, 0, 0x7F); break;
      // ⚠️ La borne est le NOMBRE RÉEL de commandes, pas 47 : au-delà on
      // posait un rang qui n'existe dans aucune table, et il ne se passait
      // évidemment rien.
      case 3: if (r.cmd != MD_VIDE)
                r.cmd = (uint8_t)borne((int)r.cmd + sens * pas, 0, MD_CMD_NOMBRE - 1);
              break;
      case 4: r.val = (uint8_t)borne((int)r.val + sens * pas, 0, 255); break;
      case 5: if (r.mdcmd != MD_VIDE)
                r.mdcmd = (uint8_t)borne((int)r.mdcmd + sens * pas, 0, MD_MDCMD_NOMBRE - 1);
              break;
      case 6: r.mdval = (uint8_t)borne((int)r.mdval + sens * pas, 0, 255); break;
    }
    md_phrase_pose(phrase_id, phrase_ligne, &r);
  }
}

// Comportement de LSDJ : sur une case PLEINE on laisse le trou ; sur une case
// VIDE on remonte toute la colonne d'un cran, si bien qu'appuyer plusieurs
// fois « aspire » la colonne vers le haut.
static void efface(void) {
  if (page == PAGE_SONG) {
    if (md_song_lit(song_canal, song_ligne) != MD_VIDE) {
      md_song_pose(song_canal, song_ligne, MD_VIDE); return;
    }
    for (int l = song_ligne; l < MD_SONG_LIGNES - 1; l++)
      md_song_pose(song_canal, l, md_song_lit(song_canal, l + 1));
    md_song_pose(song_canal, MD_SONG_LIGNES - 1, MD_VIDE);
  } else if (page == PAGE_CHAIN) {
    uint8_t ph; int8_t tsp; md_chain_lit(chain_id, chain_ligne, &ph, &tsp);
    if (chain_col == 1) { md_chain_pose(chain_id, chain_ligne, ph, 0); return; }
    if (ph != MD_VIDE)  { md_chain_pose(chain_id, chain_ligne, MD_VIDE, 0); return; }
    for (int l = chain_ligne; l < MD_LIGNES_CHAIN - 1; l++) {
      uint8_t p2; int8_t t2; md_chain_lit(chain_id, l + 1, &p2, &t2);
      md_chain_pose(chain_id, l, p2, t2);
    }
    md_chain_pose(chain_id, MD_LIGNES_CHAIN - 1, MD_VIDE, 0);
  } else if (page == PAGE_TABLE) {
    const uint32_t o = table_base(table_ligne) + (uint32_t)table_col;
    // VOL et TSP sont neutres à ZÉRO, les commandes à MD_VIDE : effacer, c'est
    // revenir au neutre du champ, pas mettre zéro partout.
    md_ecrit(o, table_col <= 1 ? 0 : MD_VIDE);
    if (table_col == 2 || table_col == 4 || table_col == 6)
      md_ecrit(o + 1, 0);   // la valeur suit sa lettre
  } else if (page == PAGE_PROJECT || page == PAGE_FICHIER) {
    return;
  } else if (page == PAGE_INSTR) {
    return;
  } else {
    md_ligne_phrase r; md_phrase_lit(phrase_id, phrase_ligne, &r);
    // Effacer une commande emmène sa valeur : les deux ne veulent rien dire
    // l'une sans l'autre.
    switch (phrase_col) {
      // Effacer la NOTE emporte l'INSTRUMENT : les deux ne veulent rien dire
      // l'un sans l'autre. Une case laissée avec un instrument mais sans note
      // ne joue rien, et la ligne paraît pourtant occupée.
      case 0: r.note = 0; r.instr = 0; break;
      case 1: r.instr = 0; break;
      case 2: r.vel = MD_VIDE; break;
      case 3: case 4: r.cmd = MD_VIDE; r.val = 0; break;
      case 5: case 6: r.mdcmd = MD_VIDE; r.mdval = 0; break;
    }
    md_phrase_pose(phrase_id, phrase_ligne, &r);
  }
}

// ============================================================================
void exception_montre(uint32_t vecteur, uint32_t pc) {
  // ⚠️ On l'ECRIT avant de l'afficher : un écran disparaît au redémarrage,
  // la mémoire de sauvegarde non. C'est ce qui permettra de savoir, après
  // une extinction, si le 68000 avait planté d'abord.
  md_miette_exception(vecteur, pc);
  md_ecran_init();
  md_ecran_vide();
  md_ecran_texte(2, 10, MD_ACCENT, "68000 EXCEPTION");
  md_ecran_texte(2, 12, MD_TITRE,  "VECTOR ");
  md_ecran_hex(11, 12, MD_ACCENT, vecteur, 2);
  md_ecran_texte(2, 13, MD_TITRE,  "ADDRESS");
  md_ecran_hex(11, 13, MD_ACCENT, pc, 6);
  md_ecran_texte(2, 15, MD_DATA,   "02 BUS 03 ADDRESS 04 ILLEGAL");
}

void principal(void) {
  md_ecran_init();
  md_manette_init();
  md_lecture_init();

  if (!md_song_memoire_presente()) {
    md_ecran_texte(2, 12, MD_ACCENT, "NO SAVE MEMORY");
    md_ecran_texte(2, 14, MD_TITRE, "NOTHING ANSWERED AT 200000.");
    for (;;) { }
  }
  // ── ON DÉMARRE TOUJOURS SUR UN MORCEAU VIDE ──────────────────────────
  // ⚠️ C'EST LA RÈGLE, SANS EXCEPTION : qu'un morceau ait été converti et
  // versé juste avant ou non, l'allumage donne un projet vide. Les morceaux
  // se vont chercher dans FILE, LOAD, la liste — nulle part ailleurs.
  //
  // On restaurait l'emplacement de travail depuis la FRAM. Conséquence : il
  // suffisait d'avoir chargé la démo une seule fois pour la retrouver à
  // CHAQUE allumage, comme si le tracker l'imposait. Ce n'est pas un service,
  // c'est arriver sur le travail de quelqu'un d'autre.
  //
  // Rien n'est perdu : la sauvegarde n'a jamais été automatique (voir la
  // boucle principale), donc ce qui est rangé dans un emplacement y reste et
  // se recharge depuis FILE.
  // ⚠️ La bibliothèque se formate quand même : c'était md_song_ouvre() qui
  // s'en chargeait au passage, et sans lui l'écran FILE montrait seize
  // emplacements de 65535 blocs — la FRAM neuve lue telle quelle.
  md_bib_init();
  // Les morceaux versés depuis le Mac rejoignent la bibliothèque AVANT qu'on
  // vide le morceau de travail : md_bib_sauve compresse ce tampon-là.
  rom_vers_bibliotheque();
  md_song_vide();
  modifie_depuis = 1;

  // ── Le compte rendu, écrit AU DÉMARRAGE ─────────────────────────────
  // On a bâti ce canal exprès pour ne plus faire recopier un écran à la main.
  // Il faut donc s'en servir SYSTÉMATIQUEMENT.
  md_journal_debut();
  md_journal_txt("=== GENETRACKER ==="); md_journal_ligne();
  md_journal_txt("CONSOLE : ");
  md_journal_txt((*(volatile uint8_t *)0xA10001 & 0x40) ? "PAL 50 HZ" : "NTSC 60 HZ");
  md_journal_ligne();
  md_journal_txt("VER  A13008 : "); md_journal_hex(*(volatile uint16_t *)0xA13008, 4);
  md_journal_ligne();
  md_journal_txt("ETAT A13002 : "); md_journal_hex(*(volatile uint16_t *)0xA13002, 4);
  md_journal_ligne();
  md_journal_txt("DEMARRAGE No "); md_journal_dec(md_sonde_demarrages());
  md_journal_ligne();
  md_journal_txt("MORCEAU : VIDE (REGLE DE DEMARRAGE)");
  md_journal_ligne();
  // ── Le Z80 atteint-il la cartouche ? ─────────────────────────────────
  { uint8_t lu[3];
    md_z80_essai(lu);
    md_journal_txt("Z80 LIT ROM : ");
    for (int k = 0; k < 3; k++) { md_journal_hex(lu[k], 2); md_journal_txt(" "); }
    md_journal_txt(" ATTENDU ");
    for (int k = 0; k < 3; k++) { md_journal_hex(pcm_banque[k], 2); md_journal_txt(" "); }
    md_journal_txt((lu[0] == pcm_banque[0] && lu[1] == pcm_banque[1]
                    && lu[2] == pcm_banque[2]) ? " -> OUI" : " -> NON");
    md_journal_ligne(); }
  md_journal_txt("TEMPO "); md_journal_dec(md_song_tempo());
  md_journal_txt("  VITESSE "); md_journal_dec(md_song_vitesse());
  md_journal_ligne();

  // ── Ce que faisait le tracker juste avant de disparaître ──────────────
  // Une coupure de courant n'écrit rien. Alors on lit ce qu'on avait écrit
  // AVANT : la page, l'activité et le numéro d'image de la dernière image
  // vécue. Si ça tombe toujours au même endroit, la panne est chez nous ; si
  // c'est chaque fois ailleurs, elle est ailleurs.
  { uint8_t mp, ma; uint32_t mi;
    md_miette_relit(&mp, &ma, &mi);
    md_journal_txt("AVANT COUPURE : ");
    if (mp == 0xFF) md_journal_txt("RIEN D'ECRIT");
    else {
      static const char *PG[8] = {"SONG","CHAIN","PHRASE","INSTR","TABLE",
                                  "PROJECT","FILE","?"};
      static const char *AC[8] = {"REPOS","DESSIN","AUDITION","SEQUENCEUR",
                                  "SRAM","CODEC","?","?"};
      md_journal_txt("PAGE "); md_journal_txt(PG[mp & 7]);
      md_journal_txt("  ACTIVITE "); md_journal_txt(AC[ma & 7]);
      md_journal_txt("  IMAGE "); md_journal_dec(mi);
    }
    md_journal_ligne(); }
  { uint32_t v, pc;
    if (md_miette_exception_relit(&v, &pc)) {
      md_journal_txt("PLANTAGE PRECEDENT : VECTEUR "); md_journal_hex(v, 2);
      md_journal_txt("  PC "); md_journal_hex(pc, 6);
      md_journal_ligne();
    } }

  // À partir d'ici la musique avance toute seule, sur le retour vertical.
  for (int c = 0; c < MD_CANAUX; c++) { lu_song[c] = -1; lu_song_prec[c] = -1; }
  md_irq_autorise();

  for (;;) {
    md_ecran_attend_image();
    // Une miette par image : où on est, et depuis combien d'images. Elle
    // coûte six écritures et c'est le seul témoin qui survive à une coupure.
    md_miette((uint8_t)page, (uint8_t)(md_lecture_en_cours() ? 1 : 0));
    { uint8_t vo = 0, so = 0, ch = 0, ph = 0, in = 0;
      if (md_lecture_en_cours()) md_lecture_position(&so, &ch, &ph, &in, &vo);
      md_miette_position(vo, so, ch, ph, in, md_pcm_etat()); }
    images_vues++;
    cmd_nom_suit();
    md_manette_lit();
    if (der_age < 30000) der_age++;

    const uint16_t tenus = md_manette_tenus();
    const uint16_t appuis = md_manette_appuis();
    const uint16_t frappes = md_manette_frappes();
    int bouge = 0;

    // ── La question d'effacement prend la main sur toute la page ─────
    if (efface_demande >= 0) {
      if (frappes & MD_C) {
        md_bib_efface(efface_demande);
        if (emplacement_courant == efface_demande) emplacement_courant = -1;
        efface_demande = -1;
        dit_message("ERASED");
      } else if (frappes & MD_B) {
        efface_demande = -1;
        redessiner = 1;
      }
      md_ecran_attend_image();
      continue;
    }

    // ── La fenêtre de nom prend la main sur toute la page ────────────
    if (nom_ouvert) {
      // ⚠️ ON NE REPEINT QUE CE QUI A CHANGÉ. La fenêtre demandait un
      // redessin COMPLET de la page à chaque image — écran vidé puis
      // repeint, soixante fois par seconde. Le curseur bougeait bien, mais
      // l'écran clignotait au point de paraître figé.
      const int vieux_l = nom_lig, vieux_c = nom_col;
      if (appuis & MD_GAUCHE) nom_col = borne(nom_col - 1, 0, 9);
      if (appuis & MD_DROITE) nom_col = borne(nom_col + 1, 0, 9);
      if (appuis & MD_HAUT)   nom_lig = borne(nom_lig - 1, 0, 3);
      if (appuis & MD_BAS)    nom_lig = borne(nom_lig + 1, 0, 3);
      if (nom_lig != vieux_l || nom_col != vieux_c) {
        nom_case(vieux_l, vieux_c);       // la case quittée reprend son ton
        nom_case(nom_lig, nom_col);       // la case prise s'allume
      }
      int change_nom = 0;
      if (frappes & MD_C) {
        const char car = GRILLE_NOM[nom_lig][nom_col];
        const int n = nom_longueur();
        if (car == '<') { if (n > 0) nom_saisi[n - 1] = 0; }
        else if (car == '*') {
          char plein[MD_BIB_NOM + 1];
          for (int k = 0; k < MD_BIB_NOM; k++)
            plein[k] = (k < nom_longueur()) ? nom_saisi[k] : ' ';
          plein[MD_BIB_NOM] = 0;
          if (md_bib_sauve(nom_pour)) { md_bib_pose_nom(nom_pour, plein);
                                        emplacement_courant = nom_pour;
                                        dit_message("SAVED"); }
          else                        dit_message("NO SPACE LEFT");
          nom_ouvert = 0;
        }
        else if (n < 8) { nom_saisi[n] = car; nom_saisi[n + 1] = 0; }
        change_nom = 1;
      }
      if (frappes & MD_B) nom_ouvert = 0;
      if (nom_ouvert) { if (change_nom) nom_champ(); }
      else            redessiner = 1;   // la fenêtre s'en va : on repeint
      md_ecran_attend_image();
      continue;
    }

    // ── B, c'est le RETOUR sur l'écran FILE ──────────────────────────
    // Depuis la liste il ramène à la rangée LOAD/SAVE/ERASE ; depuis cette
    // rangée il quitte l'écran. B ne sert à rien d'autre ici, contrairement
    // aux pages de séquence où il copie.
    if ((frappes & MD_B) && page == PAGE_APROPOS) {
      page = PAGE_PROJECT; redessiner = 1; b_utilise = 1;
    }
    if ((frappes & MD_B) && page == PAGE_FICHIER) {
      if (fic_zone == 1) { fic_zone = 0; fichier_dessine(); }
      else               { page = retour_fichier; redessiner = 1; }
      b_utilise = 1;
    }

    // ── B, et ce qu'il a servi à faire ────────────────────────────────
    // B ne copie qu'au RELÂCHEMENT, et seulement s'il n'a servi à rien
    // d'autre : il arme aussi les sélections et fait les grands sauts. Sans
    // ce drapeau, chaque accord déclencherait une copie parasite.
    if (frappes & MD_B) b_utilise = 0;

    // ── COUPER UNE VOIE : B TENU, PUIS A ──────────────────────────────
    // ⚠️ L'ORDRE FAIT TOUT. A puis B arme la sélection ; B puis A coupe la
    // voie sous le curseur. On retient donc lequel est arrivé le premier —
    // sans ça les deux gestes se déclencheraient ensemble.
    //
    // B seul RALLUME : une fois la voie coupée, un appui suffit à la
    // reprendre, sans avoir à refaire l'accord.
    if (frappes & MD_B) b_avant_a = !(tenus & MD_A);
    if ((frappes & MD_B) && page == PAGE_SONG && md_lecture_muette(song_canal)) {
      md_lecture_muet_bascule(song_canal);
      dit_message("UNMUTED");
      b_utilise = 1;
    }
    if ((frappes & MD_A) && (tenus & MD_B) && b_avant_a && page == PAGE_SONG) {
      md_lecture_muet_bascule(song_canal);
      dit_message(md_lecture_muette(song_canal) ? "MUTED" : "UNMUTED");
      b_utilise = 1;
    }
    if (frappes & MD_C) c_utilise = 0;
    if ((tenus & MD_B) && (appuis & MD_CROIX)) b_utilise = 1;
    if (!(tenus & MD_A)) accord_ab = 0;

    if (tenus & MD_A) {
      // A = le SELECT de LSDJ : la navigation entre les pages.
      if (frappes & MD_DROITE) { descend(); sel_active = 0; }
      if (frappes & MD_GAUCHE) {
        if (page == PAGE_FICHIER) { page = retour_fichier; redessiner = 1; }
        else { remonte(); sel_active = 0; }
      }
      // ⚠️ A+haut et A+bas sont la NAVIGATION vers PROJECT et le retour. Ils
      // ne peuvent servir à rien d'autre : sans eux cet écran est
      // inatteignable. Coller et cloner sont passés sur A+C et A+B+C.
      if ((frappes & MD_HAUT) && page != PAGE_PROJECT) {
        retour_projet = page; page = PAGE_PROJECT; projet_ligne = 0; redessiner = 1;
      }
      // A+bas : redescendre depuis PROJECT, coller ailleurs. Les deux gestes
      // ne se croisent jamais, puisque coller n'a de sens que sur les pages de
      // séquence — d'où le rappel affiché seulement là.
      if ((frappes & MD_BAS) && page == PAGE_PROJECT) {
        page = retour_projet; redessiner = 1;
      }
      // ── A+C colle, A+B+C clone ────────────────────────────────────
      // Le geste du clone est celui d'une SÉLECTION suivie d'un collage : on
      // tient A, on tape B — ce qui arme déjà la sélection — puis C. C'est
      // accord_ab qui les sépare, et il est posé par le A+B juste dessous.
      if ((frappes & MD_C) && page <= PAGE_PHRASE) {
        if (accord_ab) {
          dit_message(clone_profond() ? "DEEP CLONED" : "NOTHING TO CLONE");
          sel_active = 0;
        } else {
          coller(); dit_message("PASTED");
        }
        clip_montre = 0;   // on s'en est servi : le rappel n'a plus d'objet
        c_utilise = 1;
        // ⚠️ On repeint la page ENTIÈRE : ce qu'on vient de coller s'étale sur
        // plusieurs lignes et plusieurs colonnes, alors que le rafraîchissement
        // ordinaire ne touche que la case du curseur. Sans ça le collage
        // restait invisible jusqu'à ce qu'on passe dessus.
        suit_curseur(); redessine_page(); hint_dessine();
      }
      // A+B ARME la sélection. Il ne la bascule pas : A reste tenu, et chaque
      // B annulerait alors celle qu'on vient de faire.
      if ((frappes & MD_B) && page <= PAGE_PHRASE) {
        if (!sel_active) {
          sel_active = 1;
          if (page == PAGE_SONG)       { sel_ancre_l = song_ligne;   sel_ancre_c = song_canal; }
          else if (page == PAGE_CHAIN) { sel_ancre_l = chain_ligne;  sel_ancre_c = chain_col; }
          else                         { sel_ancre_l = phrase_ligne; sel_ancre_c = phrase_col; }
          // ⚠️ PAS redessiner : il efface l'écran avant de repeindre, et armer
          // une sélection faisait donc clignoter la page.
          bouge = 1;
        }
        accord_ab = 1;
        b_utilise = 1;
      }
    } else if ((tenus & MD_C) && (appuis & MD_CROIX)) {
      modifie((appuis & (MD_HAUT | MD_DROITE)) ? 1 : -1,
              (appuis & (MD_HAUT | MD_BAS)) != 0);
      bouge = 1;
    } else if ((tenus & MD_B) && (appuis & (MD_HAUT | MD_BAS)) && page == PAGE_SONG) {
      song_ligne = borne(song_ligne + ((appuis & MD_HAUT) ? -16 : 16),
                         0, MD_SONG_LIGNES - 1);
      bouge = 1;
    } else if (appuis & MD_CROIX) {
      if (page == PAGE_SONG) {
        if (appuis & MD_HAUT)   song_ligne = borne(song_ligne - 1, 0, MD_SONG_LIGNES - 1);
        if (appuis & MD_BAS)    song_ligne = borne(song_ligne + 1, 0, MD_SONG_LIGNES - 1);
        if (appuis & MD_GAUCHE) song_canal = borne(song_canal - 1, 0, MD_CANAUX - 1);
        if (appuis & MD_DROITE) song_canal = borne(song_canal + 1, 0, MD_CANAUX - 1);
      } else if (page == PAGE_CHAIN) {
        if (appuis & MD_HAUT)   chain_ligne = borne(chain_ligne - 1, 0, MD_LIGNES_CHAIN - 1);
        if (appuis & MD_BAS)    chain_ligne = borne(chain_ligne + 1, 0, MD_LIGNES_CHAIN - 1);
        if (appuis & MD_GAUCHE) chain_col = 0;
        if (appuis & MD_DROITE) chain_col = 1;
      } else if (page == PAGE_PHRASE) {
        if (appuis & MD_HAUT)   phrase_ligne = borne(phrase_ligne - 1, 0, MD_LIGNES_PHRASE - 1);
        if (appuis & MD_BAS)    phrase_ligne = borne(phrase_ligne + 1, 0, MD_LIGNES_PHRASE - 1);
        if (appuis & MD_GAUCHE) phrase_col = borne(phrase_col - 1, 0, 6);
        if (appuis & MD_DROITE) phrase_col = borne(phrase_col + 1, 0, 6);
      } else if (page == PAGE_INSTR) {
        if (appuis & MD_HAUT)   instr_ligne = borne(instr_ligne - 1, 0, instr_lignes() - 1);
        if (appuis & MD_BAS)    instr_ligne = borne(instr_ligne + 1, 0, instr_lignes() - 1);
        int nbc; const champ_t *tc = champs_type(&nbc);
        // Combien d'arrêts de curseur sur cette rangée ? Quatre sur la grille
        // des opérateurs, trois sur les points d'enveloppe, un par pas sur une
        // macro, un partout ailleurs.
        //
        // ⚠️ Les trois points d'ENV étaient INATTEIGNABLES : la règle ne
        // donnait quatre colonnes qu'à la grille FM et une seule à tout le
        // reste, si bien que les deuxième et troisième points de l'enveloppe
        // PSG ne pouvaient pas être sélectionnés.
        int nc = 1;
        if (!tc) {
          if (instr_ligne >= N_FM_HAUT && instr_ligne < N_FM_HAUT + N_CHAMPS_OP)
            nc = 4;
        } else {
          const champ_t *r = &tc[borne(instr_ligne, 0, nbc - 1)];
          if (r->rendu == REND_ENV) nc = 3;
          else if (r->rendu == REND_MAC_VOL || r->rendu == REND_MAC_ARP) {
            const uint32_t ol = (r->rendu == REND_MAC_ARP) ? MD_OFF_ARP_LEN
                              : (r->dec == MD_OFF_NZ_MAC) ? MD_OFF_NZ_LEN
                                                          : MD_OFF_VOL_LEN;
            nc = md_lit(instr_base() + ol);
            if (nc < 1) nc = 1;
          }
        }
        if (appuis & MD_GAUCHE) instr_col = borne(instr_col - 1, 0, nc - 1);
        if (appuis & MD_DROITE) instr_col = borne(instr_col + 1, 0, nc - 1);
        if (instr_col >= nc) instr_col = nc - 1;
      } else if (page == PAGE_TABLE) {
        if (appuis & MD_HAUT)   table_ligne = borne(table_ligne - 1, 0, MD_LIGNES_TABLE - 1);
        if (appuis & MD_BAS)    table_ligne = borne(table_ligne + 1, 0, MD_LIGNES_TABLE - 1);
        if (appuis & MD_GAUCHE) table_col = borne(table_col - 1, 0, 7);
        if (appuis & MD_DROITE) table_col = borne(table_col + 1, 0, 7);
      } else if (page == PAGE_PROJECT) {
        if (appuis & MD_HAUT)   projet_ligne = borne(projet_ligne - 1, 0, PJ_NOMBRE - 1);
        if (appuis & MD_BAS)    projet_ligne = borne(projet_ligne + 1, 0, PJ_NOMBRE - 1);
        // On réécrit la page PAR-DESSUS, sans l'effacer : chaque champ a une
        // largeur fixe. Passer par un effacement complet la faisait clignoter
        // à chaque déplacement du curseur.
        projet_dessine();
      } else {
        // FILE, en deux temps. Sur la rangée du haut, gauche/droite choisit ce
        // qu'on veut faire ; C valide et descend dans la liste ; là seulement
        // C agit sur le morceau désigné.
        if (fic_zone == 0) {
          if (appuis & MD_GAUCHE) fic_mode = borne(fic_mode - 1, 0, FIC_MODES - 1);
          if (appuis & MD_DROITE) fic_mode = borne(fic_mode + 1, 0, FIC_MODES - 1);
          if (appuis & MD_BAS)    fic_zone = 1;
        } else {
          // Haut ne remonte PAS à la rangée des modes : la croix sert à
          // parcourir la liste, un point. C'est B qui revient en arrière.
          if (appuis & MD_HAUT)   fic_ligne--;
          if (appuis & MD_BAS)    fic_ligne++;
        }
        fichier_dessine();
      }
      bouge = 1;
    }

    // Le relâchement coupe la note d'audition.
    if (md_manette_relaches() & MD_C) md_lecture_audition_stop();

    // B relâché sans avoir servi à autre chose : il copie sur les pages de
    // séquence, il ENREGISTRE sur PROJECT. Ce sont deux gestes distincts sur
    // deux écrans distincts, jamais ambigus.
    if ((md_manette_relaches() & MD_B) && !b_utilise) {
      if (page <= PAGE_PHRASE) {
        copier();                       // il remet sel_active à zéro
        dit_message("COPIED");
        suit_curseur(); redessine_page(); hint_dessine();
      }
    }

    // C+B efface, dans les deux ordres — c'est le A+B de la DS.
    // ⚠️ ON MARQUE LES DEUX TOUCHES COMME SERVIES. Sans ça, relâcher B après
    // un effacement déclenchait une COPIE : le presse-papier se remplissait
    // tout seul, le rappel « PASTE / CLONE » s'affichait alors qu'on venait
    // de supprimer, et le repeint de ce rappel faisait clignoter l'écran à
    // chaque effacement. Une seule cause pour les deux.
    // ⚠️ JAMAIS QUAND A EST TENU. A+B arme la sélection, et rien d'autre :
    // si un C parasite se glissait dans la lecture de la manette, l'accord
    // devenait un effacement. Ceinture en plus de la bretelle — la cause est
    // corrigée dans md_manette.c, ce verrou dit l'intention.
    if (!(tenus & MD_A) &&
        (((frappes & MD_C) && (tenus & MD_B)) ||
         ((frappes & MD_B) && (tenus & MD_C)))) {
      efface(); bouge = 1; b_utilise = 1; c_utilise = 1;
    }
    else if ((frappes & MD_C) && !(tenus & (MD_B | MD_A)) && !c_utilise) {
      pose(); bouge = 1;
    }

    // START lance et arrête, depuis n'importe quelle page. On part de la ligne
    // que le curseur DÉSIGNE : c'est ce qu'on veut entendre, pas le début du
    // morceau — sinon éprouver un passage demande de tout réécouter.
    if (frappes & MD_START) {
      if (md_lecture_en_cours()) {
        // ── Le relevé part au journal quand on ARRÊTE ────────────────────
        // On l'écrit ici et pas pendant la lecture : la mémoire de sauvegarde
        // s'écrit un octet à la fois, et le faire sous l'interruption vidéo
        // volerait du temps au son. À l'arrêt, rien ne presse.
        journal_trace_ym();
        md_lecture_arrete();
        md_ecran_texte(18, 0, MD_DATA, "    ");
      } else {
        if (page == PAGE_CHAIN) {
          md_lecture_pose_portee(MD_PORTEE_CHAIN, voie_courante,
                                 (uint8_t)chain_id, chain_ligne);
          md_lecture_demarre(0);
          md_ym_trace_arme();
        } else if (page == PAGE_PHRASE || page == PAGE_INSTR || page == PAGE_TABLE) {
          // ⚠️ TOUJOURS depuis la ligne 00, jamais depuis le curseur. Une
          // phrase est un motif : on l'écoute en entier. C'est SONG qui part
          // de la ligne désignée, parce qu'on y cherche un passage.
          md_lecture_pose_portee(MD_PORTEE_PHRASE, voie_courante,
                                 (uint8_t)phrase_id, 0);
          md_lecture_demarre(0);
          md_ym_trace_arme();
        } else {
          md_lecture_pose_portee(MD_PORTEE_SONG, 0, 0, 0);
          md_lecture_demarre(song_ligne);
          md_ym_trace_arme();
        }
        md_ecran_texte(18, 0, MD_ACCENT, "PLAY");
      }
    }


    // ⚠️ PAS DE SAUVEGARDE AUTOMATIQUE. Elle existait, elle figeait la boucle
    // une à trois secondes et affichait un « SAVE » intempestif. C'est une
    // Mega Drive, pas un ordinateur : on enregistre quand on le décide, depuis
    // l'écran FILE. À l'utilisateur d'y penser, comme sur LSDJ.
    if (message_reste) { message_reste--;
                         if (!message_reste) {
                           if (page >= PAGE_PROJECT) redessiner = 1;
                           else hint_dessine();
                         } }

    if (redessiner) {
      for (int c = 0; c < MD_CANAUX; c++) { lu_song[c] = -1; lu_song_prec[c] = -1; }
      lu_chain = lu_chain_prec = lu_phrase = lu_phrase_prec = -1;
      suit_curseur(); dessine_tout(); redessiner = 0;
    } else if (bouge && sel_active) {
      // ⚠️ PAS dessine_tout() : il efface l'écran d'abord, et déplacer le
      // curseur dans une sélection faisait donc clignoter la page à chaque
      // cran. On repeint par-dessus, ce qui suffit — chaque case connaît sa
      // couleur de sélection.
      suit_curseur();
      redessine_page();
      hint_dessine();
    } else if (bouge) {
      const int ancien_haut = song_haut;
      suit_curseur();
      if (page == PAGE_SONG && song_haut != ancien_haut) {
        song_dessine();
      } else if (page == PAGE_SONG) {
        song_case(song_ancien_canal, song_ancienne_ligne);
        song_case(song_canal, song_ligne);
      } else if (page == PAGE_CHAIN) {
        for (int l = 0; l < MD_LIGNES_CHAIN; l++) chain_case(l);
      } else if (page == PAGE_PHRASE) {
        for (int l = 0; l < MD_LIGNES_PHRASE; l++) phrase_case(l);
        cmd_nom_dessine();
      } else if (page == PAGE_INSTR) {
        // Deux rangées, jamais la page entière : repeindre quinze rangées
        // débordait du retour vertical, et ça se voyait comme une barre noire
        // qui court sur l'écran.
        instr_case(instr_ancienne_ligne);
        instr_case(instr_ligne);
      } else if (page == PAGE_TABLE) {
        for (int l = 0; l < MD_LIGNES_TABLE; l++) table_case(l);
        cmd_nom_dessine();
      } else if (page == PAGE_PROJECT) {
        projet_dessine();
      } else {
        fichier_dessine();
      }
      hint_dessine();
      // ⚠️ Ce dispatch MANQUAIT : le « sinon » dessinait les lignes de PHRASE
      // sur TOUTES les autres pages. Modifier un opérateur depuis l'écran
      // d'instrument y superposait la grille de la phrase, et comme la valeur
      // changée n'était jamais réaffichée, le réglage semblait bloqué alors
      // qu'il fonctionnait.
    }
    suit_lecture();
    song_ancien_canal = song_canal;
    song_ancienne_ligne = song_ligne;
    instr_ancienne_ligne = instr_ligne;
  }
}
