#!/bin/bash
# Construit le comparateur. ymfm vient du projet DS — c'est une COPIE de
# lecture, pas une dépendance : rien ici n'est compilé dans la ROM, et le
# projet DS n'est pas touché. Licence BSD, donc pas de souci pour la vente.
set -e
ICI="$(cd "$(dirname "$0")" && pwd)"
RACINE="$ICI/../.."
YMFM="$RACINE/../nintendo DS/moteur/ymfm"
[ -d "$YMFM" ] || { echo "ymfm introuvable : $YMFM" >&2; exit 1; }

clang -std=c11 -O2 -c -DMD_HORS_CONSOLE \
  -I"$RACINE/moteur/puces" -I"$RACINE/moteur/morceau" -I"$RACINE/source" \
  "$RACINE/moteur/puces/md_puces.c" -o "$ICI/md_puces_hote.o"

clang++ -std=c++17 -O2 -I"$YMFM" -I"$RACINE/moteur/puces" \
  "$ICI/comparateur.cpp" "$YMFM/ymfm_opn.cpp" "$YMFM/ymfm_adpcm.cpp" \
  "$YMFM/ymfm_ssg.cpp" "$ICI/md_puces_hote.o" -o "$ICI/comparateur"
echo "  -> outils/comparateur/comparateur"

# ── Le relevé côté DS ──────────────────────────────────────────────────────
# md_replayer.c est du C portable : on le compile avec une fausse couche de
# puce qui note les écritures. Le projet DS n'est pas modifié, seulement lu.
DS="$RACINE/../nintendo DS"
clang -std=c11 -O2 -w \
  -I"$DS/moteur/CustomReplayer" -I"$DS/moteur/MegaDrive" -I"$DS/moteur" \
  "$ICI/trace_ds.c" "$DS/moteur/CustomReplayer/md_replayer.c" \
  -lm -o "$ICI/trace_ds"
echo "  -> outils/comparateur/trace_ds"
