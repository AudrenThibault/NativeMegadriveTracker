#!/usr/bin/env python3
"""Construit la banque d'echantillons EMBARQUEE DANS LA ROM a partir de WAV.

Sur Mega Drive les echantillons ne peuvent pas venir de la carte : la
cartouche ne l'ouvre pas a une ROM lancee comme un jeu. Ils sont donc compiles
dans la ROM, et c'est ici qu'on les y met.

    wav2banque.py <dossier> <sortie.h> [octets_max]

⚠️ DEUX CONTRAINTES DE LA MACHINE, pas des choix :

1. LE Z80 NE PEUT PAS LIRE LA CARTOUCHE. Mesure sur la console : il emet les
   bonnes adresses, mais les donnees qui reviennent sont fausses douze fois
   sur treize (voir LISEZMOI). L'echantillon est donc recopie dans SA RAM
   avant lecture, et celle-ci ne laisse que 7 680 octets — environ 260 ms.
   Un echantillon plus long est REFUSE, jamais tronque : mieux vaut en avoir
   moins et les entendre entiers. C'est aussi pour ca qu'il n'y a plus
   d'alignement sur 32 Ko : la fenetre de banque ne sert plus.

2. Le convertisseur tourne a une cadence FIXE (~29,3 kHz, voir pilote_pcm.z80).
   On y re-echantillonne tout : un WAV a 44,1 kHz joue tel quel sonnerait une
   quinte trop bas. La note de base vaut alors C-4 — a cette note, le son sort
   exactement comme dans le fichier.
"""
import os, struct, sys, wave

# ⚠️ PLUS DE LIMITE DE LONGUEUR. Le pilote lit desormais un anneau de 4 Ko
# reapprovisionne par le 68000 (voir pilote_pcm.z80) : ni la RAM du Z80 ni son
# compteur de sorties, passe a 24 bits, ne sont plus le plafond. Seule la place
# en ROM compte.
# ⚠️ La banque occupe TOUJOURS cette place dans la ROM, pleine ou non : c'est
# ce qui permet a la DS d'y ecrire sans recompiler. Voir source/rom_plan.c.
BANQUE_CAPACITE = 320 * 1024

MAX_SORTIES = 0xFFFFFF
# ⚠️ 121, la cadence de la banque du tracker DS — PAS la cadence de sortie du
# pilote, qui vaut 148. Les deux ne sont pas le meme nombre et n'ont pas a
# l'etre : le pilote rattrape l'ecart par son pas de lecture. Aligner celle-ci
# sur celle-la desaccorderait tous les .mdm existants.
CADENCE = 3546893.0 / 121.0     # la cadence a laquelle la banque est ecrite
MAX_ECH = 32                    # emplacements dans le format
NOTE_BASE = 49                  # C-4

