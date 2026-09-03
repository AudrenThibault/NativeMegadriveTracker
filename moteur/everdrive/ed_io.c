// Voir ed_io.h : implémentation à nous, écrite d'après le protocole seul.
#include "ed_io.h"

#define ED_BASE        0xA130D0
#define REG_FIFO       (*(volatile uint8_t  *)(ED_BASE + 0x01))
#define REG_FIFO_ETAT  (*(volatile uint16_t *)(ED_BASE + 0x02))
#define REG_HORLOGE    (*(volatile uint16_t *)(ED_BASE + 0x06))

#define FIFO_COMPTE    0x07FF   // combien d'octets attendent d'être lus

#define CMD_ETAT       0x10
#define CMD_DISQUE     0xC0
#define CMD_DIR_LIRE   0xC5
#define CMD_OUVRIR     0xC9
#define CMD_ECRIRE     0xCC
#define CMD_FERMER     0xCE

// Modes d'ouverture, tels que FatFs les attend.
#define MODE_NEUF      0x0A     // créer toujours + écriture
#define MODE_AJOUT     0x32     // ouvrir en fin de fichier + écriture

#define BLOC_MAX       1024     // ce que la cartouche acquitte d'un coup
#define DELAI_COURT    250      // ms — pour savoir si quelqu'un répond
#define DELAI_LONG     5000     // ms — écriture, montage de la carte

// ── Fond de panier ─────────────────────────────────────────────────────────

static void pousse(const void *src, uint16_t n) {
  const uint8_t *p = (const uint8_t *)src;
  while (n--) REG_FIFO = *p++;
}

// Lit `n` octets, en rendant la main si la cartouche se tait trop longtemps.
// Le compte à rebours repart à chaque octet reçu : c'est l'ABSENCE de progrès
// qu'on veut détecter, pas la durée totale — une grosse écriture est lente
// sans être en panne pour autant.
static int tire(void *dst, uint16_t n, uint16_t delai) {
  uint8_t *p = (uint8_t *)dst;
  uint16_t depart = REG_HORLOGE;
  while (n) {
    uint16_t dispo = REG_FIFO_ETAT & FIFO_COMPTE;
    if (!dispo) {
      if ((uint16_t)(REG_HORLOGE - depart) > delai) return 0;
      continue;
    }
    if (dispo > n) dispo = n;
    n -= dispo;
    while (dispo--) *p++ = REG_FIFO;
    depart = REG_HORLOGE;
  }
  return 1;
}

static void trame(uint8_t code) {
  const uint8_t t[4] = {'+', (uint8_t)('+' ^ 0xFF), code, (uint8_t)(code ^ 0xFF)};
  pousse(t, 4);
}

static void chaine(const char *s) {
  uint16_t n = 0;
  while (s[n]) n++;
  const uint8_t l[2] = {(uint8_t)(n >> 8), (uint8_t)n};  // gros-boutiste
  pousse(l, 2);
  pousse(s, n);
}

// Demande l'état et le traduit. Le premier octet vaut 0xA5 quand la réponse
// est bien formée ; le second porte le code d'erreur de la cartouche.
static int etat(uint16_t delai) {
  uint8_t r[2];
  trame(CMD_ETAT);
  if (!tire(r, 2, delai)) return ED_EXPIRE;
  if (r[0] != 0xA5) return ED_ETAT_FAUX;
  return r[1];
}

// Jette ce qui traîne : une commande interrompue laisse des octets derrière
// elle, et ils décaleraient toutes les réponses suivantes.
static void purge(void) {
  const uint16_t depart = REG_HORLOGE;
  for (;;) {
    uint16_t dispo = REG_FIFO_ETAT & FIFO_COMPTE;
    if (!dispo) return;
    while (dispo--) (void)REG_FIFO;
    if ((uint16_t)(REG_HORLOGE - depart) > DELAI_COURT) return;
  }
}

static int ferme(void) {
  trame(CMD_FERMER);
  return etat(DELAI_LONG);
}

// ── Interface publique ─────────────────────────────────────────────────────

int ed_present(void) {
  purge();
  uint8_t r[2];
  trame(CMD_ETAT);
  if (!tire(r, 2, DELAI_COURT)) return 0;
  return r[0] == 0xA5;
}

int ed_disque_pret(void) {
  // Lister la racine est la sonde la moins coûteuse : si ça passe, la carte
  // est déjà montée et on s'épargne un montage qui prend du temps.
  uint8_t sans_option = 0;
  trame(CMD_DIR_LIRE);
  pousse(&sans_option, 1);
  chaine("/");
  if (etat(DELAI_LONG) == ED_OK) return ED_OK;

  trame(CMD_DISQUE);
  return etat(DELAI_LONG);
}

int ed_ecrit_fichier(const char *chemin, const void *donnees,
                     uint32_t taille, int ecrase) {
  purge();

  uint8_t mode = ecrase ? MODE_NEUF : MODE_AJOUT;
  trame(CMD_OUVRIR);
  pousse(&mode, 1);
  chaine(chemin);
  int e = etat(DELAI_LONG);
  if (e != ED_OK) return e;

  if (taille) {
    const uint8_t t[4] = {(uint8_t)(taille >> 24), (uint8_t)(taille >> 16),
                          (uint8_t)(taille >> 8),  (uint8_t)taille};
    trame(CMD_ECRIRE);
    pousse(t, 4);

    const uint8_t *p = (const uint8_t *)donnees;
    uint32_t reste = taille;
    while (reste) {
      const uint16_t bloc = (reste > BLOC_MAX) ? BLOC_MAX : (uint16_t)reste;
      uint8_t acquit;
      // La cartouche donne son feu vert AVANT chaque bloc : lui envoyer sans
      // attendre déborderait la file.
      if (!tire(&acquit, 1, DELAI_LONG)) { ferme(); return ED_EXPIRE; }
      if (acquit) { ferme(); return acquit; }
      pousse(p, bloc);
      p += bloc;
      reste -= bloc;
    }
    e = etat(DELAI_LONG);
  }

  const int f = ferme();
  return (e != ED_OK) ? e : f;   // la première erreur prime sur celle du close
}
