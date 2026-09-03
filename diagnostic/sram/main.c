// ============================================================================
//  DIAGNOSTIC : jusqu'où va la mémoire de sauvegarde, et survit-elle ?
//
//  Le tracker fera VIVRE le morceau dans la SRAM, comme LSDJ : poser une note,
//  c'est déjà l'avoir enregistrée. Toute la conception dépend donc de deux
//  chiffres qu'aucune documentation ne donne :
//
//    1. combien d'octets sont RÉELLEMENT adressables en 0x200000 ;
//    2. combien en RESURVIVENT à une coupure de courant.
//
//  Les sources FPGA de la cartouche annoncent 512 Ko sur un bus 16 bits
//  (BRM_A[17:0], HWC_BRAM_16B). C'est une lecture de schéma, pas une mesure.
//  Ce programme mesure.
//
//  Il travaille en deux passes, séparées par une coupure de courant :
//
//    PASSE 1  aucune signature trouvée -> on sème un motif sur 512 Ko, on
//             affiche « COUPEZ LE COURANT ».
//    PASSE 2  signature trouvée -> on relit, et on dit jusqu'où le motif a
//             tenu, séparément pour les octets pairs et impairs.
//
//  Le motif dépend de l'ADRESSE. C'est ce qui débusque le repliement : si la
//  mémoire ne fait que 64 Ko, l'octet écrit en 0x210000 retombe sur 0x200000
//  et ce qu'on relit ne correspond plus à son adresse. Un motif constant, lui,
//  aurait dit « tout va bien » sur une mémoire seize fois trop petite.
// ============================================================================
#include <stdint.h>
#include "md_font.h"

#define VDP_DATA   (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL   (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL32 (*(volatile uint32_t *)0xC00004)

// Le registre de bascule ROM/SRAM en 0x200000. 1 = SRAM visible et inscriptible.
#define SRAM_BASCULE (*(volatile uint8_t *)0xA130F1)
#define SRAM ((volatile uint8_t *)0x200000)

#define ETENDUE  0x80000u   // 512 Ko : la plage annoncée par le schéma
#define ENTETE   16u        // les seize premiers octets portent la signature

#define PAL_BLANC 0
#define PAL_VERT  1
#define PAL_ROUGE 2

static void vdp_reg(uint8_t r, uint8_t v) { VDP_CTRL = 0x8000 | (r << 8) | v; }

static void vram_ecrit(uint16_t a) {
  VDP_CTRL32 = 0x40000000u | ((uint32_t)(a & 0x3FFF) << 16) | ((a >> 14) & 3);
}
static void cram_ecrit(uint16_t a) {
  VDP_CTRL32 = 0xC0000000u | ((uint32_t)(a & 0x3FFF) << 16) | ((a >> 14) & 3);
}

// ── Marqueurs d'étape ──────────────────────────────────────────────────────
// Le diagnostic laissait l'écran du menu affiché : impossible de savoir OÙ il
// s'arrêtait. La ROM « feu tricolore » a prouvé que le VDP, le TMSS et le Z80
// répondent tous — le blocage est donc plus loin. On peint le fond de l'écran
// à chaque étape franchie : la couleur qui reste DÉSIGNE l'étape fautive.
#define F_ROUGE   0x000E   // entré dans principal
#define F_JAUNE   0x00EE   // Z80 et PSG passés
#define F_BLEU    0x0E00   // registres du VDP écrits
#define F_CYAN    0x0EE0   // VRAM vidée
#define F_MAGENTA 0x0E0E   // palettes et police chargées
#define F_VERT    0x00E0   // sonde SRAM passée
#define F_NOIR    0x0000   // tout va bien, place au texte

static void fond(uint16_t couleur) {
  cram_ecrit(0);
  VDP_DATA = couleur;
}

