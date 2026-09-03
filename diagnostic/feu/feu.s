| ============================================================================
|  FEU TRICOLORE — la plus petite ROM possible, pour savoir OÙ ça coince.
|
|  Le diagnostic SRAM laisse l'écran du menu affiché : la ROM ne démarre pas,
|  ou se fige avant d'avoir touché le VDP. Impossible de trancher avec un
|  programme qui fait dix choses. Celui-ci n'en fait qu'une : il change la
|  COULEUR DE FOND à chaque étape franchie.
|
|  Assembleur pur, volontairement : ni C, ni éditeur de liens compliqué, ni
|  libgcc, ni remise à zéro de la mémoire. Si ça ne marche pas non plus, le
|  problème n'est pas dans notre code.
|
|  CE QU'ON VERRA :
|    écran du menu inchangé  la ROM ne démarre pas du tout
|    ROUGE fixe              le VDP répond ; figé juste après
|    JAUNE fixe              le Z80 est passé ; figé juste après
|    BLEU/VERT qui clignote  tout est passé, la machine tourne
|
|  Deux versions sont produites, identiques SAUF l'en-tête :
|    feu_sans_sram.bin   aucune sauvegarde déclarée
|    feu_avec_sram.bin   les 256 Ko déclarés par le diagnostic
|  Si la première marche et pas la seconde, c'est notre déclaration de SRAM
|  qui empêche l'EverDrive de lancer la ROM.
| ============================================================================
    .section .text.boot, "ax"
    .globl _debut

    .long   0x00FFFE00
    .long   _debut
    .rept   62
    .long   _piege
    .endr

| L'en-tête est posé APRÈS coup par le script de construction, aux décalages
| exacts. Le compter en espaces dans l'assembleur avait déjà débordé de quatre
| octets sans prévenir : une table de champs en Python ne peut pas se tromper.
    .org    0x100
    .space  0x100, 0x20             | réservé, rempli d'espaces

    .org    0x200
_debut:
    move.w  #0x2700, %sr
    lea     0x00FFFE00, %sp

| ── 1. Déverrouillage TMSS ────────────────────────────────────────────────
| Lire d'abord le registre de version : écrire en 0xA14000 sur une console qui
| n'a pas de TMSS provoque une erreur de bus.
    lea     0x00A10001, %a1
    move.b  (%a1), %d0
    andi.b  #0x0F, %d0
    beq.s   .Lpas_tmss
    lea     0x00A14000, %a1
    move.l  #0x53454741, (%a1)      | "SEGA"
.Lpas_tmss:

| ── 2. Le VDP, au strict minimum ──────────────────────────────────────────
    lea     0x00C00004, %a0         | port de commande
    move.w  #0x8004, (%a0)          | reg0
    move.w  #0x8144, (%a0)          | reg1  affichage ON, mode 5, pas de VINT
    move.w  #0x8230, (%a0)          | reg2  plan A  -> 0xC000
    move.w  #0x8407, (%a0)          | reg4  plan B  -> 0xE000
    move.w  #0x8578, (%a0)          | reg5  sprites -> 0xF000
    move.w  #0x8700, (%a0)          | reg7  fond = palette 0, couleur 0
    move.w  #0x8C81, (%a0)          | reg12 40 colonnes
    move.w  #0x8D3E, (%a0)          | reg13 défilement H -> 0xF800
    move.w  #0x8F02, (%a0)          | reg15 incrément 2
    move.w  #0x9001, (%a0)          | reg16 plans 64 x 32

    lea     0x00C00000, %a2         | port de données

| ── 3. ROUGE : le VDP a répondu ───────────────────────────────────────────
    move.w  #0x000E, %d3
    bsr.s   _fond
    bsr     _pause

| ── 4. Le Z80 : hors reset D'ABORD, puis on réclame le bus, sans jamais
|      attendre indéfiniment.
    lea     0x00A11200, %a1
    move.w  #0x0100, (%a1)
    lea     0x00A11100, %a1
    move.w  #0x0100, (%a1)
    move.w  #2000, %d1
.Lattente:
    btst    #0, (%a1)
    beq.s   .Lbus_ok
    dbra    %d1, .Lattente
.Lbus_ok:

| ── 5. JAUNE : le Z80 est passé ───────────────────────────────────────────
    move.w  #0x00EE, %d3
    bsr.s   _fond
    bsr     _pause

| ── 6. Clignotement : on est arrivé au bout ───────────────────────────────
.Lboucle:
    move.w  #0x0E00, %d3            | bleu
    bsr.s   _fond
    bsr     _pause
    move.w  #0x00E0, %d3            | vert
    bsr.s   _fond
    bsr     _pause
    bra.s   .Lboucle

| Pose la couleur 0 de la palette 0 : c'est elle que le VDP affiche en fond,
| donc elle remplit l'écran quel que soit le contenu des plans.
_fond:
    move.l  #0xC0000000, (%a0)
    move.w  %d3, (%a2)
    rts

_pause:
    move.l  #0x00060000, %d2
.Lp:
    subq.l  #1, %d2
    bne.s   .Lp
    rts

_piege:
    bra.s   _piege
