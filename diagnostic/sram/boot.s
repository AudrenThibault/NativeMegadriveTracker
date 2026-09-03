| ============================================================================
|  Amorce 68000 + en-tête de cartouche, pour la ROM de diagnostic SRAM.
|  Assemblé par m68k-elf-as. Rien ici n'est repris d'un tiers : l'en-tête est
|  un format documenté, pas du code.
| ============================================================================
    .section .text.boot, "ax"
    .globl  _debut

| ── Table des vecteurs 68000 ───────────────────────────────────────────────
_vecteurs:
    .long   0x00FFFE00          | pile initiale : haut de la RAM
    .long   _debut              | point d'entrée
    .long   _exc_bus            | 2  erreur de bus
    .long   _exc_adresse        | 3  erreur d'adresse (mot sur adresse impaire)
    .long   _exc_illegal        | 4  instruction illégale
    .rept   59
    .long   _exc_autre
    .endr
| 64 vecteurs en tout, soit 0x100 octets : sur Mega Drive l'en-tête commence
| juste derrière, et la table s'arrête donc là — elle ne va pas jusqu'aux 256
| vecteurs du 68000 nu.

| ── En-tête de cartouche, à 0x100 ──────────────────────────────────────────
    .org    0x100
    .ascii  "SEGA MEGA DRIVE "                                  | 0x100
    .ascii  "(C)AUDREN 2026  "                                  | 0x110
    .ascii  "SRAM DIAGNOSTIC                                 "  | 0x120 (48)
    .ascii  "SRAM DIAGNOSTIC                                 "  | 0x150 (48)
    .ascii  "GM MDSRAM-00  "                                    | 0x180 (14)
    .word   0x0000                                              | 0x18E somme
    .ascii  "J               "                                  | 0x190 (16)
    .long   0x00000000                                          | 0x1A0 début ROM
    .long   0x0007FFFF                                          | 0x1A4 fin ROM
    .long   0x00FF0000                                          | 0x1A8 début RAM
    .long   0x00FFFFFF                                          | 0x1AC fin RAM
| ── Déclaration de la SRAM ────────────────────────────────────────────────
| "RA" + 0xF8 0x20 : sauvegarde présente, octets IMPAIRS (le format standard).
| La plage annoncée couvre 512 Ko : c'est précisément ce qu'on veut mesurer.
    .ascii  "RA"                                                | 0x1B0
    .byte   0xF8, 0x20                                          | 0x1B2
    .long   0x00200001                                          | 0x1B4 début
    .long   0x0027FFFF                                          | 0x1B8 fin
    .ascii  "            "                                      | 0x1BC (12)
    .ascii  "DIAGNOSTIC MEMOIRE DE SAUVEGARDE        "          | 0x1C8 (40)
    .ascii  "JUE             "                                  | 0x1F0 (16)

| ── Entrée ────────────────────────────────────────────────────────────────
    .org    0x200
_debut:
    move.w  #0x2700, %sr                | interruptions masquées
    lea     0x00FFFE00, %sp

| La zone des variables non initialisées n'est PAS remise à zéro toute seule :
| c'est à l'amorce de le faire. Sans ça, un compteur statique démarre sur ce
| que la RAM contenait à l'allumage — et le journal s'écrivait n'importe où.
    lea     __bss_debut, %a0
    lea     __bss_fin, %a1
    moveq   #0, %d0
1:  cmp.l   %a1, %a0
    bcc.s   2f
    move.b  %d0, (%a0)+
    bra.s   1b
2:
    jsr     principal
| ── Les exceptions, NOMMÉES ET SITUÉES ───────────────────────────────────
| Une couleur ne suffisait pas : j'avais donné le même jaune à l'étape « VDP »
| et à l'instruction illégale, si bien qu'on ne pouvait plus les distinguer.
| Et même sans cette maladresse, une couleur ne dit pas OÙ.
|
| Chaque gestionnaire relève donc l'adresse de l'instruction fautive dans la
| pile et la passe au C, qui l'écrit à l'écran. Avec elle, le désassemblage
| donne la ligne exacte.
|
| ⚠️ La forme de la pile DIFFÈRE : le 68000 empile un bloc étendu pour les
| erreurs de bus et d'adresse (état, adresse visée, instruction, SR, PC), et
| seulement SR + PC pour les autres. Le PC n'est donc pas au même endroit.
_exc_bus:
    move.l  10(%sp), -(%sp)         | PC fautif, bloc étendu
    pea     2
    bra.s   _exc_appel
_exc_adresse:
    move.l  10(%sp), -(%sp)
    pea     3
    bra.s   _exc_appel
_exc_illegal:
    move.l  2(%sp), -(%sp)          | PC fautif, bloc court
    pea     4
    bra.s   _exc_appel
_exc_autre:
    move.l  2(%sp), -(%sp)
    pea     99
_exc_appel:
    jsr     exception_montre
_piege:
    bra.s   _piege