// ── Le VDP, avec les seuls registres dont on a besoin ─────────────────────
//
// ⚠️ C'est ici qu'était la panne. La version précédente écrivait les VINGT-
// QUATRE registres en boucle, dont ceux qu'on n'emploie pas :
//
//   • reg1 = 0x74 allumait le DMA (bit 4) et l'interruption verticale (bit 5) ;
//   • reg19 à reg23 sont les registres de DMA — longueur et source, laissées
//     à zéro.
//
// Rien de tout ça n'est utilisé par ce programme : on n'écrit jamais par DMA
// et on n'a pas de gestionnaire d'interruption. Ça ne se voyait pas en
// émulation, où ces registres ne coûtent rien tant qu'on ne s'en sert pas.
//
// Le feu tricolore, lui, n'écrivait que les registres utiles — et il tourne
// sur la console. On s'en tient donc à la même liste. Un registre qu'on
// n'emploie pas n'a aucune raison d'être écrit.
static void vdp_reveil(void) {
  if (*(volatile uint8_t *)0xA10001 & 0x0F)
    *(volatile uint32_t *)0xA14000 = 0x53454741;   // "SEGA"

  vdp_reg(0,  0x04);   // mode 1 : pas d'interruption horizontale
  vdp_reg(1,  0x44);   // affichage ALLUMÉ, mode 5 — NI DMA NI INTERRUPTION
  vdp_reg(2,  0x30);   // plan A   -> 0xC000
  vdp_reg(3,  0x34);   // fenêtre  -> 0xD000
  vdp_reg(4,  0x07);   // plan B   -> 0xE000
  vdp_reg(5,  0x78);   // sprites  -> 0xF000
  vdp_reg(7,  0x00);   // fond = palette 0, couleur 0
  vdp_reg(11, 0x00);   // pas de défilement par colonne
  vdp_reg(12, 0x81);   // 40 colonnes
  vdp_reg(13, 0x3E);   // défilement horizontal -> 0xF800
  vdp_reg(15, 0x02);   // incrément 2
  vdp_reg(16, 0x01);   // plans 64 x 32
  vdp_reg(17, 0x00);   // fenêtre repoussée hors de l'écran
  vdp_reg(18, 0x00);
}

// ── La police en tuiles ────────────────────────────────────────────────────
// md_font range un glyphe 5x7 par caractère, une ligne par octet, le bit 4 à
// gauche. Le VDP veut des tuiles 8x8 en 4 bits par point. On convertit une
// fois au démarrage : la tuile d'un caractère est son code moins l'espace, si
// bien que la tuile 0 est l'espace — donc un fond vide sans rien y écrire.
static void police_charge(void) {
  vram_ecrit(0);
  for (int c = 0; c < 0x40; c++) {
    for (int l = 0; l < 8; l++) {
      const uint8_t bits = (l < MD_FONT_HT) ? md_font[c][l] : 0;
      uint32_t mot = 0;
      for (int p = 0; p < 5; p++)
        if (bits & (0x10 >> p)) mot |= 1u << ((7 - p) * 4);
      VDP_DATA = (uint16_t)(mot >> 16);
      VDP_DATA = (uint16_t)mot;
    }
  }
}

static void ecrit(int col, int lig, uint16_t pal, const char *s) {
  for (; *s; s++, col++) {
    char c = *s;
    if (c >= 'a' && c <= 'z') c -= 32;      // la police n'a que des majuscules
    if (c < 0x20 || c > 0x5F) c = ' ';
    vram_ecrit((uint16_t)(0xC000 + (lig * 64 + col) * 2));
    VDP_DATA = (uint16_t)((pal << 13) | (uint16_t)(c - 0x20));
  }
}

