// ============================================================================
//  Le morceau, tel qu'il vit dans la SRAM de la cartouche.
//
//  ── Pourquoi ce n'est PAS la mise en page du .mdm ──────────────────────────
//  Le bloc fixe d'un .mdm pèse 176,7 Ko. La cartouche n'en persiste que 32 :
//  un facteur cinq et demi. Garder sa mise en page obligerait à plafonner à
//  seize instruments, alors que le morceau de référence en joue vingt-quatre.
//
//  On sépare donc les deux formes. En SRAM, une forme COMPACTE faite pour
//  l'édition. Sur la carte SD, un .mdm au format complet, exporté depuis
//  celle-ci — donc lisible tel quel par les trackers DS et iPad. Le contrat
//  commun entre les trois projets porte sur le FICHIER, pas sur l'image
//  mémoire : il est préservé.
//
//  Ce qu'on gagne : un enregistrement d'instrument .mdm fait 512 octets, dont
//  295 de macros DefleMask (utiles au seul import) et 62 de bourrage d'anciens
//  champs. Les vrais paramètres tiennent en 59. Ici : 80 octets, nom compris.
//
//  ⚠️ Contrepartie assumée : les macros PSG de DefleMask n'existent pas dans
//  cette forme. Un .mdm importé qui en contient les perdra s'il est édité sur
//  la Mega Drive.
//
//  ── Un octet sur deux, et c'est mesuré ────────────────────────────────────
//  La cartouche (EverDrive MD V3) mappe 128 Ko en 0x200000, mais SEULS LES
//  OCTETS IMPAIRS portent des données. Mesure : sur 65 536 octets pairs
//  écrits puis relus, 256 se relisaient « bons » — soit 65536/256, très
//  exactement le nombre de coïncidences dues au hasard. Ils ne portent rien.
//
//  Tout accès passe donc par md_sram_lit / md_sram_ecrit, qui traduisent un
//  décalage logique en adresse physique impaire. Aucun autre code n'a à
//  connaître cette bizarrerie.
//
//  ── Et il n'y a pas de pile ───────────────────────────────────────────────
//  Rien ne survit à une coupure de courant. C'est le système de la cartouche
//  qui recopie la SRAM vers EDMD/SAVE/ au changement de jeu. L'écran doit le
//  rappeler : RESET puis le menu, jamais le bouton d'alimentation.
// ============================================================================
#ifndef MD_SONG_H
#define MD_SONG_H

#include <stdint.h>

// ── Limites ────────────────────────────────────────────────────────────────
// Choisies contre LA DIFFE, le seul morceau qui serve de repère : 57 lignes,
// 55 chains, 88 phrases, 24 instruments joués. On vise environ le double, et
// il reste 4,5 Ko de marge sur les 32.
#define MD_CANAUX          10
#define MD_SONG_LIGNES     256
#define MD_MAX_CHAINS      96
#define MD_LIGNES_CHAIN    16
#define MD_MAX_PHRASES     160
#define MD_LIGNES_PHRASE   16
#define MD_MAX_INSTR       32
#define MD_MAX_TABLES      16
#define MD_LIGNES_TABLE    16

#define MD_VIDE            0xFF   // case vide, pour tous les champs 8 bits

// ── Découpage de la SRAM, en décalages LOGIQUES ───────────────────────────
#define MD_OFF_ENTETE      0
#define MD_TAILLE_ENTETE   64

#define MD_OFF_SONG        (MD_OFF_ENTETE + MD_TAILLE_ENTETE)
#define MD_TAILLE_SONG     (MD_CANAUX * MD_SONG_LIGNES)

#define MD_OFF_CHAINS      (MD_OFF_SONG + MD_TAILLE_SONG)
#define MD_TAILLE_CHAINS   (MD_MAX_CHAINS * MD_LIGNES_CHAIN * 2)

