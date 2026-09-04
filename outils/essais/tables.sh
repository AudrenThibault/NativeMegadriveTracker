#!/bin/sh
# Fait tourner le VRAI lecteur du tracker sur le Mac, avec des bouchons a la
# place des puces, et deroule une table tick par tick. C'est ce qui prouve
# qu'un H boucle sans demander a quelqu'un d'ecouter une ROM.
#
# ⚠️ -DMD_HORS_CONSOLE est obligatoire : sans lui md_lecture_init lit le
# registre de version en 0xA10001 et le programme saute.
set -e
ICI=$(cd "$(dirname "$0")" && pwd)
R="$ICI/../.."
cc -O0 -w -DMD_HORS_CONSOLE \
   -I"$R/moteur/morceau" -I"$R/moteur/lecture" -I"$R/moteur/puces" -I"$R/source" \
   "$ICI/tables.c" "$R/moteur/lecture/md_lecture.c" "$R/moteur/lecture/md_commandes.c" \
   "$ICI/bouchons.c" -o "$ICI/tables"
"$ICI/tables"