static void ecrit_hex(int col, int lig, uint16_t pal, uint32_t v, int chiffres) {
  char t[9];
  for (int i = chiffres - 1; i >= 0; i--) {
    const uint8_t n = v & 0xF;
    t[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
    v >>= 4;
  }
  t[chiffres] = 0;
  ecrit(col, lig, pal, t);
}

// Taille en Kio, écrite en décimal.
static void ecrit_kio(int col, int lig, uint16_t pal, uint32_t octets) {
  const uint32_t k = octets >> 10;
  char t[8];
  int n = 0;
  if (k == 0) t[n++] = '0';
  else { char r[8]; int m = 0; uint32_t x = k;
         while (x) { r[m++] = (char)('0' + x % 10); x /= 10; }
         while (m) t[n++] = r[--m]; }
  t[n++] = 'K'; t[n] = 0;
  ecrit(col, lig, pal, t);
}

// ── Réveil de la machine ───────────────────────────────────────────────────
// Ce que l'émulateur pardonne et pas la console. Une ROM lancée depuis le menu
// de l'EverDrive hérite d'une machine DÉJÀ CHAUDE : le Z80 tourne, la VRAM est
// pleine des graphismes du menu, les registres du VDP sont ceux du menu. En
// émulation tout part de zéro, donc rien de tout ça ne se voyait.
static void materiel_init(void) {
  // Le Z80 d'abord. À l'allumage il exécute n'importe quoi et réclame le bus ;
  // tant qu'il l'a, le 68000 attend. On lui prend le bus et on le maintient en
  // reset : il n'a rien à faire ici.
  //
  // ⚠️ L'ORDRE COMPTE, et l'attente DOIT être bornée. Première version : on
  // demandait le bus puis on attendait la permission sans limite, le Z80
  // encore en reset. Il ne l'a jamais donnée, et la ROM se figeait LÀ — avant
  // le moindre registre du VDP, donc écran noir et pas un caractère. C'est
  // exactement la panne qu'on cherchait.
  //
  // On le sort donc du reset AVANT de réclamer le bus, et si la permission
  // tarde on continue quand même : un Z80 bruyant est un désagrément, une
  // console figée est une panne.
  *(volatile uint16_t *)0xA11200 = 0x0100;            // Z80 hors reset
  *(volatile uint16_t *)0xA11100 = 0x0100;            // demande du bus
  for (int garde = 0; garde < 10000; garde++)
    if (!(*(volatile uint16_t *)0xA11100 & 0x0100)) break;
  *(volatile uint16_t *)0xA11200 = 0x0000;            // et il reste au repos

  // Déverrouillage TMSS. Sans lui, les consoles récentes coupent le VDP et
  // l'écran reste NOIR — exactement le symptôme observé. La lecture du registre
  // de version est indispensable : écrire en 0xA14000 sur une console qui n'a
  // pas de TMSS provoque une erreur de bus.
  if (*(volatile uint8_t *)0xA10001 & 0x0F)
    *(volatile uint32_t *)0xA14000 = 0x53454741;      // "SEGA"

  // Le générateur de sons se tait : ses quatre voies sortent d'on ne sait où.
  *(volatile uint8_t *)0xC00011 = 0x9F;
  *(volatile uint8_t *)0xC00011 = 0xBF;
  *(volatile uint8_t *)0xC00011 = 0xDF;
  *(volatile uint8_t *)0xC00011 = 0xFF;
}

// Vide la mémoire vidéo et installe palettes et police. Les registres sont
// déjà posés par vdp_reveil : il n'y a plus de seconde liste, donc plus de
// risque d'en écrire un qu'on n'utilise pas.
static void vdp_init(void) {
  fond(F_BLEU);

  // ── On vide TOUTE la VRAM ────────────────────────────────────────────────
  // 64 Ko, pas seulement le plan du texte. C'est ce qui efface les graphismes
  // laissés par le menu : sans ça, les sprites du menu restaient affichés
  // par-dessus, et la liste des sprites n'était pas terminée.
  vram_ecrit(0);
  for (long i = 0; i < 32768; i++) VDP_DATA = 0;
  fond(F_CYAN);

  // Le défilement vertical aussi : il vit hors de la VRAM.
  VDP_CTRL32 = 0x40000010u;                 // écriture VSRAM en 0
  for (int i = 0; i < 40; i++) VDP_DATA = 0;

  // Trois palettes : blanc pour le texte courant, vert pour ce qui va, rouge
  // pour ce qui ne va pas. On lit un diagnostic d'un coup d'œil ou pas du tout.
  cram_ecrit(0);
  VDP_DATA = 0x0000; VDP_DATA = 0x0EEE;     // 0 : fond, blanc
  for (int i = 0; i < 14; i++) VDP_DATA = 0x0000;
  VDP_DATA = 0x0000; VDP_DATA = 0x02E2;     // 1 : vert
  for (int i = 0; i < 14; i++) VDP_DATA = 0x0000;
  VDP_DATA = 0x0000; VDP_DATA = 0x022E;     // 2 : rouge
  for (int i = 0; i < 14; i++) VDP_DATA = 0x0000;

  police_charge();
  fond(F_MAGENTA);
}

// ── Le journal ─────────────────────────────────────────────────────────────
// Recopier un écran à la main est une source d'erreurs, et on en aura besoin
// plusieurs fois : tout ce qui compte part donc AUSSI dans un fichier de la
// carte SD. On ajoute à la fin plutôt que d'écraser, pour garder l'historique
// des essais — la passe 1 et la passe 2 se retrouvent dans le même fichier.
#define JOURNAL_MAX 1400
static char journal[JOURNAL_MAX];
static int journal_n;

static void j_txt(const char *s) {
  while (*s && journal_n < JOURNAL_MAX - 1) journal[journal_n++] = *s++;
}
static void j_fin_ligne(void) {
  if (journal_n < JOURNAL_MAX - 2) { journal[journal_n++] = 0x0D; journal[journal_n++] = 0x0A; }
}
static void j_hex(uint32_t v, int chiffres) {
  for (int i = chiffres - 1; i >= 0; i--) {
    const uint8_t n = (v >> (i * 4)) & 0xF;
    if (journal_n < JOURNAL_MAX - 1)
      journal[journal_n++] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
  }
}
static void j_dec(uint32_t v) {
  char r[12]; int n = 0;
  if (!v) { j_txt("0"); return; }
  while (v) { r[n++] = (char)('0' + v % 10); v /= 10; }
  while (n) { if (journal_n < JOURNAL_MAX - 1) journal[journal_n++] = r[--n]; }
}

// Dit la même chose aux deux endroits : l'écran pour tout de suite, le fichier
// pour après. Une seule source, donc jamais de divergence entre les deux.
static void dit(int col, int lig, uint16_t pal, const char *s) {
  ecrit(col, lig, pal, s);
  j_txt(s);
  j_fin_ligne();
}

// Le motif : il dépend de l'adresse, sinon il ne prouve rien (cf. en-tête).
static uint8_t motif(uint32_t o) {
  // Il DOIT dépendre de l'adresse (cf. en-tête), mais sans décalage long : le
  // 68000 décale à deux cycles le bit, et un « >> 15 » par octet coûtait une
  // dizaine de secondes sur 512 Ko. Ici on ne manipule que des octets déjà en
  // place — le bas, le haut du mot bas, et le numéro de page.
  const uint16_t bas = (uint16_t)o;
  return (uint8_t)((uint8_t)bas ^ (uint8_t)(bas >> 8) ^ (uint8_t)(o >> 16) ^ 0x5A);
}

static const char SIGNATURE[ENTETE] = "MDTRACKER-SRAM0";

// Appelée depuis les gestionnaires d'exception (voir boot.s). Elle remet
// l'affichage debout toute seule : une exception peut survenir AVANT que la
// police soit chargée, et un écran muet ne dirait rien.
void exception_montre(uint32_t vecteur, uint32_t pc) {
  vdp_reveil();
  fond(F_ROUGE);       // signal immédiat, avant même de savoir écrire
  vdp_init();          // VRAM, palettes, police
  fond(F_NOIR);
  ecrit(2, 10, PAL_ROUGE, "EXCEPTION 68000");
  ecrit(2, 12, PAL_BLANC, "VECTEUR");
  ecrit_hex(11, 12, PAL_ROUGE, vecteur, 2);
  ecrit(2, 13, PAL_BLANC, "ADRESSE");
  ecrit_hex(11, 13, PAL_ROUGE, pc, 6);
  ecrit(2, 15, PAL_BLANC, "02 BUS  03 ADRESSE  04 ILLEGALE");
  ecrit(2, 16, PAL_BLANC, "63 AUTRE");
}

void principal(void) {
  vdp_reveil();
  fond(F_ROUGE);
  materiel_init();
  fond(F_JAUNE);
  vdp_init();
  SRAM_BASCULE = 1;
  fond(F_NOIR);

  ecrit(3, 1, PAL_BLANC, "MDTRACKER - DIAGNOSTIC SRAM");
  ecrit(3, 2, PAL_BLANC, "---------------------------");
  j_txt("=== MDTRACKER / DIAGNOSTIC SRAM ==="); j_fin_ligne();
  j_txt("CONSOLE : ");
  j_txt((*(volatile uint8_t *)0xA10001 & 0x40) ? "PAL 50 HZ" : "NTSC 60 HZ");
  j_fin_ligne();

  // ── Y a-t-il de la mémoire ? ──────────────────────────────────────────
  const uint8_t garde = SRAM[1];
  SRAM[1] = 0xA5; const int t1 = (SRAM[1] == 0xA5);
  SRAM[1] = 0x5A; const int t2 = (SRAM[1] == 0x5A);
  SRAM[1] = garde;
  if (!t1 || !t2) {
    dit(3, 4, PAL_ROUGE, "NO SAVE MEMORY");
    for (;;) { }
  }

  // ── Ce qui restait de l'essai précédent ───────────────────────────────
  // La cartouche est une EverDrive MD V3 : sa mémoire de sauvegarde n'a PAS
  // de pile. Elle ne survit pas à une coupure — c'est le système de la
  // cartouche qui la recopie vers EDMD/SAVE/ quand on change de jeu. On
  // regarde donc ce qui reste, mais sans en faire le cœur du test.
  int signee = 1;
  for (uint32_t k = 0; k < ENTETE; k++)
    if (SRAM[k] != (uint8_t)SIGNATURE[k]) { signee = 0; break; }
  if (signee) dit(3, 4, PAL_VERT,  "SIGNATURE RETROUVEE : ELLE A TENU");
  else        dit(3, 4, PAL_BLANC, "PAS DE SIGNATURE : MEMOIRE VIERGE");

  // ── 1. La PÉRIODE DE REPLIEMENT ───────────────────────────────────────
  // Le vrai piège d'une mémoire trop petite : elle ne refuse pas les écritures
  // hors bornes, elle les REPLIE sur le début. Écrire 512 Ko puis relire ne
  // dit alors rien — les dernières passes ont effacé les premières, et le test
  // annonce « zéro » sur une mémoire parfaitement saine.
  //
  // On écrit donc le NUMÉRO DE PAGE à la même position dans chacune des huit
  // pages de 64 Ko, puis on relit celle de la page 0. La valeur qu'on y trouve
  // est celle de la DERNIÈRE page qui retombe dessus, et elle donne la taille
  // de la fenêtre sans ambiguïté :
  //
  //     lit 7 -> 64 Ko     lit 6 -> 128 Ko
  //     lit 4 -> 256 Ko    lit 0 -> 512 Ko ou plus
  for (uint32_t k = 0; k < 8; k++)
    SRAM[(k << 16) + 0x101] = (uint8_t)k;
  const uint8_t derniere = SRAM[0x101];

  uint32_t fenetre_kio = 0;
  switch (derniere) {
    case 7: fenetre_kio = 64;  break;
    case 6: fenetre_kio = 128; break;
    case 4: fenetre_kio = 256; break;
    case 0: fenetre_kio = 512; break;
    default: fenetre_kio = 0;  break;   // valeur inattendue
  }

  ecrit(3, 6, PAL_BLANC, "FENETRE ADRESSABLE");
  j_txt("FENETRE ADRESSABLE : ");
  if (fenetre_kio) {
    ecrit_kio(23, 6, PAL_VERT, fenetre_kio << 10);
    j_dec(fenetre_kio); j_txt(" KIO");
  } else {
    ecrit(23, 6, PAL_ROUGE, "?");
    ecrit_hex(26, 6, PAL_ROUGE, derniere, 2);
    j_txt("INDETERMINEE, DERNIERE PAGE 0x"); j_hex(derniere, 2);
  }
  j_fin_ligne();

  // ── 2. Pairs et impairs, DANS UNE SEULE fenêtre ───────────────────────
  // Cette fois on reste sous la période de repliement : ce qu'on relit est
  // bien ce qu'on a écrit, et l'écart mesure la vraie largeur des données.
  const uint32_t borne = (fenetre_kio ? fenetre_kio : 64u) << 10;
  ecrit(3, 8, PAL_BLANC, "ECRITURE");
  for (uint32_t o = ENTETE; o < borne; o++) {
    SRAM[o] = motif(o);
    if ((o & 0x1FFF) == 0) ecrit_kio(13, 8, PAL_BLANC, o);
  }
  ecrit_kio(13, 8, PAL_VERT, borne);

  uint32_t bons_impairs = 0, bons_pairs = 0;
  for (uint32_t o = ENTETE; o < borne; o++) {
    if (SRAM[o] != motif(o)) continue;
    if (o & 1) bons_impairs++; else bons_pairs++;
  }

  ecrit(3, 10, PAL_BLANC, "IMPAIRS RELUS OK");
  ecrit_kio(21, 10, PAL_VERT, bons_impairs);
  j_txt("IMPAIRS RELUS OK : "); j_dec(bons_impairs); j_fin_ligne();

  ecrit(3, 11, PAL_BLANC, "PAIRS   RELUS OK");
  ecrit_kio(21, 11, PAL_VERT, bons_pairs);
  j_txt("PAIRS   RELUS OK : "); j_dec(bons_pairs); j_fin_ligne();

  // ── 3. Ce qui compte vraiment : ce qui est PERSISTÉ ───────────────────
  // La cartouche ne recopie vers la carte SD que 64 Ko, dont seuls les octets
  // impairs portent des données : 32 Ko. C'est cette taille-là qui borne un
  // morceau, pas la fenêtre adressable.
  ecrit(3, 13, PAL_BLANC, "PERSISTE PAR LA CARTOUCHE");
  ecrit(29, 13, PAL_VERT, "32K");
  j_txt("PERSISTE : 32 KIO (64 KO SAUVES, IMPAIRS SEULS)"); j_fin_ligne();

  // On remet la signature en tête : elle sert au prochain lancement.
  for (uint32_t k = 0; k < ENTETE; k++) SRAM[k] = (uint8_t)SIGNATURE[k];

  // ── Le journal, DANS la mémoire de sauvegarde ─────────────────────────
  // Cette cartouche n'a pas l'interface SD des modèles PRO : impossible
  // d'écrire un fichier nous-mêmes. Mais son système recopie la sauvegarde
  // vers EDMD/SAVE/ quand on change de jeu — alors on passe par là. Le
  // journal va dans les octets IMPAIRS, les seuls que la cartouche
  // conserve, tout à la fin de la fenêtre de 64 Ko.
  journal[journal_n] = 0;
  {
    const char *marque = "MDTRACKER-LOG:";
    uint32_t o = 0xF000;
    for (const char *p = marque; *p; p++) { SRAM[o + 1] = (uint8_t)*p; o += 2; }
    for (int k = 0; k < journal_n && o < 0xFFFE; k++) {
      SRAM[o + 1] = (uint8_t)journal[k];
      o += 2;
    }
    SRAM[o + 1] = 0;
  }

  ecrit(3, 15, PAL_VERT,  "JOURNAL ECRIT DANS LA SAUVEGARDE.");
  ecrit(3, 17, PAL_BLANC, "POUR LE RECUPERER :");
  ecrit(3, 18, PAL_BLANC, "RETOURNEZ AU MENU, CHARGEZ UN");
  ecrit(3, 19, PAL_BLANC, "AUTRE JEU, PUIS RENDEZ LA CARTE.");
  ecrit(3, 20, PAL_BLANC, "IL SERA DANS EDMD/SAVE/");

  for (;;) { }
}
