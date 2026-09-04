#!/bin/sh
# Passe toutes les commandes en revue sur le VRAI lecteur, avec des bouchons a
# la place des puces. Une commande qui n'ecrit rien de plus que la note seule
# est marquee AUCUN EFFET : c'est la liste de ce qui reste a faire.
set -e
ICI=$(cd "$(dirname "$0")" && pwd)
R="$ICI/../.."
cc -O0 -w -DMD_HORS_CONSOLE \
   -I"$R/moteur/morceau" -I"$R/moteur/lecture" -I"$R/moteur/puces" -I"$R/source" \
   "$ICI/commandes.c" "$R/moteur/lecture/md_lecture.c" "$R/moteur/lecture/md_commandes.c" \
   "$ICI/bouchons.c" -o "$ICI/commandes"
"$ICI/commandes"