#define MD_OFF_PHRASES     (MD_OFF_CHAINS + MD_TAILLE_CHAINS)
#define MD_PHRASE_OCTETS   7        // note, instr, vel, cmd, val, mdcmd, mdval
#define MD_TAILLE_PHRASES  (MD_MAX_PHRASES * MD_LIGNES_PHRASE * MD_PHRASE_OCTETS)

#define MD_OFF_INSTR       (MD_OFF_PHRASES + MD_TAILLE_PHRASES)
// ⚠️ 216 ET NON 80 : il fallait la place des MACROS PSG.
//
// Une voix PSG ne tire pas son caractère de son enveloppe à trois points mais
// de ses deux macros — une suite de niveaux et une suite de transpositions,
// déroulées un pas par tick. Sans elles la page PSG n'avait que quatre
// réglages là où la DS en a douze, et un morceau importé perdait tout son
// grain.
//
// Soixante-quatre pas : mesuré sur les morceaux existants, la plus longue
// macro de volume en fait 57 et le plus long arpège 30. Seize — ce que montre
// l'éditeur de la DS — aurait tronqué.
//
//   63      longueur de la macro de volume, 0 = aucune
//   64      son point de bouclage, MD_VIDE = pas de boucle
//   65-128  ses soixante-quatre pas, niveaux 0-15
//   129     longueur de la macro d'arpège
//   130     son point de bouclage
//   131     arpège FIXE : la valeur est une note, pas un écart
//   132-195 ses soixante-quatre pas, écarts signés
//   196-215 libre
#define MD_INSTR_OCTETS    216

// ── LES TROIS MACROS PSG ──────────────────────────────────────────────────
// Volume, arpège, et mode de bruit — celle-ci ne sert qu'à la voie NOISE, où
// elle change le grain pendant la note. Le tracker DS en garde 128 pas pour
// les deux premières et 32 pour la troisième ; ici les trente-deux
// instruments doivent tenir dans les 32 Ko de la cartouche, alors elles sont
// coupées à la mesure de ce que les morceaux emploient réellement :
// la plus longue macro de volume de tous les morceaux fait 57 pas, la plus
// longue macro d'arpège 30. D'où 64 et 32.
//
// ⚠️ Ces trois blocs remplissent EXACTEMENT les 216 octets déjà réservés, nom
// compris. Les agrandir demanderait de reprendre MD_TAILLE_TOTALE, qui n'a
// plus que 192 octets de marge avant les 32 Ko de la sauvegarde.
#define MD_MACRO_VOL_PAS   64
#define MD_MACRO_ARP_PAS   32
#define MD_MACRO_NZ_PAS    32

#define MD_OFF_VOL_LEN     63
#define MD_OFF_VOL_BOUCLE  64
#define MD_OFF_VOL_MAC     65    /* 65..128 */
#define MD_OFF_ARP_LEN     129
#define MD_OFF_ARP_BOUCLE  130
#define MD_OFF_ARP_FIXE    131
#define MD_OFF_ARP_MAC     132   /* 132..163 */
#define MD_OFF_NZ_LEN      164
#define MD_OFF_NZ_BOUCLE   165
#define MD_OFF_NZ_MAC      166   /* 166..197 */
// ⚠️ Le nom vit ici, PAS en 64 : 64 est le point de bouclage de la macro de
// volume. L'y laisser écrivait des lettres au milieu de la macro.
#define MD_OFF_NOM         198   /* 198..213, deux octets de rab derrière */
#define MD_NOM_OCTETS      16
#define MD_TAILLE_INSTR    (MD_MAX_INSTR * MD_INSTR_OCTETS)

#define MD_OFF_TABLES      (MD_OFF_INSTR + MD_TAILLE_INSTR)
#define MD_TABLE_OCTETS    8
#define MD_TAILLE_TABLES   (MD_MAX_TABLES * MD_LIGNES_TABLE * MD_TABLE_OCTETS)

#define MD_TAILLE_TOTALE   (MD_OFF_TABLES + MD_TAILLE_TABLES)

