#include "md_lecture.h"
#include "md_commandes.h"
#include "md_song.h"
#include "md_puces.h"
#include "banque_pcm.h"

typedef struct {
  int      song_ligne;     // où l'on en est dans SA colonne
  uint8_t  chain;
  int      chain_ligne;
  uint8_t  phrase;
  int      phrase_ligne;
  int8_t   transpose;      // celle de la ligne de chain
  uint8_t  instr;          // l'instrument courant de la voie
  uint8_t  note;           // la note qui sonne, 0 si rien
  int      actif;
  // ── La table attachée à l'instrument ──────────────────────────────────
  // Une table se déroule UN PAS PAR TICK, indépendamment des lignes de la
  // phrase : c'est ce qui permet un arpège plus rapide que le tempo. Elle
  // reboucle tant que la note dure.
  uint8_t  table;          // MD_VIDE = aucune
  // ⚠️ TROIS FLUX, CHACUN SA POSITION — la règle vient du projet DS, où une
  // table a trois curseurs qui avancent pour leur compte :
  //     0  la colonne VOL
  //     1  la colonne TSP et la première CMD
  //     2  la seconde CMD, et la colonne MD CMD qui voyage avec elle
  // C'est ce qui permet à un H de faire boucler la transposition pendant que
  // la seconde commande continue de descendre. Un curseur unique, comme
  // ici jusqu'ici, ne pouvait pas boucler sur une partie de la table sans
  // emmener tout le reste avec lui.
  uint8_t  table_pas[3];
  // Combien de tours chaque ligne portant un H a déjà faits. Le compteur est
  // indexé par LA LIGNE QUI PORTE LE H, pas par le flux : c'est ce qui permet
  // d'imbriquer deux boucles sans qu'elles se marchent dessus.
  uint8_t  hop_faits[3][MD_LIGNES_TABLE];
  uint8_t  vol;            // le volume de la ligne, avant la table

  // ── LES DEUX EMPLACEMENTS D'EFFET ─────────────────────────────────────
  // 0 = la colonne CMD (la lettre), 1 = la colonne MD CMD (le code). Les deux
  // tournent EN MEME TEMPS : une ligne peut porter un vibrato à la lettre et
  // un glissando au code, comme dans DefleMask. Un seul emplacement aurait
  // obligé l'une des deux colonnes à écraser l'autre.
  uint8_t  eff[2];         // MD_E_*
  uint8_t  effval[2];
  int8_t   vol_delta;      // ce que les glissandos et le trémolo retirent
  uint8_t  vib_prof;       // profondeur posée par W, si elle l'a été
  uint8_t  retrig;         // compteur de R
  uint8_t  hop_ph[MD_LIGNES_PHRASE];   // les tours faits par chaque H de phrase

  // ── LES TROIS MACROS PSG ──────────────────────────────────────────────
  // Un pas par tick, comme la table, mais elles appartiennent à
  // l'instrument : c'est la macro de volume qui donne son enveloppe à un
  // instrument PSG venu du tracker DS. Sans elles, ces instruments-là
  // sonnaient plats, ou pas du tout.
  //
  // Elles ne DÉCIDENT rien toutes seules : elles déposent ici ce qu'elles
  // veulent, et la table passe par-dessus — sa colonne TSP s'ajoute à
  // l'arpège, sa colonne VOL écrase le niveau. C'est l'ordre du tracker DS.
  uint8_t  mvol_pas, marp_pas, mnz_pas;
  int8_t   mac_tsp;        // ce que l'arpège de la macro ajoute
  uint8_t  mac_vol;        // MD_VIDE = la macro ne dit rien du volume
  uint8_t  mac_nz;         // MD_VIDE = elle ne dit rien du grain de bruit
  // ⚠️ « L'INSTRUMENT A UNE MACRO », pas « la macro dit quelque chose de non
  // nul ». Sans ce drapeau, un pas d'arpège valant ZÉRO — le retour à la
  // note de base, le pas le plus courant d'un arpège — était pris pour
  // « rien à dire » et la voie gardait la hauteur du pas précédent. Relevé
  // sur TUTU : la macro (+12, 0, +7) sonnait +12, +12, +7 ; la fondamentale
  // n'était jamais jouée.
  uint8_t  mac_actif;

  // ── LE BLOC DE LA COLONNE SONG ────────────────────────────────────────
  // La première ligne du groupe contigu que cette voie joue. On y revient à
  // la première case vide — pas en ligne 00, et pas là où la lecture est
  // entrée. Voir cale_chain().
  int      bloc_haut;

  // ── L'ENVELOPPE PSG, TROIS POINTS ─────────────────────────────────────
  // ⚠️ ELLE N'ÉTAIT DÉROULÉE PAR PERSONNE. La page instrument la montrait, on
  // pouvait la régler, et rien ne la jouait : une note PSG sonnait à volume
  // constant jusqu'à la suivante. Sur la voie de bruit ça s'entendait comme
  // un « crrshh » continu au lieu d'un coup sec — c'est ce défaut-là, pas le
  // mode de bruit.
  //
  // La vitesse d'un point dit à quelle allure REJOINDRE l'amplitude du point
  // suivant ; sans point suivant, la cible est le silence. Vitesse 0 tient
  // l'amplitude indéfiniment. C'est la règle de la DS, reprise telle quelle.
  uint8_t  env_pt;
  int16_t  env_niv;        // niveau au 1/256, de 0 à 15*256
  uint8_t  env_vif;        // 0 = la note s'est éteinte d'elle-même

  // ── L'état d'une commande, entre les lignes ───────────────────────────
  // ⚠️ C'est ce qui distingue une commande à lettre d'une commande MD : la
  // seconde écrit un registre et s'en va, la première VIT. Un vibrato doit se
  // souvenir de sa phase d'un tick à l'autre, un portamento de sa cible, un
  // pitch bend de ce qu'il a déjà parcouru.
  uint8_t  cmd;            // MD_VIDE = aucune commande en cours
  uint8_t  cmdval;
  int16_t  fin;            // désaccord courant, en 256ᵉ de demi-ton
  uint8_t  phase;          // le compteur de l'arpège et du vibrato
  uint8_t  cible;          // la note visée par le portamento
  uint8_t  coupe;          // K : couper après tant de ticks, 0 = jamais
  uint8_t  retard;         // D : retarder de tant de ticks
  md_ligne_phrase differee;// la ligne qu'on garde pendant le retard
} voie_t;

static voie_t voies[MD_CANAUX];

static uint8_t vol_global = 15;   // M, commun à tout le morceau

// ⚠️ J ET N NE S'EXÉCUTENT PAS AU MILIEU D'UNE LIGNE. Sauter pendant qu'une
// voie a déjà joué et pas les autres les désynchroniserait. On note la
// demande, et on la satisfait quand la ligne est finie — pour toutes les voies
// à la fois.
static uint8_t saut_demande, saut_vise;
static uint8_t rupture_demandee, rupture_ligne;

// L'adresse d'une ligne de table dans le morceau.
static uint32_t table_base(uint8_t tab, int lig) {
  return MD_OFF_TABLES
       + ((uint32_t)tab * MD_LIGNES_TABLE + (uint32_t)lig) * MD_TABLE_OCTETS;
}

// ⚠️ TOUT L'ÉTAT DE JEU D'UNE VOIE, remis à neuf. md_lecture_demarre ne
// remettait que la note et la position : la commande en cours, son compteur,
// le report d'un D, la coupure d'un K et le désaccord d'un P survivaient à un
// arrêt. On relançait la lecture et la voie repartait avec les restes de la
// précédente — au mieux un désaccord, au pire une voie muette.
static void voie_remet_a_zero(voie_t *v) {
  v->eff[0] = v->eff[1] = MD_E_RIEN;
  v->effval[0] = v->effval[1] = 0;
  v->vol_delta = 0; v->vib_prof = 0; v->retrig = 0;
  for (int k = 0; k < MD_LIGNES_PHRASE; k++) v->hop_ph[k] = 0;
  v->cmd = MD_VIDE; v->cmdval = 0; v->phase = 0;
  v->fin = 0; v->cible = 0; v->coupe = 0;
  v->retard = 0;
  v->table = MD_VIDE;
  v->mac_actif = 0; v->mac_tsp = 0;
  v->mac_vol = MD_VIDE; v->mac_nz = MD_VIDE;
  v->mvol_pas = v->marp_pas = v->mnz_pas = 0;
  v->env_pt = 0; v->env_niv = 0; v->env_vif = 0;
}

static void table_remet_a_zero(voie_t *v) {
  for (int s = 0; s < 3; s++) {
    v->table_pas[s] = 0;
    for (int l = 0; l < MD_LIGNES_TABLE; l++) v->hop_faits[s][l] = 0;
  }
}

