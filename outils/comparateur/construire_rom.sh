#!/bin/bash
# Bâtit la ROM de rejeu autour de trace.h, produit par le comparateur.
set -e
ICI="$(cd "$(dirname "$0")" && pwd)"
R="$ICI/../.."
CC=m68k-elf-gcc
# Le chemin du projet contient des espaces : les options qui en portent un
# vivent dans un TABLEAU, sinon le shell les coupe au premier espace.
CF=(-m68000 -Os -std=c11 -ffreestanding -fno-builtin -fomit-frame-pointer -Wall -I"$ICI")
$CC "${CF[@]}" -c "$ICI/rejoue.c" -o "$ICI/rejoue.o"
$CC "${CF[@]}" -c "$R/boot.s" -o "$ICI/boot.o"
$CC "${CF[@]}" -nostdlib -T "$R/rom.ld" "$ICI/boot.o" "$ICI/rejoue.o" \
    "$($CC "${CF[@]}" -print-libgcc-file-name)" -o "$ICI/rejoue.elf"
m68k-elf-objcopy -O binary "$ICI/rejoue.elf" "$ICI/rejoue.bin"
python3 "$R/outils/entete.py" "$ICI/rejoue.bin" "REJEU" "GM REJEU000-00" 131072 200001:20FFFF >/dev/null
echo "  -> rejoue.bin"
