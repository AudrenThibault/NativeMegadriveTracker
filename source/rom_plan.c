// ============================================================================
//  LE PLAN DE LA ROM, POUR QUI VEUT Y ÉCRIRE
//
//  ⚠️ LA DS NE PEUT PAS RECOMPILER LA ROM. Sur le Mac, verser un morceau
//  régénère des tableaux C et rappelle le compilateur 68000 ; sur la console
//  portable il n'y a pas de compilateur. Pour qu'elle puisse quand même
//  fabriquer une ROM Mega Drive, l'image doit être RAPIÉÇABLE : des zones à
//  emplacement et à taille FIXES, et un descripteur qui dit où elles sont.
//
//  On retrouve ce descripteur en balayant le fichier à la recherche de sa
//  marque. Tout le reste s'en déduit — aucun décalage n'est écrit en dur du
//  côté qui rapièce, donc la disposition peut changer ici sans le casser.
//
//  La ROM est mappée en 0 sur la Mega Drive : ces adresses SONT les positions
//  dans le fichier.
// ============================================================================
#include <stdint.h>
#include "banque_pcm.h"
#include "morceaux_rom.h"

const struct {
  char     marque[16];
  uint32_t version;
  // Les morceaux : le compteur, les trois tables, et le bloc comprimé.
  uint32_t morceaux_n, morceaux_nom, morceaux_taille, morceaux_offset;
  uint32_t morceaux_data, morceaux_capacite, morceaux_max;
  // Les échantillons : les cinq tables, le bloc, sa capacité et sa part utile.
  uint32_t pcm_offset, pcm_longueur, pcm_note, pcm_boucle, pcm_nom;
  uint32_t pcm_banque, pcm_capacite, pcm_utilise, pcm_max;
} rom_plan __attribute__((used)) = {
  "GENETRK-PLAN01", 1,
  (uint32_t)&morceaux_rom_n,     (uint32_t)morceaux_rom_nom,
  (uint32_t)morceaux_rom_taille, (uint32_t)morceaux_rom_offset,
  (uint32_t)morceaux_rom_data,   MORCEAUX_ROM_CAPACITE, MORCEAUX_ROM_MAX,
  (uint32_t)pcm_offset,   (uint32_t)pcm_longueur, (uint32_t)pcm_note,
  (uint32_t)pcm_boucle,   (uint32_t)pcm_nom,
  (uint32_t)pcm_banque,   PCM_BANQUE_CAPACITE,
  (uint32_t)&pcm_banque_utilise, 32
};