// ── H, LE SAUT DE TABLE ───────────────────────────────────────────────────
// Le premier chiffre dit combien de fois sauter avant de passer outre, ZÉRO
// voulant dire « à l'infini » ; le second dit la ligne visée. C'est ce qui
// fait boucler une table sur ses trois premières lignes au lieu de dérouler
// les seize. Repris du projet DS (md_table_resolve_hops), y compris sa garde :
// une table entièrement remplie de H tournerait sinon sans fin dans ce tick.
//
// ⚠️ LE FLUX DU VOLUME N'EN TIENT PAS COMPTE, comme sur la DS. Le H vit dans
// une colonne CMD, et le volume n'en a pas : lui faire suivre le saut de la
// colonne d'à côté reviendrait à lui inventer une commande.
static void hop_resout(voie_t *v, int s) {
  if (s == 0 || v->table >= MD_MAX_TABLES) return;
  for (int garde = 0; garde <= MD_LIGNES_TABLE; garde++) {
    const int lig = v->table_pas[s] & (MD_LIGNES_TABLE - 1);
    const uint32_t b = table_base(v->table, lig);
    const uint8_t cmd = md_lit(b + (uint32_t)(s == 1 ? 2 : 4));
    const uint8_t val = md_lit(b + (uint32_t)(s == 1 ? 3 : 5));
    if (cmd == MD_VIDE || md_cmd_lettre(cmd) != 'H') return;
    const int fois = (val >> 4) & 15, but = val & 15;
    if (fois == 0) {
      v->table_pas[s] = (uint8_t)but;
    } else if (v->hop_faits[s][lig] < fois) {
      v->hop_faits[s][lig]++;
      v->table_pas[s] = (uint8_t)but;
    } else {
      v->hop_faits[s][lig] = 0;   // boucle finie : on passe à la suite
      v->table_pas[s] = (uint8_t)((lig + 1) & (MD_LIGNES_TABLE - 1));
    }
  }
}

// ── LES VOIES COUPÉES ─────────────────────────────────────────────────────
// Une voie coupée avance quand même dans le morceau — elle ne SONNE pas. Sans
// ça, la rallumer la ferait repartir au milieu de nulle part et le morceau se
// désynchroniserait.
static uint16_t voies_muettes;

int md_lecture_muette(int c) {
  return (c >= 0 && c < MD_CANAUX) ? ((voies_muettes >> c) & 1) : 0;
}

void md_lecture_muet_bascule(int c) {
  if (c < 0 || c >= MD_CANAUX) return;
  voies_muettes ^= (uint16_t)(1u << c);
  if (!md_lecture_muette(c)) return;
  // On la fait taire TOUT DE SUITE : attendre la note suivante laisserait
  // sonner celle qui tient, et couper une voie ne se verrait pas.
  voies[c].note = 0;
  if (c == MD_PCM_VOIE)   md_pcm_arrete();
  else if (c < 6)         md_fm_note_off(c);
  else if (c == 9)        md_psg_note_off(3);
  else                    md_psg_note_off(c - 6);
}

// ── LES PREMIERES NOTES PCM, RELEVEES ─────────────────────────────────────
// ⚠️ Le mouchard PCM n'etait branche que sur l'AUDITION d'un echantillon, pas
// sur la lecture : l'anneau revenait vide de la console alors que le PCM
// jouait. On releve donc les douze premieres notes de chaque lecture — c'est
// la ou le defaut s'entend, et ca borne le cout : ecrire la sauvegarde coute
// cher, et on est sous l'interruption video.
static uint8_t mouchard_reste;

static void pcm_mouchard(uint8_t ech, uint8_t note, uint8_t vol) {
  if (!mouchard_reste) return;
  mouchard_reste--;
  md_pcm_bilan_t b;
  md_pcm_bilan(&b);
  uint8_t e[MD_PCM_EVT];
  for (int k = 0; k < MD_PCM_EVT; k++) e[k] = 0;
  e[0] = ech;              e[1] = note;              e[2] = vol;
  e[3] = md_pcm_etat();
  e[4] = (uint8_t)b.pointeur;   e[5] = (uint8_t)(b.pointeur >> 8);
  e[6] = (uint8_t)b.longueur;   e[7] = (uint8_t)(b.longueur >> 8);
  e[8] = (uint8_t)b.pas;        e[9] = (uint8_t)(b.pas >> 8);
  e[10] = b.commences;          e[11] = b.finis;
  e[12] = (uint8_t)b.fin_hl;    e[13] = (uint8_t)(b.fin_hl >> 8);
  e[14] = (uint8_t)b.reste_de;  e[15] = (uint8_t)(b.reste_de >> 8);
  e[16] = b.commande;      e[17] = b.premier_lu;     e[18] = b.dernier_lu;
  e[19] = (uint8_t)(pcm_banque[pcm_offset[ech]]);   /* ce qu'on ATTENDAIT */
  md_pcm_anneau_pose(e);
}

static int en_cours;
static int ticks;          // ticks écoulés depuis le dernier changement de ligne
static int reste;          // fraction de tick reportée d'une image à l'autre
static int images_par_s;   // 50 sur une console PAL, 60 sur une NTSC

// ── La portée ──────────────────────────────────────────────────────────────
static int     portee = MD_PORTEE_SONG;
static int     portee_canal;
static uint8_t portee_id;
static int     portee_ligne;