// ── Le journal, dans la place qui reste ───────────────────────────────────
// La cartouche n'a pas d'interface SD accessible à un jeu (mesuré, voir le
// LISEZMOI) : on ne peut pas écrire un fichier. Mais elle recopie la SRAM vers
// EDMD/SAVE/ au changement de jeu — alors le tracker y écrit son compte rendu,
// et on le relit sur le Mac.
//
// Il se loge APRÈS le morceau, dans les 4,5 Ko qui restent. Rien de ce
// qu'écrit md_song_vide ne va jusque-là : le journal survit à un morceau neuf.
#define MD_OFF_JOURNAL   MD_BIB_FIN
// ⚠️ 512 et non 960 : on prend la place pour un ANNEAU PCM dédié. Le journal
// texte se faisait remplir par les relevés d'échantillon, qui le faisaient
// reboucler et emportaient l'en-tête de démarrage avec — on perdait
// justement ce qu'on voulait lire. Deux compte rendus de démarrage tiennent
// encore dans 512 octets, et c'est tout ce qu'on lui demande.
#define MD_JOURNAL_MAX   512

void md_journal_debut(void);
void md_journal_txt(const char *s);
void md_journal_hex(uint32_t v, int chiffres);
void md_journal_dec(uint32_t v);
void md_journal_ligne(void);

// ── Sonde de persistance ──────────────────────────────────────────────────
// LA question qui commande la conception : la sauvegarde survit-elle à une
// coupure de courant ? La spécification de la v2 dit que c'est de la FRAM,
// « qui n'a pas besoin de pile ». Notre premier essai était ambigu — START
// était tenu, ce qui forçait de toute façon une réécriture.
//
// Alors on compte les démarrages. Si le compteur monte d'un lancement à
// l'autre APRÈS avoir coupé le courant, la mémoire est bien non volatile.
#define MD_OFF_SONDE     (MD_OFF_JOURNAL + MD_JOURNAL_MAX)

uint16_t md_sonde_demarrages(void);

// ── Fil d'Ariane ──────────────────────────────────────────────────────────
// La console redemarre ou s'eteint toute seule, par moments. Une coupure
// n'ecrit rien : impossible de savoir apres coup ce que le tracker faisait.
// Alors on l'ecrit AVANT — a chaque image, la page en cours, ce qu'on etait
// en train de faire, et le numero d'image. La memoire est de la FRAM, elle
// survit a la coupure ; au demarrage suivant on relit et on le met au journal.
//
// Si la derniere miette dit toujours la meme chose, la coupure a un endroit
// precis dans notre code. Si elle est chaque fois ailleurs, ce n'est pas nous.
#define MD_OFF_MIETTE  (MD_OFF_SONDE + 8)

// activite : 0 repos  1 dessin  2 audition  3 sequenceur  4 SRAM  5 codec
void md_miette(uint8_t page, uint8_t activite);
void md_miette_position(uint8_t voie, uint8_t song, uint8_t chain,
                        uint8_t phrase, uint8_t instr, uint8_t pcm);
void md_miette_relit(uint8_t *page, uint8_t *activite, uint32_t *images);

// Un plantage attrape, garde pour le demarrage suivant.
void md_miette_exception(uint32_t vecteur, uint32_t pc);
int  md_miette_exception_relit(uint32_t *vecteur, uint32_t *pc);

