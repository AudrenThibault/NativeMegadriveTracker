#include "md_commandes.h"

// Les vingt et une lettres, DANS L'ORDRE DU FORMAT (voir md_table_cmds du
// projet DS). Les treize premières sont celles de LSDJ, les huit suivantes
// les effets de DefleMask qui n'ont pas d'équivalent LSDJ.
static const char LETTRES[MD_CMD_NOMBRE] = {
  'A', 'C', 'D', 'H', 'K', 'L', 'M', 'O', 'P', 'R', 'T', 'V', 'Z',
  'B', 'E', 'F', 'S', 'J', 'N', 'W', 'U'
};

// ── CE QUE FAIT CHAQUE COMMANDE, EN TOUTES LETTRES ───────────────────────
// Une lettre seule ne se retient pas : « U » ne dit pas « fine tune ». Ces
// noms viennent de la table md_table_cmds du projet DS, qui associe chaque
// lettre a son effet — ce n'est donc pas une interpretation, c'est la source.
// Ils sont courts a dessein : ils s'affichent centres sous la grille.
static const char *NOMS[MD_CMD_NOMBRE] = {
  "TABLE",            /* A */
  "ARPEGGIO",         /* C */
  "NOTE DELAY",       /* D */
  "HOP",              /* H */
  "NOTE CUT",         /* K */
  "TONE PORTAMENTO",  /* L */
  "GLOBAL VOLUME",    /* M */
  "PANNING",          /* O */
  "PITCH BEND",       /* P */
  "RETRIG NOTE",      /* R */
  "TEMPO",            /* T */
  "VIBRATO",          /* V */
  "TREMOLO",          /* Z */
  "VOLUME SLIDE",     /* B */
  "PORTA + VOL SLIDE",/* E */
  "VIBRATO + VOL SLIDE", /* F */
  "SPEED",            /* S */
  "POSITION JUMP",    /* J */
  "PATTERN BREAK",    /* N */
  "VIBRATO DEPTH",    /* W */
  "FINE TUNE"         /* U */
};

const char *md_cmd_nom(int rang) {
  if (rang < 0 || rang >= MD_CMD_NOMBRE) return "";
  return NOMS[rang];
}

char md_cmd_lettre(int rang) {
  if (rang < 0 || rang >= MD_CMD_NOMBRE) return 0;
  return LETTRES[rang];
}

// Les commandes MD, par leur code DefleMask, dans l'ordre du format.
static const uint8_t CODES[MD_MDCMD_NOMBRE] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
  0x0A, 0x0B, 0x0C, 0x0D, 0x0F,
  0xE4, 0xE5, 0xEC, 0xED,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x19, 0x1A, 0x1B, 0x1C
};

// Ce que chacune fait sur la puce. Les quinze premières sont des effets de
// séquence, pas des écritures de registre : elles rendent MD_P_RIEN ici et
// seront traitées avec les commandes à lettre, quand elles le seront.
static const uint8_t ACTIONS[MD_MDCMD_NOMBRE] = {
  MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_RIEN,
  MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_PAN,  MD_P_RIEN,
  MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_RIEN,
  MD_P_RIEN, MD_P_RIEN, MD_P_RIEN, MD_P_RIEN,
  MD_P_LFO,  MD_P_FB,   MD_P_TL1,  MD_P_TL2,  MD_P_TL3,
  MD_P_TL4,  MD_P_MUL,
  MD_P_ARALL, MD_P_AR1, MD_P_AR2,  MD_P_AR3
};

// Les commandes MD portent le code de DefleMask ; leur nom vient de la meme
// documentation, et pour les six dernieres de ce que le sequenceur en fait
// (voir ACTIONS juste au-dessus).
static const char *NOMS_MD[MD_MDCMD_NOMBRE] = {
  "ARPEGGIO", "PORTA UP", "PORTA DOWN", "TONE PORTA", "VIBRATO",
  "PORTA + VOL", "VIBRATO + VOL", "TREMOLO", "PANNING", "SET SPEED 1",
  "VOLUME SLIDE", "POSITION JUMP", "RETRIG", "PATTERN BREAK", "SET SPEED 2",
  "VIBRATO DEPTH", "FINE TUNE", "NOTE CUT", "NOTE DELAY",
  "LFO", "FEEDBACK", "LEVEL OP1", "LEVEL OP2", "LEVEL OP3",
  "LEVEL OP4", "MULTIPLIER",
  "ATTACK ALL", "ATTACK OP1", "ATTACK OP2", "ATTACK OP3"
};

