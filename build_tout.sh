#!/bin/sh
# ============================================================================
#  LES DEUX ROMs, TOUJOURS ENSEMBLE.
#
#  Il y en a deux, et elles se ressemblent assez pour qu'on les confonde :
#
#    release/GeneTrackerMD-v0.1.0.bin   le tracker seul, sans morceau — celle
#                                       qu'on publie et qu'on essaie dans ares
#    geneTrackerTUTU.bin                la ROM de travail, avec le morceau
#                                       dedans — celle qu'on met sur la carte
#
#  ⚠️ N'EN REFAIRE QU'UNE EST LE PIEGE. Trois fois de suite un correctif a ete
#  construit dans l'une pendant que l'autre gardait l'ancien code : on a
#  cherche un defaut dans du code deja repare, et on a perdu des heures. Il n'y
#  a rien a retenir, il y a ce script a lancer.
#
#  Usage : ./build_tout.sh [MORCEAU.MDM]      (par defaut morceaux/TUTU.MDM)
# ============================================================================
set -e
ICI=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-morceaux/TUTU.MDM}

if [ ! -f "$ICI/$SRC" ] && [ ! -f "$SRC" ]; then
  echo "morceau introuvable : $SRC" >&2
  echo "les .mdm sont prives, ils ne sont pas dans le depot" >&2
  exit 1
fi

echo "== ROM de publication (sans morceau, avec la banque) =="
python3 "$ICI/outils/bibliotheque.py" vierge "$SRC"

echo
echo "== ROM de travail (avec le morceau) =="
python3 "$ICI/outils/bibliotheque.py" verser "$SRC"

echo
echo "== ce qui vient d'etre fabrique =="
ls -l "$ICI/release/"*.bin "$ICI"/geneTracker*.bin 2>/dev/null | sed 's|'"$ICI"'/||'
