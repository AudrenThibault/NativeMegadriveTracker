// ============================================================================
//  Les puces, pour de vrai.
//
//  C'est ici que ce projet se sépare de ses deux voisins. Sur iPad et sur DS,
//  le YM2612 et le SN76489 sont ÉMULÉS — ymfm, emu76489, un mélangeur, un
//  rééchantillonneur. Ici les puces sont dans la console : on leur écrit aux
//  ports et le reste n'existe plus.
//
//     YM2612   0xA04000 adresse / 0xA04001 donnée, banc 0
//              0xA04002 adresse / 0xA04003 donnée, banc 1
//     SN76489  0xC00011, un octet à la fois
//
//  ⚠️ Le 68000 doit tenir le bus du Z80 pour accéder au YM2612. md_ecran_init
//  le prend au démarrage et ne le rend jamais : ce tracker n'a pas de pilote
//  Z80 pour l'instant.
//
//  ⚠️ Le YM2612 est LENT. Après chaque écriture il lève un drapeau d'occupation
//  qu'il faut attendre, sinon l'écriture suivante est perdue — et une voix
//  chargée à moitié sonne faux sans qu'on comprenne pourquoi.
// ============================================================================
#ifndef MD_PUCES_H
#define MD_PUCES_H

#include <stdint.h>

// Remet le Z80 dans l'état qu'exige le YM2612 : bus pris, reset RELÂCHÉ.
// À rappeler avant de jouer — voir md_puces.c pour pourquoi ça ne suffit pas
// de le faire une fois au démarrage.
void md_z80_prepare(void);

// Ce que le Z80 a lu dans la cartouche a son demarrage — trois octets.
void md_z80_essai(uint8_t *lu);

void md_puces_init(void);

// Écriture brute, pour les cas particuliers et la mise au point.
void md_ym_ecrit(int banc, uint8_t reg, uint8_t valeur);
void md_psg_ecrit(uint8_t octet);

// Charge un instrument (forme compacte, 80 octets en SRAM) dans une voie FM.
void md_fm_charge(int voie, uint32_t offset_instr);

// note : 1 = C-0, comme dans le tracker. 0 ou MD_VIDE = rien.
void md_fm_note_on(int voie, uint8_t note);
// La hauteur seule, sans réattaquer : pour les arpèges d'une table.
void md_fm_frequence(int voie, uint8_t note);

// La hauteur FINE, en 256ᵉ de demi-ton : ce qu'il faut au vibrato, au
// portamento et au pitch bend. Voir md_puces.c pour pourquoi on interpole.
void md_fm_hauteur(int voie, int note, int fin);
void md_psg_hauteur(int voie, int note, int fin);

// ── Retouches en cours de jeu ─────────────────────────────────────────────
// Les commandes MD règlent un paramètre de la voix SANS la recharger ni
// réattaquer : c'est tout leur intérêt. Elles écrivent un registre, un seul.
uint8_t md_fm_detune_vu(int voie, int op);
uint8_t md_fm_rs_vu(int voie, int op);
void md_fm_pose_alg_fb(int voie, uint8_t alg, uint8_t fb);
void md_fm_pose_tl(int voie, int op, uint8_t tl);
void md_fm_pose_mul(int voie, int op, uint8_t mul);
void md_fm_pose_ar(int voie, int op, uint8_t ar);
void md_fm_pose_pan(int voie, uint8_t pan, uint8_t ams, uint8_t pms);
void md_fm_pose_lfo(int marche, uint8_t vitesse);
void md_fm_note_off(int voie);

// voie PSG : 0-2 pour les tons, 3 pour le bruit. volume 0-15 (15 = silence
// côté puce, on l'inverse ici pour raisonner comme partout ailleurs).
void md_psg_note_on(int voie, uint8_t note, uint8_t volume);

// La voie de bruit prend un MODE et une NOTE : les modes 3 et 7 suivent la
// hauteur, les autres ont une période fixe — c'est la puce qui le veut.
void md_psg_bruit(uint8_t mode, uint8_t note, uint8_t volume);
void md_psg_note_off(int voie);

// La derniere periode PSG ecrite — pour le banc d'essai d'accord.
uint32_t md_periode_vue(void);

