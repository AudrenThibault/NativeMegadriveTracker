// ============================================================================
//  Le séquenceur.
//
//  Chaque canal parcourt SA PROPRE colonne du SONG, indépendamment des autres :
//  c'est ce qui fait qu'une piste peut boucler sur un chain pendant qu'une
//  autre en enchaîne dix. C'est la structure de LSDJ, et celle du .mdm.
//
//  Cadencé sur le retour vertical — 50 Hz sur une console PAL, 60 sur une
//  NTSC. Pas de temporisateur du YM2612 pour l'instant : l'image suffit, et
//  elle ne demande aucune interruption.
//
//  ⚠️ CE QUI N'EST PAS ENCORE LÀ, et qu'il ne faut pas croire absent par
//  accident : les effets (les quarante-huit commandes), les tables
//  d'instrument, les enveloppes PSG, les macros, la voie PCM. Ce séquenceur
//  joue les NOTES et les INSTRUMENTS, rien de plus. Le reste vient ensuite.
// ============================================================================
#ifndef MD_LECTURE_H
#define MD_LECTURE_H

#include <stdint.h>

// ── Portée de la lecture, façon LSDJ ──────────────────────────────────────
// Depuis SONG on joue toute la chanson ; depuis CHAIN, seulement le chain
// pointé ; depuis PHRASE, seulement la phrase pointée. Dans ces deux cas une
// SEULE voie sonne — celle de la colonne d'où l'on est descendu — et la
// lecture BOUCLE sur elle-même au lieu de retomber dans le morceau.
//
// Sans ça on ne peut pas écouter un instrument seul pour le comparer : chaque
// START relance la chanson entière depuis le début.
typedef enum {
  MD_PORTEE_SONG = 0,
  MD_PORTEE_CHAIN = 1,
  MD_PORTEE_PHRASE = 2,
} md_portee_t;

// À appeler AVANT md_lecture_demarre. `id` est le numéro de chain ou de
// phrase selon la portée, ignoré pour MD_PORTEE_SONG.
void md_lecture_pose_portee(int portee, int canal, uint8_t id, int ligne);

void md_lecture_init(void);
void md_lecture_demarre(int ligne_song);
void md_lecture_arrete(void);
int  md_lecture_en_cours(void);

// À appeler une fois par image.
void md_lecture_tick(void);

// ── Couper une voie ───────────────────────────────────────────────────────
// Elle continue d'avancer dans le morceau, elle ne sonne plus : c'est ce qui
// permet de la rallumer en place, sans décalage.
int  md_lecture_muette(int c);
void md_lecture_muet_bascule(int c);

// Où en est la lecture — écrit dans le fil d'Ariane à chaque image, pour que
// le prochain démarrage sache où la machine s'est arrêtée.
void md_lecture_position(uint8_t *song, uint8_t *chain, uint8_t *phrase,
                         uint8_t *instr, uint8_t *voie);

// ── Audition ──────────────────────────────────────────────────────────────
// Poser une note doit s'ENTENDRE, et la note doit se couper quand on relâche.
// Sans ça on ne peut pas comparer deux instruments : il faudrait lancer la
// lecture à chaque essai. Elle emprunte la voie du canal édité — donc une voie
// du bon TYPE, ce qui compte : un instrument FM posé sur une colonne PSG ne
// s'auditionnerait pas correctement sur une voie FM.
void md_lecture_audition(int canal, uint8_t note, uint8_t instr);
void md_lecture_audition_stop(void);

// Un octet de cet instrument vient de changer : le renvoyer AUX VOIX QUI LE
// JOUENT, sans attendre la note suivante.
void md_lecture_instr_maj(uint8_t ins);

// La note a laquelle un echantillon joue a sa vitesse naturelle. Copie
// VIVANTE de la banque ROM : reglable, mais perdue a l'extinction.
uint8_t md_pcm_note(int si);
void    md_pcm_note_pose(int si, uint8_t n);

// ── Pour l'affichage : où en est chaque voie ──────────────────────────────
int md_lecture_ligne_song(int canal);
int md_lecture_phrase(int canal);        // numéro de phrase en cours, -1 si rien
int md_lecture_ligne_phrase(int canal);  // ligne dans cette phrase, -1 si rien
int md_lecture_chain(int canal);
int md_lecture_ligne_chain(int canal);

// La table que déroule la voie, et la ligne que lit chacun de ses trois flux
// (0 = VOL, 1 = TSP et première CMD, 2 = seconde CMD et colonne MD). -1 quand
// il n'y a rien à montrer.
int md_lecture_table(int canal);
int md_lecture_table_pos(int canal, int flux);

#endif
