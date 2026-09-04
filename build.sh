#!/bin/sh
# Fabrique une ROM. Seul prérequis : brew install m68k-elf-gcc
#
#   ./build.sh                -> geneTracker.bin
#   ./build.sh geneTrackerTUTU -> geneTrackerTUTU.bin
#
# ⚠️ LE NOM EST UN ARGUMENT parce qu'il y a désormais UNE ROM PAR MORCEAU.
# Une ROM ne porte qu'un morceau et sa banque : la banque ne tient que 32
# échantillons, et deux morceaux qui la partagent finissent par s'amputer
# l'un l'autre.
set -e
cd "$(dirname "$0")"
ROM="${1:-geneTracker}"

# ⚠️ LE NOM DOIT CORRESPONDRE AU MORCEAU EMBARQUÉ, ET C'EST VÉRIFIÉ.
# « verser » plusieurs morceaux laisse source/morceaux_rom.h sur le DERNIER.
# Refaire ensuite « ./build.sh geneTrackerTUTU » à la main écrivait alors le
# mauvais morceau dans un fichier portant le bon nom — et on teste pendant une
# heure une ROM qui n'est pas celle qu'on croit. C'est arrivé.
if [ -n "$1" ] && [ "$1" != "geneTracker" ]; then
  embarque=$(sed -n 's/.*morceaux_rom_nom\[[0-9]*\]\[[0-9]*\] = {"\([^"]*\)".*/\1/p' \
             source/morceaux_rom.h)
  if [ -n "$embarque" ] && [ "$1" != "geneTracker$embarque" ]; then
    echo "ROM demandee : $1" >&2
    echo "morceau embarque : $embarque  (donc geneTracker$embarque)" >&2
    echo "-> refus : passe par outils/bibliotheque.py verser" >&2
    exit 1
  fi
fi
CC=m68k-elf-gcc
CFLAGS="-m68000 -Os -std=c11 -ffreestanding -fno-builtin -fomit-frame-pointer \
        -Wall -Wextra -fno-strict-aliasing \
        -I moteur/ecran -I moteur/puces -I moteur/manette -I moteur/morceau -I moteur/puces -I moteur/lecture -I source"
LIBGCC=$($CC $CFLAGS -print-libgcc-file-name)

# On EFFACE les produits avant de reconstruire. Sans ça, une étape d'édition de
# liens supprimée par erreur passe inaperçue : objcopy reconvertit tranquillement
# l'ancien .elf et on teste pendant des heures une ROM périmée. C'est arrivé.
rm -rf build && mkdir -p build

$CC $CFLAGS -c source/main.c            -o build/main.o
$CC $CFLAGS -c source/banque_pcm.c      -o build/banque_pcm.o
$CC $CFLAGS -c source/morceaux_rom.c    -o build/morceaux_rom.o
$CC $CFLAGS -c source/rom_plan.c        -o build/rom_plan.o
$CC $CFLAGS -c moteur/ecran/md_ecran.c  -o build/md_ecran.o
$CC $CFLAGS -c moteur/manette/md_manette.c -o build/md_manette.o
$CC $CFLAGS -c moteur/morceau/md_song.c -o build/md_song.o
$CC $CFLAGS -c moteur/morceau/md_codec.c -o build/md_codec.o
$CC $CFLAGS -c moteur/morceau/md_mem.c -o build/md_mem.o
$CC $CFLAGS -c moteur/puces/md_puces.c   -o build/md_puces.o
$CC $CFLAGS -c moteur/lecture/md_lecture.c -o build/md_lecture.o
$CC $CFLAGS -c moteur/lecture/md_commandes.c -o build/md_commandes.o
$CC $CFLAGS -c boot.s                   -o build/boot.o
$CC $CFLAGS -nostdlib -T rom.ld build/boot.o build/main.o build/banque_pcm.o build/morceaux_rom.o build/rom_plan.o build/md_ecran.o \
    build/md_manette.o build/md_song.o build/md_codec.o build/md_mem.o build/md_puces.o build/md_lecture.o build/md_commandes.o "$LIBGCC" -o build/geneTracker.elf
m68k-elf-objcopy -O binary build/geneTracker.elf "$ROM.bin"

# 512 Ko : la banque d'echantillons embarquee en pese a elle seule plus de
# 200 Ko, et il n'y a pas d'autre endroit ou la mettre — la cartouche n'ouvre
# pas sa carte SD a une ROM lancee comme un jeu.
python3 outils/entete.py "$ROM.bin" "GENETRACKERMD" "GM GENETRK0-00" 524288 200001:20FFFF

# Si la carte SD de la cartouche est montée, la ROM va DIRECTEMENT dessus :
# sinon on recopie à la main à chaque essai et on finit par tester une version
# périmée. On reconnaît la carte à son dossier EDMD, pas à son nom de volume.
carte=""
for v in /Volumes/*/; do [ -d "$v/EDMD" ] && carte="$v" && break; done
if [ -n "$carte" ]; then
  mkdir -p "$carte/Musique"
  # Le nom a changé : « MD Tracker » est déjà pris par un autre tracker, sans
  # rapport avec ce projet. On efface NOTRE ancien fichier, et lui seul —
  # jamais le dossier ni la sauvegarde de l'autre.
  [ -f "$carte/Musique/mdtracker.bin" ] && rm -f "$carte/Musique/mdtracker.bin" \
      && echo "   (ancien mdtracker.bin retire)"
  cp "$ROM.bin" "$carte/Musique/$ROM.bin"
  sync
  echo "-> copiee sur la carte : $carte/Musique/$ROM.bin"
else
  echo "-> carte SD absente, la ROM reste ici"
fi