def lit_wav(chemin):
    """Rend une liste d'echantillons en 8 bits non signes, a CADENCE."""
    with wave.open(chemin, 'rb') as w:
        n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
        brut = w.readframes(n)
    if sw == 1:
        # Le WAV 8 bits est NON SIGNE ; le 16 bits est signe. Melanger les deux
        # sans le voir donne un echantillon a l'envers, tres audible.
        ech = [(x - 128) * 256 for x in brut]
    elif sw == 2:
        ech = list(struct.unpack('<%dh' % (len(brut) // 2), brut[:len(brut) // 2 * 2]))
    else:
        return None
    if ch > 1:
        ech = [sum(ech[i:i + ch]) // ch for i in range(0, len(ech) - ch + 1, ch)]
    # Re-echantillonnage lineaire vers la cadence du convertisseur.
    m = int(len(ech) * CADENCE / sr)
    if m < 2:
        return None
    out = bytearray(m)
    for k in range(m):
        p = k * sr / CADENCE
        i = int(p)
        if i + 1 < len(ech):
            f = p - i
            v = ech[i] * (1.0 - f) + ech[i + 1] * f
        else:
            v = ech[-1]
        out[k] = max(0, min(255, int(v / 256) + 128))
    return bytes(out)

def ecris_banque(entete, source, pris, offsets, banque, notes, boucles, noms):
    """Ecrit l'en-tete ET le fichier de donnees.

    ⚠️ DEUX FICHIERS, ET C'EST INDISPENSABLE. La banque etait un tableau
    `static const` dans l'en-tete : chaque fichier qui l'incluait en recevait
    SA PROPRE COPIE. Avec 47 Ko d'echantillons ca passait inapercu ; a 216 Ko
    la ROM debordait de 53 Ko et l'edition de liens echouait sans dire
    pourquoi. Une seule definition, des declarations partout ailleurs.
    """
    donnees = os.path.splitext(entete)[0] + '.c'

    def table(nom, typ, vals):
        v = list(vals) + [0] * (MAX_ECH - len(vals))
        return f"const {typ} {nom}[{MAX_ECH}] = {{" + ",".join(str(x) for x in v) + "};\n"

    with open(entete, 'w') as g:
        g.write(f"// Genere depuis {source} — ne pas editer.\n")
        g.write("#ifndef BANQUE_PCM_H\n#define BANQUE_PCM_H\n#include <stdint.h>\n\n")
        g.write("// Des DECLARATIONS seulement : les donnees vivent dans\n"
                "// banque_pcm.c, en un seul exemplaire.\n")
        g.write(f"extern const uint32_t pcm_offset[{MAX_ECH}];\n")
        g.write(f"extern const uint32_t pcm_longueur[{MAX_ECH}];\n")
        g.write(f"extern const uint8_t  pcm_note[{MAX_ECH}];\n")
        g.write(f"extern const int32_t  pcm_boucle[{MAX_ECH}];\n")
        g.write(f"extern const char     pcm_nom[{MAX_ECH}][17];\n")
        g.write(f"#define PCM_BANQUE_CAPACITE {BANQUE_CAPACITE}\n")
        g.write("extern const uint32_t pcm_banque_utilise;\n")
        g.write("extern const uint8_t  pcm_banque[PCM_BANQUE_CAPACITE];\n\n#endif\n")

    n16 = list(noms) + [""] * (MAX_ECH - len(noms))
    with open(donnees, 'w') as g:
        g.write(f"// Genere depuis {source} — ne pas editer.\n")
        g.write('#include "banque_pcm.h"\n\n')
        g.write(table("pcm_offset", "uint32_t", offsets))
        g.write(table("pcm_longueur", "uint32_t", [len(d) for _, d in pris]))
        g.write(table("pcm_note", "uint8_t", notes))
        g.write(table("pcm_boucle", "int32_t", boucles))
        g.write("const char pcm_nom[%d][17] = {" % MAX_ECH
                + ",".join('"%s"' % s for s in n16) + "};\n\n")
        # ⚠️ CAPACITE FIXE. La DS ecrit dans l'image de la ROM sans pouvoir la
        # recompiler : la banque doit donc occuper toujours la meme place, quel
        # que soit son contenu. Ce qui depasse la partie utile reste a 128, le
        # milieu d'echelle — du silence, pas du bruit.
        if len(banque) > BANQUE_CAPACITE:
            raise SystemExit(f"banque de {len(banque)} octets : la ROM en "
                             f"reserve {BANQUE_CAPACITE}")
        g.write(f"const uint32_t pcm_banque_utilise = {len(banque)};\n")
        g.write("const uint8_t pcm_banque[PCM_BANQUE_CAPACITE] "
                "__attribute__((aligned(32768))) = {\n")
        for i in range(0, len(banque), 16):
            g.write("  " + ",".join(str(x) for x in banque[i:i + 16]) + ",\n")
        g.write("};\n")

def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    dossier, sortie = sys.argv[1], sys.argv[2]
    budget = int(sys.argv[3]) if len(sys.argv) > 3 else 200000

    noms = sorted(f for f in os.listdir(dossier) if f.lower().endswith('.wav'))
    charges = []
    for f in noms:
        d = lit_wav(os.path.join(dossier, f))
        if d is None:
            print(f"  ignore (format) : {f}")
            continue
        if len(d) > MAX_SORTIES:
            print(f"  ignore ({len(d)} o, le pilote compte {MAX_SORTIES} "
                  f"sorties au plus) : {f}")
            continue
        charges.append((len(d), f, d))

    # Les plus COURTS d'abord : un tracker a besoin de percussions et de
    # coups brefs, et trente-deux emplacements se remplissent vite.
    charges.sort()
    pris, total = [], 0
    for taille, f, d in charges:
        if len(pris) >= MAX_ECH or total + taille > budget:
            break
        pris.append((f, d))
        total += taille

    # Placement : bout a bout, sans trou. Plus rien n'oblige a aligner —
    # l'echantillon est recopie dans la RAM du Z80 avant d'etre joue, il n'est
    # jamais lu en place.
    banque, offsets = bytearray(), []
    for f, d in pris:
        offsets.append(len(banque))
        banque.extend(d)

    ecris_banque(sortie, dossier, pris, offsets, banque,
                 [NOTE_BASE] * len(pris), [-1] * len(pris),
                 [os.path.splitext(f)[0].upper()[:16] for f, _ in pris])

    print(f"{len(pris)} echantillons, {len(banque)} octets -> {sortie}")
    for i, (f, d) in enumerate(pris):
        print(f"  {i:02d} {offsets[i]:6d} +{len(d):5d}  {f}")

if __name__ == '__main__':
    main()
