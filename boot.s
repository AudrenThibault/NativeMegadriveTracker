| ============================================================================
|  Amorce 68000 du tracker. L'en-tête de cartouche est posé APRÈS coup par
|  outils/entete.py, aux décalages exacts : le compter en espaces dans un
|  .ascii avait déjà débordé de quatre octets sans que l'assembleur prévienne.
| ============================================================================
    .section .text.boot, "ax"
    .globl  _debut

    .long   0x00FFFFF8          | pile initiale, tout en haut de la RAM
    .long   _debut
    .long   _exc_bus            | 2
    .long   _exc_adresse        | 3
    .long   _exc_illegal        | 4
    .rept   25
    .long   _exc_autre          | 5-29
    .endr
    .long   _int_vbl            | 30 : autovecteur de niveau 6, le retour vertical
    .rept   33
    .long   _exc_autre          | 31-63
    .endr
| 64 vecteurs, soit 0x100 octets : l'en-tête commence juste derrière.

    .org    0x100
    .space  0x100, 0x20         | en-tête, rempli par outils/entete.py

    .org    0x200
_debut:
    move.w  #0x2700, %sr        | interruptions masquées
    lea     0x00FFFFF8, %sp     | la pile descend depuis le sommet ;
                                | les tampons montent depuis 0xFF0000

| ── Les variables INITIALISÉES ────────────────────────────────────────────
| Elles vivent en RAM mais leur valeur de départ est dans la ROM : il faut la
| recopier. Sans ça, un « static int x = 1 » démarre sur ce que la RAM
| contenait à l'allumage — et la page SONG ne se dessinait jamais, son drapeau
| de rafraîchissement valant n'importe quoi.
    lea     __data_rom, %a0
    lea     __data_debut, %a1
    lea     __data_fin, %a2
3:  cmp.l   %a2, %a1
    bcc.s   4f
    move.b  (%a0)+, (%a1)+
    bra.s   3b
4:

| La zone des variables non initialisées n'est pas remise à zéro toute seule.
| Sans ça, un compteur statique démarre sur ce que la RAM contenait à
| l'allumage — et sur cette machine, c'est du bruit.
    lea     __bss_debut, %a0
    lea     __bss_fin, %a1
    moveq   #0, %d0
1:  cmp.l   %a1, %a0
    bcc.s   2f
    move.b  %d0, (%a0)+
    bra.s   1b
2:
    jsr     principal

| ── Le retour vertical ────────────────────────────────────────────────────
| C'est LUI qui fait avancer la musique, et non la boucle d'affichage.
| Redessiner une page entière prend plusieurs images : tant que le séquenceur
| vivait dans cette boucle, changer de page ralentissait la lecture — on
| l'entendait. Ici la cadence ne dépend plus de ce qu'on dessine.
|
| On sauve d0/d1/a0/a1 : ce sont les registres que l'appelé a le droit de
| détruire, et l'interruption tombe n'importe où dans le programme principal.
_int_vbl:
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    move.w  0x00C00004, %d0     | lire l'état du VDP acquitte l'interruption
    jsr     md_lecture_tick
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    rte

| Débloque les interruptions. Appelé une fois, quand l'écran et le séquenceur
| sont prêts : avant, une interruption tomberait sur du matériel non initialisé.
    .globl  md_irq_autorise
md_irq_autorise:
    move.w  #0x2000, %sr
    rts

| ── Les exceptions, nommées et situées ────────────────────────────────────
| Elles bouclaient en silence dans une première version : une erreur de bus
| ressemblait alors trait pour trait à un programme qui tourne en rond. Chacune
| relève désormais l'adresse fautive dans la pile et la passe au C.
|
| ⚠️ La forme du bloc empilé DIFFÈRE : le 68000 empile un bloc étendu pour les
| erreurs de bus et d'adresse, et seulement SR + PC pour les autres.
_exc_bus:
    move.l  10(%sp), -(%sp)
    pea     2
    bra.s   _exc_appel
_exc_adresse:
    move.l  10(%sp), -(%sp)
    pea     3
    bra.s   _exc_appel
_exc_illegal:
    move.l  2(%sp), -(%sp)
    pea     4
    bra.s   _exc_appel
_exc_autre:
    move.l  2(%sp), -(%sp)
    pea     99
_exc_appel:
    jsr     exception_montre
_piege:
    bra.s   _piege