// ── La voie PCM ───────────────────────────────────────────────────────────
// La 6ᵉ voie FM EST le convertisseur : le bit 7 du registre 0x2B la débranche
// de la synthèse pour en faire un DAC. Un instrument PCM posé sur une autre
// colonne n'a donc aucun sens, et le moteur de référence le dit : « la 6ᵉ voie
// ne fait QUE de l'échantillon ».
#define MD_PCM_VOIE 5

void md_pcm_actif(int actif);
void md_pcm_octet(uint8_t v);

// Lance un échantillon. `adresse` est son adresse ABSOLUE dans l'espace du
// 68000 ; il ne doit pas franchir une frontière de 32 Ko — le convertisseur
// s'en charge à la compilation, ce qui garde le pilote Z80 court.
// demi_tons : ecart a la note de base de l'echantillon. Descente sans
// limite, montee plafonnee a +11 demi-tons (voir pas_lecture).
// volume : 0 a 255, ou 127 est l'UNITE et 255 vaut le double, avec
// ecretage. C'est l'echelle de la DS — voir md_puces.c.
void md_pcm_joue(uint32_t adresse, uint32_t longueur, int demi_tons, int volume);
void md_pcm_arrete(void);

// ⚠️ À APPELER À CHAQUE IMAGE : c'est lui qui réapprovisionne l'anneau
// pendant que le Z80 y lit. Sans lui, un échantillon de plus de 4 Ko
// rejoue en boucle son début.

// Ce qu'il reste à verser dans l'anneau, en tranches de 256 octets. Zéro = le
// flux est à jour. Écrit dans le fil d'Ariane à chaque image.
uint8_t md_pcm_etat(void);

// ── Le mouchard PCM ───────────────────────────────────────────────────────
// Ce que le 68000 a DEMANDE, et ce que le Z80 a REELLEMENT fait. Les deux
// cote a cote : c'est la seule facon de savoir, sur la console, si l'ordre
// est parti, s'il est arrive, et ou la lecture s'est arretee. L'emulateur ne
// montre rien de tout ca — il joue juste.
typedef struct {
  uint32_t adresse;     // l'octet de ROM demande
  uint16_t banque;      // ce qu'on a pousse dans le registre de banque
  uint16_t pointeur;    // l'adresse vue par le Z80
  uint16_t longueur;
  uint16_t pas;
  uint8_t  commande;    // relue DANS la memoire du Z80, apres ecriture
  uint8_t  ptr_relu_lo, ptr_relu_hi;
  uint8_t  frac_relu, ent_relu;   // les octets retouches, relus
  uint16_t fin_hl;      // ou le Z80 s'est arrete
  uint16_t reste_de;    // ce qui lui restait a lire — ZERO si tout est passe
  uint8_t  commences, finis;
  // ⚠️ Ce que le Z80 a LU DANS LA ROM, premier et dernier octet. Le 68000
  // sait ce qui devrait s'y trouver : c'est la seule façon de savoir si le
  // Z80 atteint vraiment la cartouche à travers sa fenêtre de banque.
  uint8_t  premier_lu, dernier_lu;
} md_pcm_bilan_t;

void md_pcm_bilan(md_pcm_bilan_t *b);
uint8_t pcm_commande_vue(void);

// ── Le relevé des écritures YM2612 ────────────────────────────────────────
// À comparer avec celui que outils/comparateur produit sur le Mac : c'est le
// seul moyen de voir où la console diverge de ce que md_fm_charge dit envoyer.
void md_ym_trace_arme(void);
int  md_ym_trace_nombre(void);
void md_ym_trace_lit(int i, int *banc, uint8_t *reg, uint8_t *val);

// ── Le bus du Z80 ─────────────────────────────────────────────────────────
// Le Z80 débite le convertisseur, et il ne peut le faire que s'il A le bus.
// Le 68000 le lui EMPRUNTE pour écrire dans le YM2612 et le lui rend aussitôt.
// À encadrer autour d'une RAFALE d'écritures plutôt qu'autour de chacune :
// chaque emprunt fige le Z80, donc le convertisseur.
void md_bus_prend(void);
void md_bus_rend(void);

void md_puces_silence(void);

#endif
