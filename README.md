# GeneTracker

Un tracker de musique qui tourne **sur la Sega Mega Drive** — pas un éditeur
sur ordinateur qui exporte vers la console, mais le tracker lui-même, manette
en main, sur la machine.

Dix voies : six FM du YM2612, trois de ton et une de bruit du SN76489, et une
voie PCM qui remplace la sixième FM quand on lui donne un échantillon.

L'ergonomie est celle de LSDJ — song, chains, phrases, tables — parce que
c'est celle qu'on a dans les doigts.

## Ce qu'il sait faire

- **Séquencer** sur les dix voies, avec l'enchaînement song → chain → phrase.
- **Éditer les instruments FM** : les quatre opérateurs, leurs onze paramètres,
  l'algorithme, le feedback, le LFO.
- **Jouer des échantillons PCM de n'importe quelle longueur.** Le Z80 les lit
  directement dans la cartouche et enchaîne les banques tout seul — un
  échantillon d'une seconde passe sans hoquet.
- **Macros PSG** — volume, arpège, grain de bruit — et enveloppe à trois points.
- **Enregistrer** dans la mémoire de la cartouche : seize emplacements,
  conservés à l'extinction (FRAM, pas de pile).
- **Échanger** avec le tracker DS frère : les morceaux voyagent dans les deux
  sens, échantillons compris.

## Construire

Un seul prérequis :

```sh
brew install m68k-elf-gcc
```

Puis, pour fabriquer une ROM contenant un morceau :

```sh
python3 outils/bibliotheque.py verser morceaux/TUTU.MDM
```

Cette commande reconstruit la banque d'échantillons, compile la ROM, et la
dépose sur la carte SD de l'EverDrive si elle est montée. `./build.sh` seul
compile sans changer le morceau embarqué — utile pour vérifier que ça compile,
pas pour tester un morceau.

## La ROM porte son plan

Une ROM GeneTracker contient un descripteur repérable par la marque
`GENETRK-PLAN01` : il dit où vivent les morceaux et la banque d'échantillons,
et quelle place leur est réservée. C'est ce qui permet à un autre outil — le
tracker DS, par exemple — d'écrire dans l'image **sans recompiler**.

## Matériel

Développé et vérifié sur une **Mega Drive PAL** avec une **EverDrive MD V3**.
La mise au point s'appuie sur un banc d'essai qui fait tourner la ROM dans un
cœur d'émulation sans écran, relève le son et l'image, et sur un journal que le
tracker écrit lui-même dans sa mémoire de sauvegarde — c'est ainsi que les
défauts ont été trouvés, plutôt qu'en devinant.

## Licence

GNU General Public License version 3 — voir [LICENSE](LICENSE).

Copyright (C) 2026 Audren Thibault

Ce programme est distribué **sans aucune garantie**. Vous êtes libre de le
redistribuer et de le modifier selon les termes de la GPL v3.

La page **HELP** du tracker porte l'avis légal. L'article 5(d) de la GPL v3
demande qu'une version modifiée continue de l'afficher : si vous publiez un
dérivé, gardez cette page et le lien vers ce dépôt.
