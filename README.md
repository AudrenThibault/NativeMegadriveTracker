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

## Les commandes

Trois boutons, et **A joue le rôle du SELECT de LSDJ** : c'est le modificateur
qui fait voyager entre les écrans.

### Se déplacer

| Geste | Ce qu'il fait |
|---|---|
| Croix | déplacer le curseur |
| **A + droite** | descendre d'un écran : SONG → CHAIN → PHRASE → INSTRUMENT → TABLE |
| **A + gauche** | remonter |
| **A + haut** | aller à l'écran PROJECT |
| **A + bas** | en revenir |
| **B + haut / bas** | sauter de seize lignes, dans SONG |
| START | lancer et arrêter la lecture, depuis n'importe quel écran |

### Éditer

| Geste | Ce qu'il fait |
|---|---|
| **C** | poser une valeur dans la case vide |
| **C + droite / gauche** | augmenter, diminuer d'un cran |
| **C + haut / bas** | par grands pas |
| **B + C** | effacer la case (dans les deux ordres) |

### Copier, coller, cloner

| Geste | Ce qu'il fait |
|---|---|
| **A + B** | armer une sélection — ensuite la croix l'étend, le curseur reste sur l'ancre |
| **B** | copier la sélection |
| **A + C** | coller. Sur SONG le collage **insère** : ce qui est dessous descend |
| **A + B puis C** | clonage profond — le chain et ses phrases sont dupliqués dans des emplacements neufs |

### Couper une voie

| Geste | Ce qu'il fait |
|---|---|
| **B tenu, puis A** | couper la voie sous le curseur, dans SONG |
| **B** | la rallumer |

L'ordre compte : A puis B arme une sélection, B puis A coupe la voie. Une voie
coupée continue d'avancer dans le morceau sans sonner — elle se rallume donc
en place, sans décalage. Son nom s'affiche en vidéo inverse dans l'en-tête.

### Enregistrer

`PROJECT → LOAD/SAVE SONG`. On arrive toujours sur **LOAD**, une pression à
droite mène à **SAVE**, une deuxième à **ERASE** — qui demande confirmation.
En SAVE, le curseur se pose sur la ligne du morceau en cours et son nom est
déjà écrit : `C`, `C`, `C` enregistrent une version plus récente.

Dans la fenêtre de nom, la croix se promène sur la grille de lettres, `C` pose
le caractère, `<` efface, `OK` valide, `B` annule.

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

Pour la **ROM nue**, celle qu'on publie — le tracker seul, sans aucun morceau
ni échantillon :

```sh
python3 outils/bibliotheque.py vierge
```

Elle s'ouvre sur un projet vide et enregistre dans les seize emplacements de
la cartouche comme n'importe quelle autre : les sauvegardes vivent dans la
FRAM, pas dans la ROM.

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

La page **ABOUT** du tracker porte l'avis légal. L'article 5(d) de la GPL v3
demande qu'une version modifiée continue de l'afficher.

## Termes additionnels (article 7 de la GPL v3)

L'article 7(b) de la licence permet d'exiger la préservation d'attributions
d'auteur. Ce projet s'en sert, et c'est la seule condition ajoutée :

> **Vous devez conserver, dans le code source et dans les avis légaux affichés
> par le programme (la page ABOUT), la mention de l'auteur « Audren Thibault »
> et l'adresse du dépôt d'origine
> `https://github.com/AudrenThibault/NativeMegadriveTracker`.**

Autrement dit : faites-en ce que vous voulez, modifiez, redistribuez, vendez
même — mais **le nom et le lien restent**, dans les fichiers comme à l'écran.

