#!/bin/sh
# Fabrique sram.bin, la ROM de diagnostic. Rien d'autre à installer que
# m68k-elf-gcc (brew install m68k-elf-gcc).
set -e
CC=m68k-elf-gcc
CFLAGS="-m68000 -Os -std=c11 -ffreestanding -fno-builtin -fomit-frame-pointer \
        -Wall -Wextra -fno-strict-aliasing"
LIBGCC=$($CC $CFLAGS -print-libgcc-file-name)

# On EFFACE les produits avant de reconstruire. Sans ça, une étape d'édition de
# liens supprimée par erreur passe inaperçue : objcopy reconvertit tranquillement
# l'ancien .elf et on teste pendant des heures une ROM périmée. C'est arrivé.
rm -f sram.elf sram.bin main.o boot.o

$CC $CFLAGS -I ../../moteur/everdrive -c main.c -o main.o
$CC $CFLAGS -c boot.s -o boot.o
$CC $CFLAGS -nostdlib -T rom.ld boot.o main.o "$LIBGCC" -o sram.elf
m68k-elf-objcopy -O binary sram.elf sram.bin

# Une ROM Mega Drive se remplit jusqu'à une taille ronde, puis porte sa somme
# de contrôle en 0x18E : somme des mots de 0x200 à la fin, sur 16 bits.
python3 - <<'PY'
import struct
d = bytearray(open('sram.bin','rb').read())
taille = 1
while taille < max(len(d), 131072): taille *= 2
d += b'\xFF' * (taille - len(d))
s = 0
for i in range(0x200, len(d), 2):
    s = (s + struct.unpack_from('>H', d, i)[0]) & 0xFFFF
struct.pack_into('>H', d, 0x18E, s)
open('sram.bin','wb').write(d)
print(f"sram.bin : {len(d)} octets, somme de controle {s:04X}")
PY

# ── Où atterrit la ROM ──────────────────────────────────────────────────────
# RÈGLE : si la carte SD de l'EverDrive est montée, on construit DIRECTEMENT
# dessus — sinon on recopie à la main à chaque essai, et on finit par tester
# une version périmée sans le savoir. Si elle n'est pas là, la ROM reste sur le
# Mac et le script le dit clairement.
#
# On reconnaît la carte à son dossier EDMD, celui du micrologiciel EverDrive :
# c'est plus sûr que le nom du volume, qui peut être « NO NAME » ou autre.
carte=""
for v in /Volumes/*/; do
  [ -d "$v/EDMD" ] && carte="$v" && break
done

if [ -n "$carte" ]; then
  dest="$carte/Musique"
  mkdir -p "$dest"
  cp sram.bin "$dest/sram.bin"
  # Vidées tout de suite : une carte retirée avec des écritures en attente
  # revient avec un fichier tronqué, et on croit à une panne de la ROM.
  sync
  echo "-> copiee sur la carte : $dest/sram.bin"
else
  echo "-> carte SD absente, la ROM reste ici : $(pwd)/sram.bin"
fi