static int borne(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static int est_pcm(int c, uint8_t ins);

// ── LE HAUT DU BLOC ────────────────────────────────────────────────────────
// On remonte tant que les cases sont remplies. Une case vide est son propre
// haut de bloc : il n'y a alors rien à jouer.
static int haut_du_bloc(int c, int depart) {
  if (depart < 0 || depart >= MD_SONG_LIGNES) return 0;
  if (md_song_lit(c, depart) == MD_VIDE) return depart;
  int t = depart;
  while (t > 0 && md_song_lit(c, t - 1) != MD_VIDE) t--;
  return t;
}

// ── Trouver de quoi jouer ──────────────────────────────────────────────────
// ⚠️ CHAQUE VOIE JOUE SON BLOC CONTIGU, ET REBOUCLE EN HAUT DE CE BLOC.
//
// C'est la règle de LSDJ, celle du tracker de référence, celle du projet DS —
// et elle manquait ici depuis le début. On cherchait la prochaine case non
// vide N'IMPORTE OÙ dans la colonne, en rebouclant sur les 256 lignes : une
// ligne isolée, avec du vide au-dessus et au-dessous, ne bouclait donc pas.
// Elle sautait au groupe suivant, ou revenait tout en haut.
//
// Trois points, dans les mots du fichier de référence :
//   « Play CONTIGUOUS non-empty song rows; stop at the first empty. »
//   « A song loop must return to the TOP of its block, not to wherever
//     playback entered it. »
//   Et les voies sont INDÉPENDANTES : chacune boucle sur son bloc à elle.
static int cale_chain(int c, int depart) {
  voie_t *v = &voies[c];
  int r = depart;
  if (r < 0 || r >= MD_SONG_LIGNES || md_song_lit(c, r) == MD_VIDE) {
    r = v->bloc_haut;                       // la case vide : on reboucle
    if (r < 0 || r >= MD_SONG_LIGNES || md_song_lit(c, r) == MD_VIDE) {
      v->actif = 0;                         // bloc introuvable : silence franc
      return 0;
    }
  }
  const uint8_t ch = md_song_lit(c, r);
  if (ch >= MD_MAX_CHAINS) { v->actif = 0; return 0; }
  v->song_ligne = r; v->chain = ch; v->chain_ligne = 0;
  return 1;
}

void md_lecture_pose_portee(int p, int canal, uint8_t id, int ligne) {
  portee = p; portee_canal = canal; portee_id = id; portee_ligne = ligne;
}

// Positionne la voie sur la prochaine ligne de chain qui porte une phrase.
// En portée CHAIN on reste DANS le chain : arrivé au bout, on revient à sa
// première ligne au lieu de retomber dans le morceau.
static int cale_phrase(int c) {
  voie_t *v = &voies[c];
  if (portee == MD_PORTEE_CHAIN) {
    for (int essais = 0; essais < MD_LIGNES_CHAIN * 2; essais++) {
      if (v->chain_ligne >= MD_LIGNES_CHAIN) v->chain_ligne = 0;
      uint8_t ph; int8_t tsp;
      md_chain_lit(v->chain, v->chain_ligne, &ph, &tsp);
      if (ph != MD_VIDE && ph < MD_MAX_PHRASES) {
        v->phrase = ph; v->transpose = tsp; v->phrase_ligne = 0;
        return 1;
      }
      v->chain_ligne++;
    }
    v->actif = 0;   // chain entièrement vide : silence franc
    return 0;
  }
  for (int essais = 0; essais < MD_SONG_LIGNES + MD_LIGNES_CHAIN; essais++) {
    if (v->chain_ligne >= MD_LIGNES_CHAIN) {
      // Chain terminé : on passe à la case SONG suivante.
      if (!cale_chain(c, v->song_ligne + 1)) return 0;
      continue;
    }
    uint8_t ph; int8_t tsp;
    md_chain_lit(v->chain, v->chain_ligne, &ph, &tsp);
    if (ph != MD_VIDE && ph < MD_MAX_PHRASES) {
      v->phrase = ph; v->transpose = tsp; v->phrase_ligne = 0;
      return 1;
    }
    // ⚠️ UNE CHAÎNE S'ARRÊTE À SA PREMIÈRE LIGNE VIDE. Elle ne saute PAS
    // par-dessus pour aller chercher une phrase plus bas — même règle que la
    // DS. On passe donc à la case SONG suivante, qui rebouclera d'elle-même
    // en haut du bloc si elle est vide.
    if (!cale_chain(c, v->song_ligne + 1)) return 0;
  }
  v->actif = 0;
  return 0;
}

void md_lecture_init(void) {
  md_puces_init();
#ifdef MD_HORS_CONSOLE
  images_par_s = 50;        /* le banc d'essai suppose une console PAL */
#else
  images_par_s = (*(volatile uint8_t *)0xA10001 & 0x40) ? 50 : 60;
#endif
  en_cours = 0;
  for (int c = 0; c < MD_CANAUX; c++) voies[c].actif = 0;
}

void md_lecture_demarre(int ligne_song) {
  md_z80_prepare();   // au cas où quelque chose l'aurait réinitialisé
  md_puces_init();
  ticks = 0; reste = 0;
  // ⚠️ M vaut pour TOUT le morceau : sans cette remise, un morceau joué après
  // un autre qui baissait le volume général démarrait déjà atténué, et on
  // cherchait la panne dans l'instrument.
  vol_global = 15;
  saut_demande = 0; rupture_demandee = 0;
  mouchard_reste = 12;   // on releve les douze premieres notes PCM

  for (int c = 0; c < MD_CANAUX; c++) {
    voie_t *v = &voies[c];
    v->actif = 0; v->instr = 0; v->note = 0; v->transpose = 0;
    v->chain_ligne = 0; v->phrase_ligne = 0;
    voie_remet_a_zero(v);
    table_remet_a_zero(v);
  }

  if (portee == MD_PORTEE_SONG) {
    for (int c = 0; c < MD_CANAUX; c++) {
      voies[c].actif = 1;
      // Le bloc se fixe AU DÉPART, d'après la ligne où l'on lance : c'est lui
      // qui dira où reboucler.
      voies[c].bloc_haut = haut_du_bloc(c, ligne_song);
      if (cale_chain(c, ligne_song)) cale_phrase(c);
    }
  } else {
    // Une SEULE voie sonne : celle de la colonne d'où l'on est descendu.
    // C'est ce qui permet d'écouter un instrument isolément.
    const int c = (portee_canal >= 0 && portee_canal < MD_CANAUX) ? portee_canal : 0;
    voie_t *v = &voies[c];
    v->actif = 1;
    if (portee == MD_PORTEE_CHAIN) {
      v->chain = portee_id;
      v->chain_ligne = (portee_ligne >= 0 && portee_ligne < MD_LIGNES_CHAIN)
                       ? portee_ligne : 0;
      if (!cale_phrase(c)) v->actif = 0;
    } else {
      v->phrase = portee_id;
      v->phrase_ligne = (portee_ligne >= 0 && portee_ligne < MD_LIGNES_PHRASE)
                        ? portee_ligne : 0;
      v->transpose = 0;
    }
  }
  en_cours = 1;
}

void md_lecture_arrete(void) {
  en_cours = 0;
  md_puces_silence();
  for (int c = 0; c < MD_CANAUX; c++) { voies[c].actif = 0; voies[c].note = 0; }
}

int md_lecture_en_cours(void) { return en_cours; }

int md_lecture_ligne_song(int canal) {
  return (canal >= 0 && canal < MD_CANAUX && voies[canal].actif)
         ? voies[canal].song_ligne : -1;
}
int md_lecture_phrase(int c) {
  return (c >= 0 && c < MD_CANAUX && voies[c].actif) ? voies[c].phrase : -1;
}

// Quelle table cette voie déroule, et où en est chacun de ses trois flux.
// C'est ce qui permet à la page TABLE de montrer sa lecture — sans quoi on
// pose un H et rien à l'écran ne dit s'il agit.
int md_lecture_table(int c) {
  if (c < 0 || c >= MD_CANAUX || !voies[c].actif) return -1;
  return (voies[c].table < MD_MAX_TABLES) ? voies[c].table : -1;
}
int md_lecture_table_pos(int c, int flux) {
  if (c < 0 || c >= MD_CANAUX || flux < 0 || flux > 2) return -1;
  const voie_t *v = &voies[c];
  if (!v->actif || !v->note || v->table >= MD_MAX_TABLES) return -1;
  return v->table_pas[flux] & (MD_LIGNES_TABLE - 1);
}
int md_lecture_ligne_phrase(int c) {
  return (c >= 0 && c < MD_CANAUX && voies[c].actif) ? voies[c].phrase_ligne : -1;
}
int md_lecture_chain(int c) {
  return (c >= 0 && c < MD_CANAUX && voies[c].actif) ? voies[c].chain : -1;
}
int md_lecture_ligne_chain(int c) {
  return (c >= 0 && c < MD_CANAUX && voies[c].actif) ? voies[c].chain_ligne : -1;
}

// ── Audition ──────────────────────────────────────────────────────────────
// Elle emprunte la voie du canal édité. Pendant la lecture, cette voie est
// donc brièvement détournée — c'est le compromis de la référence, et il est
// juste : on veut entendre la note DANS son contexte, sur le bon type de puce.
static int audition_voie = -1;

// ── Les notes de base des echantillons ────────────────────────────────────
// La banque est en ROM : on ne peut pas y ecrire. On en garde une copie
// vivante, initialisee a la premiere lecture, pour que la page instrument
// puisse regler la note de base comme sur la DS.
//
// ⚠️ Elle ne survit pas a une coupure : les echantillons viennent de la ROM et
// non du morceau, donc le morceau n'a aucun endroit ou la ranger. Ca changera
// quand la banque sera portee par le fichier.
static uint8_t note_base[32];
static int notes_pretes;
static void notes_prepare(void) {
  if (notes_pretes) return;
  for (int i = 0; i < 32; i++) note_base[i] = pcm_note[i];
  notes_pretes = 1;
}
uint8_t md_pcm_note(int si) {
  notes_prepare();
  return (si >= 0 && si < 32) ? note_base[si] : 0;
}
void md_pcm_note_pose(int si, uint8_t n) {
  notes_prepare();
  if (si >= 0 && si < 32) note_base[si] = n;
}


void md_lecture_audition(int canal, uint8_t note, uint8_t instr) {
  if (canal < 0 || canal >= MD_CANAUX) return;
  if (!note || note == MD_VIDE) return;
  md_lecture_audition_stop();

  const uint8_t ins = instr ? instr : 1;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;
  if (est_pcm(canal, ins)) {
    const uint8_t si = md_lit(base + 61);
    if (si < 32 && pcm_longueur[si])
      md_pcm_joue((uint32_t)pcm_banque + pcm_offset[si], pcm_longueur[si],
                  (int)note - (int)md_pcm_note(si), md_lit(base + 62));
  } else if (canal < 6) {
    if (canal == MD_PCM_VOIE) md_pcm_actif(0);
    md_fm_charge(canal, base);
    md_fm_note_on(canal, note);
  } else if (canal == 9) {
    md_psg_bruit((uint8_t)(md_lit(base + 52) & 7), note, 15);
  } else {
    md_psg_note_on(canal - 6, note, 15);
  }
  audition_voie = canal;
}

// ── Un réglage se règle EN L'ÉCOUTANT ─────────────────────────────────────
// ⚠️ Sans ça, éditer un instrument ne s'entendait qu'à la note SUIVANTE : on
// tournait un paramètre dans le vide, puis tout tombait d'un coup — d'où
// l'impression de sauts brutaux, et de volumes qui changent sans prévenir
// quand on parcourt les algorithmes.
//
// La DS réécrit la voix de tous les canaux qui portent cet instrument dès
// qu'un octet change (md_push_instrument_update). On fait pareil.
void md_lecture_instr_maj(uint8_t ins) {
  if (!ins || ins > MD_MAX_INSTR) return;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;
  for (int c = 0; c < 6; c++) {
    if (c == MD_PCM_VOIE) continue;         // le convertisseur n'a pas de voix
    if (voies[c].instr != ins) continue;
    md_fm_charge(c, base);
  }
  if (audition_voie >= 0 && audition_voie < 6 && audition_voie != MD_PCM_VOIE)
    md_fm_charge(audition_voie, base);
}

void md_lecture_audition_stop(void) {
  if (audition_voie < 0) return;
  if (audition_voie < 6) md_fm_note_off(audition_voie);
  else                   md_psg_note_off(audition_voie - 6);
  audition_voie = -1;
}

// La 6ᵉ voie FM est le convertisseur. Un instrument y est du PCM s'il le dit
// (kind = 3) — ET AUSSI s'il ne dit rien (kind = 0), ce qui est le cas de tout
// instrument neuf : sans cette règle, poser une note sur cette colonne
// déclenchait le patch FM d'usine, très fort et très laid.
static int est_pcm(int c, uint8_t ins) {
  if (c != MD_PCM_VOIE || !ins) return 0;
  const uint8_t kind =
      md_lit(MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS + 60);
  return (kind == 3) || (kind == 0);
}

// ── La hauteur d'une voie, désaccord compris ──────────────────────────────
// Un seul endroit qui sait poser une hauteur : sinon vibrato, portamento et
// pitch bend divergeraient sur les cas limites, et sur la voie de bruit
// surtout, qui ne se règle pas comme les autres.
static void joue_note(int c, const md_ligne_phrase *pr);
static void commande_md(int c, uint8_t rang, uint8_t val);

static void pose_hauteur(int c) {
  const voie_t *v = &voies[c];
  if (!v->note) return;
  if (c == MD_PCM_VOIE) return;            // le convertisseur ne s'accorde pas
  if (c < 6)            md_fm_hauteur(c, v->note, v->fin);
  else if (c == 9) {
    // Le bruit ne suit la note que dans les modes verrouillés sur le ton 3.
    const uint8_t ins = v->instr ? v->instr : 1;
    const uint8_t mode = md_lit(MD_OFF_INSTR
                    + (uint32_t)(ins - 1) * MD_INSTR_OCTETS + 52) & 7;
    if ((mode & 3) == 3) md_psg_hauteur(2, v->note, v->fin);
  }
  else md_psg_hauteur(c - 6, v->note, v->fin);
}

// ── LE VOLUME D'UNE VOIE ──────────────────────────────────────────────────
// ⚠️ IL N'EXISTAIT PAS. La vélocité d'une ligne ne faisait rien sur les six
// voies FM : le YM2612 n'a pas de registre de volume, il faut atténuer ses
// opérateurs porteuses, et personne ne le faisait. Toutes les commandes de
// volume — M, Z, B, E, F et leurs équivalents MD — n'avaient donc rien où
// agir. Un seul endroit sait poser un volume, comme pour la hauteur.

static int niveau_de(const voie_t *v) {
  int n = (int)v->vol + v->vol_delta;
  n = borne(n, 0, 15);
  return n * (int)vol_global / 15;
}

static void pose_volume(int c) {
  const voie_t *v = &voies[c];
  if (!v->note) return;
  if (c == MD_PCM_VOIE) return;   // le convertisseur porte son gain à part
  if (c < 6) md_fm_volume(c, (uint8_t)niveau_de(v));
  // Les voies PSG passent par table_pas, qui mêle enveloppe et macro : leur
  // niveau s'y calcule déjà, et l'y écrire deux fois les ferait sauter.
}

// ── Les commandes à LETTRE, un pas par tick ───────────────────────────────
// ⚠️ Elles ne s'exécutent PAS à la ligne, mais à chaque TICK. C'est ce qui
// fait la différence entre un arpège et trois notes écrites : l'arpège tourne
// plus vite que le tempo. La ligne ne fait que les ARMER.
//
// `t` est le numéro du tick dans la ligne : zéro au moment où la ligne tombe.
static void arme_commande(int c, const md_ligne_phrase *r);

static void note_coupe(int c) {
  voie_t *v = &voies[c];
  if (c == MD_PCM_VOIE) md_pcm_arrete();
  else if (c < 6)       md_fm_note_off(c);
  else                  md_psg_note_off(c - 6);
  v->coupe = 0; v->note = 0;
}

// ── UN EFFET, UN PAS ──────────────────────────────────────────────────────
// Appelée pour CHACUN des deux emplacements : la lettre et le code MD y
// passent par le même chemin, donc ils se comportent pareil par construction.
// `t` est le numéro du tick DANS LA LIGNE — c'est lui qui donne à l'arpège sa
// rotation et au retard son échéance.
static void effet_pas(int c, int e, uint8_t val, int t) {
  voie_t *v = &voies[c];
  const uint8_t x = (uint8_t)(val >> 4), y = (uint8_t)(val & 15);

  switch (e) {
    case MD_E_ARPEGE: {   // la note, puis +x, puis +y, un pas par tick
      const int k = t % 3;
      const int n = (int)v->note + (k == 1 ? x : (k == 2 ? y : 0));
      if (c < 6 && c != MD_PCM_VOIE) md_fm_hauteur(c, n, v->fin);
      else if (c >= 6 && c != 9)     md_psg_hauteur(c - 6, n, v->fin);
      break;
    }
    case MD_E_VIBRATO:
    case MD_E_VIB_VOL: {  // x la vitesse, y la profondeur
      // Une triangulaire sur seize pas. Pas de table de sinus : elle coûterait
      // de la ROM pour une différence qu'on n'entend pas sur un vibrato de
      // tracker, et LSDJ lui-même n'en a pas.
      const uint8_t prof = v->vib_prof ? v->vib_prof : y;
      v->phase = (uint8_t)((v->phase + x) & 15);
      const int p = (v->phase < 8) ? v->phase : (16 - v->phase);   // 0..8
      v->fin = (int16_t)(((p - 4) * (int)prof * 4));
      pose_hauteur(c);
      if (e == MD_E_VIB_VOL) { v->vol_delta--; pose_volume(c); }
      break;
    }
    case MD_E_PITCH:      // x monte, y descend, un pas par tick
      v->fin = (int16_t)(v->fin + (int)x * 4 - (int)y * 4);
      v->fin = (int16_t)borne(v->fin, -4096, 4096);
      pose_hauteur(c);
      break;
    case MD_E_PORTA_HAUT:
      v->fin = (int16_t)borne(v->fin + (int)val * 2, -4096, 4096);
      pose_hauteur(c);
      break;
    case MD_E_PORTA_BAS:
      v->fin = (int16_t)borne(v->fin - (int)val * 2, -4096, 4096);
      pose_hauteur(c);
      break;
    case MD_E_PORTA_TON:
    case MD_E_PORTA_VOL:  // on glisse vers la cible
      if (v->cible && v->cible != v->note) {
        const int pas = (int)val * 2;
        const int ecart = ((int)v->cible - (int)v->note) * 256 - v->fin;
        if (ecart > 0)      v->fin += (int16_t)((ecart < pas) ? ecart : pas);
        else if (ecart < 0) v->fin -= (int16_t)((-ecart < pas) ? -ecart : pas);
        pose_hauteur(c);
      }
      if (e == MD_E_PORTA_VOL) { v->vol_delta--; pose_volume(c); }
      break;
    case MD_E_TREMOLO: {  // x la vitesse, y la profondeur, sur le VOLUME
      v->phase = (uint8_t)((v->phase + x) & 15);
      const int p = (v->phase < 8) ? v->phase : (16 - v->phase);
      v->vol_delta = (int8_t)(((p - 4) * (int)y) / 8);
      pose_volume(c);
      break;
    }
    case MD_E_VOL_SLIDE:  // x monte, y descend, un pas par tick
      v->vol_delta = (int8_t)borne(v->vol_delta + (int)x - (int)y, -15, 15);
      pose_volume(c);
      break;
    case MD_E_RETRIG:     // on rejoue la note tous les y ticks
      if (y && ++v->retrig >= y) {
        v->retrig = 0;
        if (c < 6 && c != MD_PCM_VOIE) md_fm_note_on(c, v->note);
        else if (c == 9) { /* le bruit se relance par sa macro */ }
        else if (c >= 6) md_psg_note_on(c - 6, v->note, (uint8_t)(15 - niveau_de(v)));
      }
      break;
    case MD_E_COUPE:      // coupure après tant de ticks
      if (v->coupe && t >= v->coupe) note_coupe(c);
      break;
    default: break;
  }
}

static void commande_tick(int c, int t) {
  voie_t *v = &voies[c];

  // D : la ligne mise de côté tombe maintenant.
  if (v->retard) {
    if (t >= v->retard) {
      v->retard = 0;
      md_ligne_phrase g = v->differee;
      joue_note(c, &g);
      arme_commande(c, &g);
      if (g.mdcmd != MD_VIDE) commande_md(c, g.mdcmd, g.mdval);
    }
    return;
  }
  if (!v->note) return;
  // Les DEUX emplacements avancent, la lettre puis le code : c'est l'ordre
  // d'affichage, donc celui qu'on lit sur l'écran.
  for (int s = 0; s < 2; s++)
    if (v->eff[s] != MD_E_RIEN) effet_pas(c, v->eff[s], v->effval[s], t);
}

// ── Les commandes MD ──────────────────────────────────────────────────────
// Elles retouchent la voix EN PLACE, sans la recharger. Chacune écrit un
// registre et un seul, donc elles coûtent presque rien et s'entendent tout de
// suite.
//
// ⚠️ Elles ne valent QUE pour la voie qui les porte, et jusqu'à la note
// suivante — qui recharge la voix et les efface. Une commande ne doit pas
// abîmer l'instrument pour tout le morceau.
static void commande_md(int c, uint8_t rang, uint8_t val) {
  if (c >= 6 || c == MD_PCM_VOIE) return;   // la puce FM seulement
  const uint8_t x = (uint8_t)(val >> 4), y = (uint8_t)(val & 15);
  const uint8_t ins = voies[c].instr ? voies[c].instr : 1;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;

  switch (md_mdcmd_action(rang)) {
    case MD_P_LFO:   md_fm_pose_lfo(x != 0, y); break;
    case MD_P_FB:    md_fm_pose_alg_fb(c, md_lit(base + 44) & 7, val & 7); break;
    case MD_P_ALG:   md_fm_pose_alg_fb(c, val & 7, md_lit(base + 45) & 7); break;
    case MD_P_TL1:   md_fm_pose_tl(c, 0, val); break;
    case MD_P_TL2:   md_fm_pose_tl(c, 1, val); break;
    case MD_P_TL3:   md_fm_pose_tl(c, 2, val); break;
    case MD_P_TL4:   md_fm_pose_tl(c, 3, val); break;
    // ⚠️ Le quartet HAUT désigne l'opérateur, en comptant à partir de UN :
    // c'est la convention de DefleMask, et un morceau importé en dépend.
    case MD_P_MUL:   md_fm_pose_mul(c, (int)x - 1, y); break;
    case MD_P_AR1:   md_fm_pose_ar(c, 0, val & 31); break;
    case MD_P_AR2:   md_fm_pose_ar(c, 1, val & 31); break;
    case MD_P_AR3:   md_fm_pose_ar(c, 2, val & 31); break;
    case MD_P_AR4:   md_fm_pose_ar(c, 3, val & 31); break;
    case MD_P_ARALL:
      for (int op = 0; op < 4; op++) md_fm_pose_ar(c, op, val & 31);
      break;
    case MD_P_PAN:
      md_fm_pose_pan(c, val, md_lit(base + 46) & 3, md_lit(base + 47) & 7);
      break;
    default: break;
  }
}

// ── Jouer une ligne de phrase ──────────────────────────────────────────────
// ⚠️ L'ORDRE COMPTE : la note D'ABORD, la commande ENSUITE.
// Une commande retouche la voix qui vient d'être chargée. Dans l'autre sens
// le chargement de la note l'effacerait aussitôt, et on chercherait longtemps
// pourquoi une commande « ne fait rien ». Et elle s'applique AUSSI sur une
// ligne sans note : c'est même son usage principal, retoucher une note tenue.
static void joue_note(int c, const md_ligne_phrase *pr);

static void joue(int c) {
  voie_t *v = &voies[c];
  md_ligne_phrase r;
  md_phrase_lit(v->phrase, v->phrase_ligne, &r);

  if (r.instr) v->instr = r.instr;

  // ── D : le RETARD, qui se décide AVANT tout le reste ──────────────────
  // Retarder une note veut dire ne pas la jouer maintenant. On garde donc la
  // ligne entière de côté et on la rejouera au bon tick — sinon on jouerait
  // la note puis on la « retarderait », ce qui ne veut rien dire.
  const int retarde =
      (r.cmd != MD_VIDE   && md_cmd_effet(r.cmd) == MD_E_RETARD && r.val) ||
      (r.mdcmd != MD_VIDE && md_mdcmd_effet(r.mdcmd) == MD_E_RETARD && r.mdval);
  if (retarde) {
    const uint8_t valeur =
        (r.cmd != MD_VIDE && md_cmd_effet(r.cmd) == MD_E_RETARD) ? r.val : r.mdval;
    // ⚠️ LE RETARD DOIT TOMBER DANS LA LIGNE. Il était pris tel quel : un D20
    // sur une ligne de six ticks attendait un instant qui n'arrivait jamais,
    // et comme commande_tick sort tant qu'un report est en cours, LA VOIE
    // RESTAIT MUETTE JUSQU'À LA FIN DU MORCEAU — plus une note, plus une
    // commande. C'est ce qui faisait croire que les commandes ne marchaient
    // pas : il suffisait d'un D quelque part pour tout éteindre derrière.
    const uint8_t vit = md_song_vitesse();
    const uint8_t max = (uint8_t)(vit > 1 ? vit - 1 : 1);
    v->differee = r;
    v->retard = (valeur > max) ? max : valeur;
    return;
  }

  // ── L : LA NOTE ÉCRITE EST UNE CIBLE, PAS UNE ATTAQUE ─────────────────
  // ⚠️ Elle était jouée comme les autres : on entendait la note d'arrivée
  // tout de suite, puis un glissando qui ne glissait plus vers rien. Un
  // portamento garde la note en cours et la tire vers celle qui est écrite.
  const int e0 = (r.cmd != MD_VIDE) ? md_cmd_effet(r.cmd) : MD_E_RIEN;
  const int e1 = (r.mdcmd != MD_VIDE) ? md_mdcmd_effet(r.mdcmd) : MD_E_RIEN;
  const int porta = (e0 == MD_E_PORTA_TON || e0 == MD_E_PORTA_VOL
                     || e1 == MD_E_PORTA_TON || e1 == MD_E_PORTA_VOL);

  if (porta && v->note && r.note && r.note != MD_VIDE) {
    v->cible = (uint8_t)borne((int)r.note + v->transpose, 1, 108);
    if (r.instr) v->instr = r.instr;
  } else {
    joue_note(c, &r);
  }
  arme_commande(c, &r);
  if (r.mdcmd != MD_VIDE) commande_md(c, r.mdcmd, r.mdval);
}

// Ce qu'une ligne fait de sa commande : les unes agissent TOUT DE SUITE, les
// autres s'installent pour être déroulées tick après tick.
// ── UN EFFET, ARMÉ ────────────────────────────────────────────────────────
// Les uns agissent TOUT DE SUITE et n'ont rien à dérouler — poser un tempo,
// un panoramique, un saut. Les autres s'installent dans leur emplacement et
// tournent tick après tick. C'est la seule différence, et elle se lit ici.
// Rend 1 quand l'effet a agi SUR-LE-CHAMP et n'a rien à dérouler ensuite.
static int effet_immediat(int c, int effet, uint8_t val) {
  voie_t *v = &voies[c];
  switch (effet) {
    case MD_E_RIEN:
      return 1;
    case MD_E_VITESSE:
      if (val) md_song_pose_vitesse(val);
      return 1;
    case MD_E_TEMPO:
      if (val) md_song_pose_bpm(val);
      return 1;
    case MD_E_VOL_GLOBAL:
      // Le volume général du morceau, 0-15. Il multiplie celui de chaque voie.
      vol_global = (uint8_t)borne(val, 0, 15);
      for (int k = 0; k < 6; k++)
        if (voies[k].actif && voies[k].note) pose_volume(k);
      return 1;
    case MD_E_PAN:
      if (c < 6 && c != MD_PCM_VOIE) {
        const uint8_t ins = v->instr ? v->instr : 1;
        const uint32_t b = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;
        md_fm_pose_pan(c, val, md_lit(b + 46) & 3, md_lit(b + 47) & 7);
      }
      return 1;
    case MD_E_TABLE:
      if (val < MD_MAX_TABLES) { v->table = val; table_remet_a_zero(v); }
      return 1;
    case MD_E_VIB_PROF:
      // W ne fait pas de vibrato : il REGLE la profondeur que V emploiera.
      v->vib_prof = (uint8_t)(val & 15);
      return 1;
    case MD_E_FINE:
      // Un désaccord fin, en seizièmes de demi-ton, centré sur 8.
      v->fin = (int16_t)(((int)(val & 15) - 8) * 16);
      pose_hauteur(c);
      return 1;
    case MD_E_SAUT:
      // On demande le saut ; il se fera à la fin de la ligne, pas au milieu.
      saut_vise = val; saut_demande = 1;
      return 1;
    case MD_E_RUPTURE:
      // La ligne de SONG suivante, en commençant à la ligne demandée.
      rupture_ligne = (uint8_t)(val & (MD_LIGNES_PHRASE - 1));
      rupture_demandee = 1;
      return 1;
    default:
      break;
  }
  return 0;
}

static void arme_effet(int c, int slot, int effet, uint8_t val) {
  voie_t *v = &voies[c];
  v->eff[slot] = MD_E_RIEN;
  if (effet_immediat(c, effet, val)) return;

  switch (effet) {
    case MD_E_COUPE:
      v->coupe = val ? val : 1;
      break;
    case MD_E_PORTA_TON:
    case MD_E_PORTA_VOL:
      // La cible est posée par joue(), qui sait s'il y avait une note sur la
      // ligne. L'écraser ici la remplacerait par la note en cours, et le
      // glissando n'aurait plus nulle part où aller.
      break;
    case MD_E_VIBRATO:
    case MD_E_VIB_VOL:
    case MD_E_PITCH:
      break;          // ceux-là gardent le désaccord en cours
    default:
      v->fin = 0;     // les autres repartent d'une hauteur droite
      break;
  }
  v->eff[slot] = (uint8_t)effet;
  v->effval[slot] = val;
  v->phase = 0;
  v->retrig = 0;
}

static void arme_commande(int c, const md_ligne_phrase *r) {
  voie_t *v = &voies[c];

  // ⚠️ Une ligne SANS commande annule celle d'avant, et remet la hauteur et
  // le volume droits. Sans ça un vibrato posé une fois durerait tout le
  // morceau.
  if (r->cmd == MD_VIDE && r->mdcmd == MD_VIDE) {
    if (v->eff[0] != MD_E_RIEN || v->eff[1] != MD_E_RIEN) {
      v->eff[0] = v->eff[1] = MD_E_RIEN;
      v->fin = 0; v->vol_delta = 0;
      pose_hauteur(c); pose_volume(c);
    }
    return;
  }

  // Les deux colonnes arment CHACUNE son emplacement. Le HOP n'en est pas un :
  // il ne se déroule pas, il déplace une position, et c'est avance() qui s'en
  // occupe.
  const int e0 = (r->cmd != MD_VIDE) ? md_cmd_effet(r->cmd) : MD_E_RIEN;
  const int e1 = (r->mdcmd != MD_VIDE) ? md_mdcmd_effet(r->mdcmd) : MD_E_RIEN;
  arme_effet(c, 0, (e0 == MD_E_HOP || e0 == MD_E_RETARD) ? MD_E_RIEN : e0, r->val);
  arme_effet(c, 1, (e1 == MD_E_RETARD) ? MD_E_RIEN : e1, r->mdval);
}

static void joue_note(int c, const md_ligne_phrase *pr) {
  voie_t *v = &voies[c];
  const md_ligne_phrase r = *pr;

  if (r.note == MD_VIDE) {                 // note-off explicite
    v->table = MD_VIDE;                    // plus de note, plus de table
    if (c == MD_PCM_VOIE) md_pcm_arrete();
    if (c < 6) md_fm_note_off(c); else md_psg_note_off(c - 6);
    v->note = 0;
    return;
  }
  if (r.note == 0) return;                 // rien sur cette ligne

  // La transposition du chain s'ajoute à la note, bornée à la tessiture.
  int n = (int)r.note + v->transpose;
  n = borne(n, 1, 108);
  v->note = (uint8_t)n;

  const uint8_t ins = v->instr ? v->instr : 1;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;

  // La table de l'instrument repart de son premier pas à chaque note : c'est
  // ce qui rend un arpège reproductible d'une note à l'autre.
  v->table = md_lit(base + 59);
  table_remet_a_zero(v);
  // Les macros repartent de leur premier pas, comme la table : une macro qui
  // reprendrait où elle s'est arrêtée donnerait une attaque différente à
  // chaque note.
  v->mvol_pas = v->marp_pas = v->mnz_pas = 0;
  v->mac_tsp = 0; v->mac_vol = MD_VIDE; v->mac_nz = MD_VIDE; v->mac_actif = 0;
  // L'enveloppe repart de son premier point. Un instrument sans enveloppe
  // définie sonne au maximum, comme avant.
  { const uint8_t a0 = md_lit(base + 53);
    v->env_pt = 0;
    v->env_niv = (int16_t)((a0 == MD_VIDE ? 15 : (a0 > 15 ? 15 : a0)) * 256);
    v->env_vif = 1; }
  v->vol = (r.vel != MD_VIDE) ? (uint8_t)((r.vel * 15) / 0x7F) : 15;
  // ⚠️ Le désaccord et l'atténuation d'une note précédente ne se reportent
  // pas sur celle-ci : une nouvelle note repart droite et à son volume.
  v->vol_delta = 0;

  if (est_pcm(c, ins)) {
    // L'échantillon vit en ROM : 47 Ko pour le seul morceau de référence, là
    // où la sauvegarde entière fait 32 Ko. Il ne pouvait pas être ailleurs.
    // Un echantillon se transpose comme une note : l'ecart a sa note de base
    // devient le pas de lecture. Sans ca toute la colonne sonnait a la meme
    // hauteur quelle que soit la note ecrite.
    const uint8_t si = md_lit(base + 61);
    if (si < 32 && pcm_longueur[si])
      // ⚠️ Deux niveaux se multiplient, comme sur la DS : la colonne VEL,
      // ligne par ligne, et le volume de l'instrument, une fois pour toutes.
      { int g = md_lit(base + 62);
        if (r.vel != MD_VIDE) g = g * (int)r.vel / 0x7F;
        if (g > 255) g = 255;
        md_pcm_joue((uint32_t)pcm_banque + pcm_offset[si], pcm_longueur[si],
                    n - (int)md_pcm_note(si), g);
        pcm_mouchard(si, (uint8_t)n, (uint8_t)g); }
    else
      md_pcm_actif(1);   // instrument PCM sans échantillon : la voie se tait
    return;
  }
  if (c == MD_PCM_VOIE) md_pcm_actif(0);   // retour à la synthèse

  if (c < 6) {
    // On recharge la voix à CHAQUE note. C'est plus d'écritures que nécessaire,
    // mais c'est juste : deux notes voisines peuvent porter des instruments
    // différents, et une voix chargée à moitié sonne faux. On optimisera quand
    // on aura MESURÉ que ça coûte trop cher.
    md_fm_charge(c, base);
    md_fm_note_on(c, (uint8_t)n);
    // La VÉLOCITÉ, enfin. Elle atténue les porteuses de l'algorithme en cours ;
    // md_fm_volume sait lesquelles le sont. Posée APRÈS le chargement, sinon
    // l'instrument la réécrirait avec ses propres Total Level.
    pose_volume(c);
  } else {
    uint8_t vol = 15;
    if (r.vel != MD_VIDE) vol = (uint8_t)((r.vel * 15) / 0x7F);
    if (c == 9) {
      const uint8_t bruit = md_lit(base + 52) & 7;
      md_psg_bruit(bruit, (uint8_t)n, vol);
    } else {
      md_psg_note_on(c - 6, (uint8_t)n, vol);
    }
  }
}

// ── Avancer d'une ligne ────────────────────────────────────────────────────
static void avance(int c) {
  voie_t *v = &voies[c];
  // Une ligne finie n'a plus rien en attente : un report ou une coupure qui
  // n'ont pas trouvé leur tick ne doivent pas déborder sur la ligne suivante.
  v->retard = 0; v->coupe = 0;

  // ── N : on quitte la phrase tout de suite ─────────────────────────────
  if (rupture_demandee) {
    v->chain_ligne++;
    if (!cale_phrase(c)) { v->actif = 0; return; }
    v->phrase_ligne = (uint8_t)(rupture_ligne & (MD_LIGNES_PHRASE - 1));
    return;
  }

  // ── H : on saute au lieu d'avancer ────────────────────────────────────
  // Mêmes chiffres que dans une table : combien de fois, puis vers quelle
  // ligne. Le compteur est indexé par la ligne qui porte le H, donc deux
  // boucles imbriquées comptent chacune ses tours.
  { md_ligne_phrase r;
    md_phrase_lit(v->phrase, v->phrase_ligne, &r);
    if (r.cmd != MD_VIDE && md_cmd_effet(r.cmd) == MD_E_HOP) {
      const int lig = v->phrase_ligne & (MD_LIGNES_PHRASE - 1);
      const int fois = (r.val >> 4) & 15, but = r.val & 15;
      if (fois == 0) { v->phrase_ligne = (uint8_t)but; return; }
      if (v->hop_ph[lig] < fois) {
        v->hop_ph[lig]++; v->phrase_ligne = (uint8_t)but; return;
      }
      v->hop_ph[lig] = 0;   // boucle finie : on passe à la suite
    }
  }

  v->phrase_ligne++;
  if (v->phrase_ligne < MD_LIGNES_PHRASE) return;

  // En portée PHRASE on reboucle sur place : c'est tout l'intérêt, entendre la
  // même phrase encore et encore pour comparer un réglage.
  if (portee == MD_PORTEE_PHRASE) { v->phrase_ligne = 0; return; }

  v->chain_ligne++;
  if (!cale_phrase(c)) v->actif = 0;
}

// Appelée une fois par IMAGE, mais le morceau ne se compte pas en images.
//
// `tempo` est un nombre de TICKS PAR SECONDE et `vitesse` un nombre de ticks
// par ligne : une ligne dure vitesse/tempo seconde. Compter en images ferait
// dépendre la musique de la région de la console — LA DIFFE (tempo 60,
// vitesse 6) doit tenir 10 lignes par seconde, alors qu'une ligne tous les six
// balayages n'en donne que 8,33 sur une console 50 Hz. Dix-sept pour cent trop
// lent, et le même morceau plus rapide sur une machine 60 Hz.
//
// On accumule donc `tempo` à chaque image et on retire `images_par_s` à chaque
// tick produit : la cadence est exacte en moyenne, et la fraction restante est
// reportée au lieu d'être perdue. Il peut y avoir PLUSIEURS ticks par image
// quand le tempo dépasse la cadence vidéo, d'où la boucle.
// ── Un pas de macro ───────────────────────────────────────────────────────
// Renvoie la valeur du pas courant et avance. Une macro sans point de
// bouclage TIENT sa dernière valeur au lieu de repartir : c'est ce que fait
// DefleMask, et une macro de volume qui reboucle sans le vouloir remonte le
// son au milieu de la note.
static int macro_lit(uint32_t base, int off_len, int off_bcl, int off_mac,
                     uint8_t *pos) {
  const uint8_t n = md_lit(base + off_len);
  if (!n) return -1;
  uint8_t p = *pos;
  if (p >= n) p = n - 1;
  const int val = md_lit(base + off_mac + p);
  const uint8_t bcl = md_lit(base + off_bcl);
  if (p + 1 < n)            *pos = (uint8_t)(p + 1);
  else if (bcl != MD_VIDE && bcl < n) *pos = bcl;
  else                      *pos = p;      // on tient la dernière valeur
  return val;
}

// La vitesse d'un point, traduite en pas de niveau par tick. Zéro = tenir.
static int env_allure(uint8_t vitesse) {
  if (!vitesse) return 0;
  const int r = (15 * 256) / ((int)vitesse * (int)vitesse);
  return r < 1 ? 1 : r;
}

// ── Un pas d'enveloppe PSG ────────────────────────────────────────────────
static void env_pas(int c) {
  voie_t *v = &voies[c];
  if (c < 6 || !v->note || !v->env_vif) return;
  const uint8_t ins = v->instr ? v->instr : 1;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;

  // ⚠️ AMPLITUDE ET VITESSE SONT ENTRELACEES : (53,54), (55,56), (57,58).
  //
  // C'est la SERIALISATION du .mdm qui fait foi, pas la structure en memoire.
  // md_write_instrument ecrit env_amp[i] PUIS env_speed[i] a chaque tour ;
  // dans la structure, en revanche, les deux tableaux sont separes (53-55 et
  // 56-58). J'ai un jour « corrige » d'apres offsetof sur la structure, et
  // casse toutes les enveloppes : sur TUTU, l'instrument 06 affichait
  // F4 8F -- au lieu de F8 -- --. Le fichier n'est pas un vidage de la
  // structure — il est ecrit champ par champ.
  int pt = v->env_pt; if (pt > 2) pt = 2;
  const int allure = env_allure(md_lit(base + 54 + (uint32_t)pt * 2));
  const int suiv = pt + 1;
  const uint8_t a_s = (suiv < 3) ? md_lit(base + 53 + (uint32_t)suiv * 2) : MD_VIDE;
  const int a_suiv = (a_s != MD_VIDE);
  const int cible = a_suiv ? ((a_s > 15 ? 15 : a_s) * 256) : 0;

  if (allure > 0) {
    if (v->env_niv < cible) {
      v->env_niv = (int16_t)(v->env_niv + allure);
      if (v->env_niv > cible) v->env_niv = (int16_t)cible;
    } else if (v->env_niv > cible) {
      v->env_niv = (int16_t)(v->env_niv - allure);
      if (v->env_niv < cible) v->env_niv = (int16_t)cible;
    }
    if (v->env_niv == cible && a_suiv) v->env_pt = (uint8_t)suiv;
  }

  // Amplitude nulle sans espoir de remonter : la note s'est éteinte seule.
  // C'est ce qui dispense d'écrire des note-off partout.
  if (v->env_niv <= 0 && !(allure > 0 && cible > 0)) {
    v->env_niv = 0;
    v->env_vif = 0;
    if (c == 9) md_psg_note_off(3); else md_psg_note_off(c - 6);
  }
}

// ── Un pas des trois macros PSG ───────────────────────────────────────────
// ⚠️ ELLES N'ÉCRIVENT PAS DANS LA PUCE. Elles déposent leur avis dans la voie
// et laissent la table trancher juste après ; sinon les deux se battraient
// pour le même registre et on entendrait celui qui écrit en dernier.
static void macro_pas(int c) {
  voie_t *v = &voies[c];
  if (c < 6 || !v->note) return;          // les macros PSG sont pour le PSG
  const uint8_t ins = v->instr ? v->instr : 1;
  const uint32_t base = MD_OFF_INSTR + (uint32_t)(ins - 1) * MD_INSTR_OCTETS;

  const int vol = macro_lit(base, MD_OFF_VOL_LEN, MD_OFF_VOL_BOUCLE,
                            MD_OFF_VOL_MAC, &v->mvol_pas);
  v->mac_vol = (vol < 0) ? MD_VIDE : (uint8_t)(vol > 15 ? 15 : vol);

  const int arp = macro_lit(base, MD_OFF_ARP_LEN, MD_OFF_ARP_BOUCLE,
                            MD_OFF_ARP_MAC, &v->marp_pas);
  // En mode « fixe » la macro donne des notes absolues, pas des écarts.
  if (arp < 0)                          v->mac_tsp = 0;
  else if (md_lit(base + MD_OFF_ARP_FIXE)) v->mac_tsp = (int8_t)(arp - v->note);
  else                                  v->mac_tsp = (int8_t)arp;

  const int nz = macro_lit(base, MD_OFF_NZ_LEN, MD_OFF_NZ_BOUCLE,
                           MD_OFF_NZ_MAC, &v->mnz_pas);
  v->mac_nz = (nz < 0) ? MD_VIDE : (uint8_t)(nz & 7);

  // Une macro EXISTE dès qu'elle a une longueur, même si son pas courant
  // vaut zéro : c'est ce qui fait rejouer la fondamentale d'un arpège.
  v->mac_actif = (vol >= 0 || arp >= 0 || nz >= 0);
}

// ── Un pas de table ───────────────────────────────────────────────────────
// VOL remplace le volume de la voie, TSP transpose la note. Zéro veut dire
// « ne touche à rien » pour les deux — c'est le neutre du format, et c'est ce
// qui permet à une table de ne régler QUE ce qui l'intéresse.
static int  effet_immediat(int c, int effet, uint8_t val);
static void effet_pas(int c, int e, uint8_t val, int t);
static void commande_md(int c, uint8_t rang, uint8_t val);

// ── LES COMMANDES D'UNE TABLE ─────────────────────────────────────────────
// ⚠️ ELLES N'ÉTAIENT PAS LUES DU TOUT. Une table n'appliquait que VOL et TSP :
// poser un P ou un V dans sa colonne CMD ne faisait rien, et rien ne le
// disait. Elles passent maintenant par le MÊME moteur d'effets que les lignes
// de phrase — une seule implémentation par effet, donc le même P des deux
// côtés.
//
// Une table avance d'une ligne par tick : chaque tick apporte donc une
// commande neuve. On l'applique directement, sans l'armer dans un
// emplacement — l'armer la remettrait à zéro à chaque tick, et un vibrato
// n'aurait jamais le temps de tourner.
//
// H et D ne passent pas par là : le premier déplace une position, et
// hop_resout s'en charge ; le second n'a pas de sens sur une table, qui n'a
// pas de ligne à retarder.
static void table_cmds(int c, int t) {
  voie_t *v = &voies[c];
  if (v->table >= MD_MAX_TABLES) return;

  for (int s = 1; s <= 2; s++) {
    const uint32_t b = table_base(v->table, v->table_pas[s]);
    const uint8_t cmd = md_lit(b + (uint32_t)(s == 1 ? 2 : 4));
    const uint8_t val = md_lit(b + (uint32_t)(s == 1 ? 3 : 5));
    if (cmd == MD_VIDE) continue;
    const int e = md_cmd_effet(cmd);
    if (e == MD_E_RIEN || e == MD_E_HOP || e == MD_E_RETARD) continue;
    if (!effet_immediat(c, e, val)) effet_pas(c, e, val, t);
  }

  // La colonne MD voyage avec le second flux, comme sur la DS : c'est un
  // réglage ponctuel, il n'a pas besoin d'un flux à lui.
  { const uint32_t b = table_base(v->table, v->table_pas[2]);
    const uint8_t md = md_lit(b + 6), mv = md_lit(b + 7);
    if (md != MD_VIDE) {
      commande_md(c, md, mv);              // les écritures de registre
      const int e = md_mdcmd_effet(md);    // et les effets de séquence
      if (e != MD_E_RIEN && e != MD_E_RETARD
          && !effet_immediat(c, e, mv)) effet_pas(c, e, mv, t);
    }
  }
}

static void table_pas(int c, int t) {
  voie_t *v = &voies[c];
  if (!v->note) return;
  // ⚠️ On ne sort plus quand il n'y a pas de table : les macros PSG, elles,
  // ont toujours quelque chose à dire, et c'est ici qu'on les applique.
  const int a_table = (v->table < MD_MAX_TABLES);
  const int a_macro = v->mac_actif;
  // ⚠️ Une voie PSG repasse ICI à chaque tick même sans table ni macro :
  // c'est son enveloppe qui la fait décroître, et il faut bien la réécrire.
  const int psg = (c >= 6);
  if (!a_table && !a_macro && !psg) return;
  if (psg && !v->env_vif) return;      // éteinte : on ne la ranime pas

  uint8_t vol = 0; int8_t tsp = 0;
  if (a_table) {
    // Les sauts d'abord : ils disent QUELLE ligne chaque flux va lire.
    for (int s = 0; s < 3; s++) hop_resout(v, s);
    vol =         md_lit(table_base(v->table, v->table_pas[0]) + 0);
    tsp = (int8_t)md_lit(table_base(v->table, v->table_pas[1]) + 1);
  }

  // L'arpège de la macro et la transposition de la table S'AJOUTENT ; le
  // volume de la table, lui, écrase celui de la macro. C'est l'ordre du
  // tracker DS, et il compte : une table d'arpège posée sur un instrument
  // importé doit décaler son arpège, pas le remplacer.
  int n = (int)v->note + tsp + v->mac_tsp;
  if (n < 1) n = 1; else if (n > 108) n = 108;
  // Le niveau vient de l'ENVELOPPE, atténué par la vélocité de la ligne. Une
  // macro de volume la remplace — les deux décriraient la même chose et se
  // contrediraient. La table, elle, écrase les deux pour son tick.
  uint8_t niveau = v->vol;
  if (psg) {
    const int n = ((int)v->env_niv >> 8) * (int)v->vol / 15;
    niveau = (uint8_t)(n < 0 ? 0 : (n > 15 ? 15 : n));
  }
  if (v->mac_vol != MD_VIDE) niveau = v->mac_vol;
  if (vol) niveau = (uint8_t)((vol > 15) ? 15 : vol);

  if (c >= 6) {
    // Le PSG se réaccorde et se re-atténue sans relancer l'enveloppe : c'est
    // ce qu'on veut d'un arpège.
    if (c == 9) {
      // Le grain du bruit vient de la macro quand elle en a un, sinon du
      // réglage fixe de l'instrument.
      uint8_t grain = (v->mac_nz != MD_VIDE) ? v->mac_nz
                    : (uint8_t)(md_lit(MD_OFF_INSTR
                        + (uint32_t)((v->instr ? v->instr : 1) - 1)
                          * MD_INSTR_OCTETS + 52) & 7);
      md_psg_bruit(grain, (uint8_t)n, niveau);
    }
    else {
      md_psg_note_on(c - 6, (uint8_t)n, niveau);
      // Même raison que pour la FM : le désaccord doit survivre au réaccord
      // de la table.
      if (v->fin) md_psg_hauteur(c - 6, n, v->fin);
    }
  } else if (c != MD_PCM_VOIE) {
    // ⚠️ On repose la FRÉQUENCE seulement, pas la voix entière : recharger
    // l'instrument relancerait l'attaque à chaque tick, et on n'entendrait
    // qu'un grésillement.
    //
    // ⚠️ ET AVEC LE DÉSACCORD EN COURS. Sans lui, cette écriture-ci EFFAÇAIT
    // à chaque tick ce qu'un vibrato ou un pitch bend venait de poser : la
    // commande agissait, on la voyait passer, et la table la reprenait
    // aussitôt. Un instrument qui a une table rendait donc toutes les
    // commandes de hauteur inaudibles.
    md_fm_hauteur(c, n, v->fin);
  }

  // ⚠️ ON AVANCE, PUIS ON RÉSOUT LES SAUTS TOUT DE SUITE — pas au tick
  // suivant. La position rangée ici est celle que la page TABLE affiche : la
  // laisser se poser sur la ligne du H faisait descendre le repère jusqu'à
  // lui avant de remonter, alors que cette ligne n'est jamais jouée. Ce qu'on
  // entendait était juste, ce qu'on voyait ne l'était pas.
  // Les commandes de la table s'appliquent sur la ligne QU'ON VIENT DE LIRE,
  // avant d'avancer : sinon elles porteraient sur la suivante.
  if (a_table) table_cmds(c, t);

  if (a_table)
    for (int s = 0; s < 3; s++) {
      v->table_pas[s] = (uint8_t)((v->table_pas[s] + 1) & (MD_LIGNES_TABLE - 1));
      hop_resout(v, s);
    }
}

// ── OU EN EST LA LECTURE, POUR LE FIL D'ARIANE ────────────────────────────
// ⚠️ Un gel ne laisse AUCUNE exception 68000 : la machine ne trappe pas, elle
// s'arrete. Le seul moyen de savoir ou, c'est que le tracker ecrive sa
// position a chaque image. Sans ca on en est reduit a demander « c'etait vers
// quelle ligne ? ».
void md_lecture_position(uint8_t *song, uint8_t *chain, uint8_t *phrase,
                         uint8_t *instr, uint8_t *voie) {
  // La voie PCM d'abord : c'est elle qu'on soupconne. Si elle dort, on prend
  // la premiere voie active.
  int c = MD_PCM_VOIE;
  if (!voies[c].actif || !voies[c].note) {
    for (int k = 0; k < MD_CANAUX; k++)
      if (voies[k].actif && voies[k].note) { c = k; break; }
  }
  const voie_t *v = &voies[c];
  if (voie)   *voie   = (uint8_t)c;
  if (song)   *song   = (uint8_t)v->song_ligne;
  if (chain)  *chain  = (uint8_t)v->chain_ligne;
  if (phrase) *phrase = (uint8_t)v->phrase_ligne;
  if (instr)  *instr  = v->instr;
}

void md_lecture_tick(void) {
  if (!en_cours) return;

  uint8_t vitesse = md_song_vitesse();
  if (vitesse < 1) vitesse = 6;
  int bpm = md_song_bpm();
  if (bpm < 20) bpm = 125;

  // Ticks par seconde = BPM x lignes_par_temps x vitesse / 60. On accumule le
  // numérateur à chaque image et on retire le dénominateur à chaque tick : la
  // cadence est exacte en moyenne, et elle ne dépend pas de la région.
  reste += bpm * MD_LIGNES_PAR_TEMPS * vitesse;
  while (reste >= 60 * images_par_s) {
    reste -= 60 * images_par_s;

    if (ticks == 0)
      for (int c = 0; c < MD_CANAUX; c++)
        if (voies[c].actif && !md_lecture_muette(c)) joue(c);
    // L'enveloppe PSG avance avec les macros, un pas par tick.
    for (int c = 0; c < MD_CANAUX; c++)
      if (voies[c].actif && !md_lecture_muette(c)) env_pas(c);
    // Les macros AVANT la table : elles déposent leur avis, la table tranche.
    for (int c = 0; c < MD_CANAUX; c++)
      if (voies[c].actif && !md_lecture_muette(c)) macro_pas(c);
    // La table avance à CHAQUE tick, la phrase seulement toutes les
    // `vitesse` — c'est toute la raison d'être d'une table.
    for (int c = 0; c < MD_CANAUX; c++)
      if (voies[c].actif && !md_lecture_muette(c)) table_pas(c, ticks);
    // Les commandes tournent au même rythme que les tables — un pas par tick.
    for (int c = 0; c < MD_CANAUX; c++)
      if (voies[c].actif && !md_lecture_muette(c)) commande_tick(c, ticks);
    ticks++;
    if (ticks >= vitesse) {
      ticks = 0;
      for (int c = 0; c < MD_CANAUX; c++) if (voies[c].actif) avance(c);
      // La rupture a servi pour toutes les voies : elle ne vaut que ce tour.
      rupture_demandee = 0;
      // ⚠️ LE SAUT EN DERNIER, et il relance la lecture. Un saut de position
      // renvoie à une AUTRE ligne de SONG : chaque voie doit y reprendre son
      // bloc et son chain, ce que md_lecture_demarre sait déjà faire. Le
      // rejouer coûte une réinitialisation des puces, une fois, à un endroit
      // où la musique change de toute façon.
      if (saut_demande) {
        saut_demande = 0;
        md_lecture_demarre(saut_vise);
        return;
      }
    }
  }
}
