// ============================================================================
//  ROM de rejeu : elle n'écrit RIEN d'autre que la trace relevée.
//
//  ⚠️ Elle n'utilise pas md_puces.c. C'est voulu : on veut donner à l'autre
//  modèle de puce EXACTEMENT la même suite d'écritures qu'à ymfm, sans qu'une
//  couche puisse en ajouter ou en réordonner. Les deux moteurs reçoivent donc
//  le même flux, et ce qu'on entend ne peut venir que d'eux.
// ============================================================================
#include <stdint.h>
#include "trace.h"

#define YM_A0 (*(volatile uint8_t *)0xA04000)
#define YM_D0 (*(volatile uint8_t *)0xA04001)
#define YM_A1 (*(volatile uint8_t *)0xA04002)
#define YM_D1 (*(volatile uint8_t *)0xA04003)

static void attend(void) {
  for (int g = 0; g < 2000; g++) if (!(YM_A0 & 0x80)) return;
}

static void ecrit(int banc, uint8_t reg, uint8_t val) {
  attend();
  if (banc) { YM_A1 = reg; attend(); YM_D1 = val; }
  else      { YM_A0 = reg; attend(); YM_D0 = val; }
}

void exception_montre(uint32_t vecteur, uint32_t pc) { (void)vecteur; (void)pc; for (;;) { } }
void md_lecture_tick(void) { }

void principal(void) {
  // Le Z80 tenu hors reset, bus pris : sans ça le YM2612 reste endormi, sa
  // ligne de reset étant la même. C'est la leçon la plus chère de ce projet.
  *(volatile uint16_t *)0xA11200 = 0x0000;
  for (volatile int d = 0; d < 200; d++) { }
  *(volatile uint16_t *)0xA11100 = 0x0100;
  for (int g = 0; g < 10000; g++)
    if (!(*(volatile uint16_t *)0xA11100 & 0x0100)) break;
  *(volatile uint16_t *)0xA11200 = 0x0100;

  ecrit(0, 0x22, 0x00);   // LFO éteint
  ecrit(0, 0x27, 0x00);   // pas de mode spécial sur la voie 3
  ecrit(0, 0x2B, 0x00);   // convertisseur débranché

  for (unsigned i = 0; i < TRACE_N; i++)
    ecrit(TRACE[i][0], TRACE[i][1], TRACE[i][2]);

  for (;;) { }
}
