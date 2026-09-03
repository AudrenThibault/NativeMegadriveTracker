// Éprouve le codec sur de VRAIS morceaux, sur le Mac, avant qu'il aille sur la
// console. Le même fichier .c est compilé pour les deux : ce qu'on vérifie ici
// est exactement ce qui tournera là-bas.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md_codec.h"
#include "md_song.h"

static uint8_t morceau[MD_TAILLE_TOTALE], rendu[MD_TAILLE_TOTALE], comprime[40000];

int main(int argc, char **argv) {
  int total_ko = 0;
  for (int a = 1; a < argc; a++) {
    FILE *f = fopen(argv[a], "rb");
    if (!f) { printf("  %s : illisible\n", argv[a]); continue; }
    memset(morceau, 0, sizeof morceau);
    if (fread(morceau, 1, MD_TAILLE_TOTALE, f) != MD_TAILLE_TOTALE) {
      printf("  %s : taille inattendue\n", argv[a]); fclose(f); continue;
    }
    fclose(f);

    const uint32_t n = md_codec_comprime(morceau, comprime, sizeof comprime);
    md_codec_decomprime(comprime, n, rendu);

    int diff = 0, premier = -1;
    for (uint32_t i = 0; i < MD_TAILLE_TOTALE; i++)
      if (morceau[i] != rendu[i]) { diff++; if (premier < 0) premier = (int)i; }

    const char *nom = strrchr(argv[a], '/'); nom = nom ? nom + 1 : argv[a];
    printf("  %-16s %6u -> %5u o (%4.1f %%)   aller-retour : ",
           nom, MD_TAILLE_TOTALE, n, 100.0 * n / MD_TAILLE_TOTALE);
    if (diff == 0) printf("IDENTIQUE\n");
    else { printf("%d octets differents, premier a %d\n", diff, premier); total_ko = 1; }
  }
  return total_ko;
}
