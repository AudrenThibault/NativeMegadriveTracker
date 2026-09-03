#!/usr/bin/env python3
"""Lit le journal que GeneTracker ecrit dans la memoire de sauvegarde.

La cartouche ne peut pas ecrire un fichier depuis une ROM lancee comme un jeu
(voir LISEZMOI), mais son OS recopie la sauvegarde vers EDMD/SAVE/<nom> des
qu'on change de jeu. Ce fichier-la, on le lit ici : c'est le seul retour que
la console sache donner.

    lit_journal.py "/Volumes/.../EDMD/SAVE/geneTracker.bin"

Le vidage du banc d'essai (.ppm.sram) marche aussi.
"""
import sys

OFF_JOURNAL = 31728
MAX = 512
OFF_SONDE = OFF_JOURNAL + MAX
OFF_MIETTE = OFF_SONDE + 8
OFF_ANNEAU = OFF_MIETTE + 14
ANNEAU_N = 24

def octets_utiles(d):
    # La cartouche persiste UN OCTET SUR DEUX : la fenetre fait 128 Ko mais
    # seuls les octets IMPAIRS portent quelque chose.
    #
    # On ne se fie PAS a la taille du fichier : le vidage du banc fait 65404
    # octets, celui de la cartouche 65536, et un futur outil en donnera une
    # autre. On regarde le contenu — si un octet sur deux est nul, le fichier
    # est entrelace.
    # ⚠️ On ne compare pas a ZERO : la cartouche rend 00 sur les octets pairs,
    # l'emulateur rend FF. Ce qui les distingue d'une vraie donnee, c'est
    # qu'ils sont TOUS PAREILS.
    ech = d[0:4096:2]
    if len(d) > 40000 and ech and len(set(ech)) == 1:
        return d[1::2]
    return d

def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    o = octets_utiles(open(sys.argv[1], 'rb').read())
    if len(o) < OFF_JOURNAL + 16:
        raise SystemExit("fichier trop court : ce n'est pas une sauvegarde GeneTracker")
    z = o[OFF_JOURNAL:OFF_JOURNAL + MAX]
    if not z.startswith(b'GENETRK-LOG'):
        raise SystemExit("marque GENETRK-LOG absente : la ROM n'a pas tourne, "
                         "ou la sauvegarde vient d'ailleurs")
    # Les deux octets qui suivent la marque portent la POSITION d'ecriture :
    # le journal s'ajoute d'un demarrage a l'autre, il ne s'efface pas (la
    # cartouche ne recopie la sauvegarde qu'au redemarrage, et le seul moyen
    # de redemarrer relance le tracker).
    # La marque fait douze caracteres, puis deux octets de position.
    pos = z[12] | (z[13] << 8)
    t = z[14:]
    fin = t.find(b'\x00')
    if fin >= 0:
        t = t[:fin]
    print(t.decode('ascii', 'replace'))
    print(f"[{pos} octets sur {MAX} — au-dela de {MAX - 260} le journal repart du debut]")
    position(o)
    anneau(o)

def position(o):
    """Ou en etait la lecture juste avant la coupure.

    ⚠️ Un gel ne laisse AUCUNE exception 68000 : la machine ne trappe pas,
    elle s'arrete. Ces six octets, reecrits a chaque image, sont donc la seule
    facon de savoir a quel endroit du morceau elle s'est arretee.
    """
    if o[OFF_MIETTE] != 0x4D:
        return
    voie, song, chain, phrase, instr, pcm = o[OFF_MIETTE + 6:OFF_MIETTE + 12]
    if song == 0 and chain == 0 and phrase == 0 and instr == 0 and voie == 0:
        print("\nposition : rien d'enregistre (la lecture n'etait pas active)")
        return
    NOMS = ["F1","F2","F3","F4","F5","PC","P1","P2","P3","NO"]
    nv = NOMS[voie] if voie < 10 else f"?{voie}"
    print(f"\nposition avant la coupure : voie {nv}  song {song:02X}  "
          f"chain {chain:02X}  phrase {phrase:02X}  instr {instr:02X}")
    # Non nul est NORMAL pendant un long echantillon : c'est ce qui reste a
    # verser, et le flux verse au fur et a mesure. Ce n'est un signe que si
    # l'anneau se vide plus vite qu'il ne se remplit, ce que le compteur de
    # departs de l'anneau montre mieux.
    print(f"  flux PCM : {pcm} tranches de 256 octets restant a verser"
          + ("  (echantillon termine)" if pcm == 0 else "  (en cours, normal)"))

def anneau(o):
    """Les douze premieres notes PCM de la derniere lecture.

    Le bilan est pris JUSTE APRES l'armement : `commences`/`finis` et les
    octets lus decrivent donc l'echantillon PRECEDENT. C'est voulu — c'est
    ainsi qu'on voit si le precedent s'est termine ou s'il a ete coupe.
    """
    n_ecrit = o[OFF_ANNEAU]
    taille = o[OFF_ANNEAU + 1]
    if taille != 20 or n_ecrit == 0:
        print("\nanneau PCM : vide — aucune note PCM relevee")
        return
    print(f"\nanneau PCM : {n_ecrit} note(s) relevee(s)")
    print("  ech note vol  retard  ptr   len  pas  dep/fin  reste  cmd  attendu/lu")
    for k in range(n_ecrit):
        b = o[OFF_ANNEAU + 2 + k * 20: OFF_ANNEAU + 2 + (k + 1) * 20]
        if len(b) < 20:
            break
        ptr = b[4] | (b[5] << 8); ln = b[6] | (b[7] << 8)
        pas = b[8] | (b[9] << 8); reste = b[14] | (b[15] << 8)
        note = ("COUPE" if reste else "fini") if b[11] else "—"
        print(f"  {b[0]:3d} {b[1]:4d} {b[2]:4d} {b[3]:6d}  {ptr:04X} {ln:5d} {pas:4d}"
              f"  {b[10]:3d}/{b[11]:<3d} {reste:6d}  {b[16]:3d}  {b[19]:02X}/{b[18]:02X}"
              f"   precedent : {note}")
    print("\n  « dep/fin » : departs et fins cotes Z80. ⚠️ LE NOMBRE DE DEPARTS DOIT")
    print("  SUIVRE LA COLONNE « cmd », UN POUR UN. S'il avance plus vite, le Z80")
    print("  redemarre tout seul — c'est le signe que quelque chose ecrit dans sa")
    print("  memoire. Vingt departs pour douze notes, c'etait le bloc de commande")
    print("  recouvert par le code du pilote.")
    print("\n  « retard » : ce qui restait a verser AU MOMENT de la note. Non nul est")
    print("  normal au milieu d'un echantillon long.")
    print("  « attendu/lu » : l'octet qui devrait etre au debut de l'echantillon, et")
    print("  le dernier que le Z80 a reellement lu.")

if __name__ == '__main__':
    main()