// ── L'anneau PCM ──────────────────────────────────────────────────────────
// ⚠️ UN ANNEAU, PAS UN JOURNAL. On veut les DERNIERS événements, pas les
// premiers : le défaut arrive après un moment, et un journal qui se remplit
// garde le début et jette la fin. Ici on garde toujours les trente et un
// derniers échantillons armés — de quoi voir une série de gestes.
//
// Seize octets par événement, tous relevés APRÈS l'écriture dans le Z80 :
//   0-1  image      2  échantillon   3  verdict
//   4-5  pointeur   6-7 longueur     8-9 pas
//   10   commencés 11  finis        12-13 pointeur de fin  14-15 reste
//   16   banque    17  1er attendu  18 1er lu   19 dernier lu
//
// ⚠️ On garde l'octet ATTENDU à côté de l'octet LU. Un verdict « faux » ne
// dit pas grand-chose ; deux valeurs côte à côte disent si le Z80 lit à côté,
// lit du vide, ou lit un décalage régulier.
// ⚠️ L'ANNEAU PORTE SA PROPRE TAILLE D'ÉVÉNEMENT, dans son deuxième octet.
// J'ai relu une fois des événements de seize octets en croyant qu'ils en
// faisaient vingt : tout semblait cohérent et tout était faux, et il a fallu
// remarquer qu'un champ contenait le numéro de l'échantillon SUIVANT pour
// s'en apercevoir. Une mesure qui ne dit pas comment on doit la lire n'est
// pas une mesure.
#define MD_OFF_PCM_ANNEAU (MD_OFF_MIETTE + 14)
#define MD_PCM_ANNEAU     24
#define MD_PCM_EVT        20

void md_pcm_anneau_pose(const uint8_t *evt);

// Ce que la cartouche persiste réellement. Le compilateur vérifie qu'on tient
// dedans : mieux vaut une erreur ici qu'un morceau tronqué sur la machine.
#define MD_SRAM_UTILE      32768

// ── Où vivent les octets ──────────────────────────────────────────────────
//
// Le morceau QU'ON ÉDITE vit dans la RAM de la console : 28 Ko sur les 64 qu'a
// la machine, et on n'en utilisait presque rien. Il y est rapide d'accès et,
// surtout, il laisse la mémoire de sauvegarde ENTIÈRE pour la bibliothèque.
//
// La mémoire de sauvegarde (32 Ko, non volatile) ne contient donc plus un seul
// morceau en clair, mais une bibliothèque de morceaux COMPRIMÉS — onze de la
// taille de LA DIFFE, plusieurs dizaines de morceaux courts.
//
// ⚠️ Un morceau en RAM ne survit pas à une coupure. C'est pour ça que le
// morceau de travail est REPORTÉ tout seul dans son emplacement dès qu'on
// cesse de toucher aux boutons : la promesse « poser une note, c'est déjà
// l'avoir enregistrée » est tenue, simplement avec une seconde de retard.

// ── Accès brut à la mémoire de SAUVEGARDE ─────────────────────────────────
// Un décalage logique de 0 tombe sur 0x200001, le 1 sur 0x200003, etc.
#define MD_SRAM_OCTET(o) (*(volatile uint8_t *)(0x200001u + ((uint32_t)(o) << 1)))

static inline uint8_t md_sram_lit(uint32_t o)              { return MD_SRAM_OCTET(o); }
static inline void    md_sram_ecrit(uint32_t o, uint8_t v) { MD_SRAM_OCTET(o) = v; }

// ── Accès au morceau de TRAVAIL, en RAM ───────────────────────────────────
uint8_t *md_travail(void);
uint8_t  md_lit(uint32_t o);
void     md_ecrit(uint32_t o, uint8_t v);

// ── La bibliothèque ───────────────────────────────────────────────────────
#define MD_BIB_EMPLACEMENTS 16
#define MD_BIB_NOM          10

// Disposition de la mémoire de sauvegarde :
//   0      en-tête (8) puis la table des emplacements (16 x 14 = 224)
//   232    les morceaux comprimés, bout à bout
//   31728  le journal (1 Ko), puis la sonde de démarrages
#define MD_BIB_DONNEES  232
#define MD_BIB_FIN      31728

// ⚠️ Le fil d'Ariane est le DERNIER occupant : il doit tenir dans ce que la
// cartouche persiste, sinon il s'écrit dans le vide et on croit qu'il ne se
// passe rien. Le compilateur le vérifie.
typedef char md_verif_miette[(MD_OFF_MIETTE + 14 <= MD_SRAM_UTILE) ? 1 : -1];

