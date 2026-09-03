// ============================================================================
//  memset, memcpy, memcmp — en autonome, il n'y a pas de bibliothèque C.
//
//  Il ne suffit pas de les éviter dans notre code : le compilateur en émet des
//  appels TOUT SEUL pour les copies de structures et les initialisations de
//  tableaux. Sans ces symboles, l'édition de liens échoue sur des fonctions
//  qu'on n'a jamais écrites.
// ============================================================================
#include <stdint.h>
#include <stddef.h>

void *memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  while (n--) *p++ = (uint8_t)v;
  return d;
}

void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
  while (n--) *a++ = *b++;
  return d;
}

int memcmp(const void *x, const void *y, size_t n) {
  const uint8_t *a = (const uint8_t *)x, *b = (const uint8_t *)y;
  while (n--) { if (*a != *b) return (int)*a - (int)*b; a++; b++; }
  return 0;
}