const char *md_mdcmd_nom(int rang) {
  if (rang < 0 || rang >= MD_MDCMD_NOMBRE) return "";
  return NOMS_MD[rang];
}

uint8_t md_mdcmd_code(int rang) {
  if (rang < 0 || rang >= MD_MDCMD_NOMBRE) return 0xFF;
  return CODES[rang];
}

int md_mdcmd_action(int rang) {
  if (rang < 0 || rang >= MD_MDCMD_NOMBRE) return MD_P_RIEN;
  return ACTIONS[rang];
}


// ── DE LA LETTRE (ou du code) A L'EFFET ───────────────────────────────────
// Meme ordre que LETTRES et CODES : ces tables sont lues par RANG, comme le
// reste du format.
static const uint8_t EFFET_LETTRE[MD_CMD_NOMBRE] = {
  MD_E_TABLE,      /* A */
  MD_E_ARPEGE,     /* C */
  MD_E_RETARD,     /* D */
  MD_E_HOP,        /* H */
  MD_E_COUPE,      /* K */
  MD_E_PORTA_TON,  /* L */
  MD_E_VOL_GLOBAL, /* M */
  MD_E_PAN,        /* O */
  MD_E_PITCH,      /* P */
  MD_E_RETRIG,     /* R */
  MD_E_TEMPO,      /* T */
  MD_E_VIBRATO,    /* V */
  MD_E_TREMOLO,    /* Z */
  MD_E_VOL_SLIDE,  /* B */
  MD_E_PORTA_VOL,  /* E */
  MD_E_VIB_VOL,    /* F */
  MD_E_VITESSE,    /* S */
  MD_E_SAUT,       /* J */
  MD_E_RUPTURE,    /* N */
  MD_E_VIB_PROF,   /* W */
  MD_E_FINE        /* U */
};

// Les quinze premiers codes sont les effets de sequence de DefleMask, plus
// quatre codes etendus. Le reste ecrit un registre et passe par
// md_mdcmd_action : ici ils ne sont RIEN, et c'est voulu.
static const uint8_t EFFET_CODE[MD_MDCMD_NOMBRE] = {
  MD_E_ARPEGE,     /* 00 */
  MD_E_PORTA_HAUT, /* 01 */
  MD_E_PORTA_BAS,  /* 02 */
  MD_E_PORTA_TON,  /* 03 */
  MD_E_VIBRATO,    /* 04 */
  MD_E_PORTA_VOL,  /* 05 */
  MD_E_VIB_VOL,    /* 06 */
  MD_E_TREMOLO,    /* 07 */
  MD_E_PAN,        /* 08 */
  MD_E_VITESSE,    /* 09 */
  MD_E_VOL_SLIDE,  /* 0A */
  MD_E_SAUT,       /* 0B */
  MD_E_RETRIG,     /* 0C */
  MD_E_RUPTURE,    /* 0D */
  MD_E_VITESSE,    /* 0F */
  MD_E_VIB_PROF,   /* E4 */
  MD_E_FINE,       /* E5 */
  MD_E_COUPE,      /* EC */
  MD_E_RETARD,     /* ED */
  MD_E_RIEN, MD_E_RIEN, MD_E_RIEN, MD_E_RIEN, MD_E_RIEN, MD_E_RIEN,
  MD_E_RIEN, MD_E_RIEN, MD_E_RIEN, MD_E_RIEN, MD_E_RIEN
};

int md_cmd_effet(int rang) {
  return (rang < 0 || rang >= MD_CMD_NOMBRE) ? MD_E_RIEN : EFFET_LETTRE[rang];
}
int md_mdcmd_effet(int rang) {
  return (rang < 0 || rang >= MD_MDCMD_NOMBRE) ? MD_E_RIEN : EFFET_CODE[rang];
}