void     md_bib_init(void);
int      md_bib_occupe(int emplacement);
uint16_t md_bib_taille(int emplacement);
void     md_bib_nom(int emplacement, char *dest);
void     md_bib_pose_nom(int emplacement, const char *nom);
uint32_t md_bib_libre(void);

// Rendent 1 si l'opération a réussi.
int md_bib_sauve(int emplacement);
int md_bib_charge(int emplacement);
int md_bib_efface(int emplacement);

// ── Cycle de vie ───────────────────────────────────────────────────────────
// Rend 1 si un morceau a été retrouvé dans l'emplacement de travail.
int  md_song_ouvre(void);
void md_song_vide(void);
int  md_song_memoire_presente(void);

// ── SONG : une grille de numéros de chain, un par canal et par ligne ───────
uint8_t md_song_lit(int canal, int ligne);
void    md_song_pose(int canal, int ligne, uint8_t chain);

// ── CHAIN : seize lignes, chacune une phrase et une transposition ─────────
void md_chain_lit(int chain, int ligne, uint8_t *phrase, int8_t *transpose);
void md_chain_pose(int chain, int ligne, uint8_t phrase, int8_t transpose);

// ── PHRASE : seize lignes de note / instrument / vélocité / commandes ─────
typedef struct {
  uint8_t note, instr, vel, cmd, val, mdcmd, mdval;
} md_ligne_phrase;

void md_phrase_lit(int phrase, int ligne, md_ligne_phrase *l);
void md_phrase_pose(int phrase, int ligne, const md_ligne_phrase *l);

// ── Réglages généraux ──────────────────────────────────────────────────────
uint8_t md_song_tempo(void);
void    md_song_pose_tempo(uint8_t t);
uint8_t md_song_vitesse(void);
void    md_song_pose_vitesse(uint8_t v);

// ── Le tempo, tel que le moteur DS/iPad le définit ────────────────────────
// `tempo` est un nombre de TICKS PAR SECONDE, `vitesse` un nombre de ticks par
// ligne. Une ligne dure donc vitesse/tempo seconde — et surtout PAS un nombre
// d'images, ce qui ferait dépendre la musique de la région de la console.
//
// `macro_speedup` et `finetune` viennent du même modèle : le premier multiplie
// la cadence des macros, le second ajuste finement la fréquence de base. Ils ne
// servent pas encore, mais ils sont RANGÉS dès maintenant — les ajouter plus
// tard déplacerait tout l'en-tête et invaliderait les morceaux enregistrés.
// ── Le BPM ────────────────────────────────────────────────────────────────
// C'est la SEULE grandeur qu'on montre à l'utilisateur. `tempo` et `vitesse`
// sont des rouages internes hérités du format : tempo est un nombre de ticks
// par seconde, vitesse un nombre de ticks par ligne. Personne ne veut régler
// ça — on veut un BPM.
//
//     BPM = tempo x 60 / (vitesse x lignes_par_temps)
//
// ⚠️ Le BPM est RANGÉ tel quel (en-tête, offset 14-15) et non recalculé depuis
// le tempo. À vitesse 6, un cran de tempo vaut 2,5 BPM : en passant par lui,
// « +1 » donnait tantôt +2, tantôt +3, jamais +1. Le séquenceur lit donc le
// BPM directement, et le tempo n'est plus qu'un champ d'export.
//
// ⚠️ Ne PAS confondre tempo et BPM. Un morceau neuf posait tempo = 125, ce qui
// donne 312 BPM et non 125 : la lecture partait au triple de la vitesse.
#define MD_LIGNES_PAR_TEMPS 4

uint16_t md_song_bpm(void);
void     md_song_pose_bpm(int bpm);

uint16_t md_song_macro(void);
int16_t  md_song_finetune(void);

// Premier numéro libre, pour le double-appui sur C qui crée un élément neuf.
// Rend MD_VIDE s'il n'y a plus de place.
uint8_t md_chain_libre(void);
uint8_t md_phrase_libre(void);
uint8_t md_table_libre(void);

#endif
