#!/usr/bin/env python3
"""Fait passer les morceaux entre le tracker DS et celui de la Mega Drive.

    bibliotheque.py verser <a.mdm> [b.mdm…]      DS -> Mega Drive
           UNE ROM PAR MORCEAU : geneTrackerTUTU.bin, geneTrackerFABA.bin…
    bibliotheque.py lire   <sauvegarde>          ce que contient la cartouche
    bibliotheque.py sortir <sauvegarde> <dossier> [d.mdm]   Mega Drive -> DS
           (le .mdm facultatif prete sa banque ; sinon c'est celle de la
            derniere ROM construite)

⚠️ VERSER NE TOUCHE PLUS A LA SAUVEGARDE. Il ecrivait les morceaux verses
dans EDMD/SAVE/, ECRASANT les emplacements ou tu ranges ton travail — que la
console n'y lit jamais, mais que « sortir » lit, lui. Les morceaux verses vont
dans la ROM, et rien d'autre ne bouge.

⚠️ POURQUOI LA SAUVEGARDE ET PAS UNE ROM.
La cartouche n'ouvre pas sa carte SD à une ROM lancée comme un jeu — mesuré,
ses registres rendent tous 4A78. Mais son système recopie la mémoire de
sauvegarde vers `EDMD/SAVE/<nom>` au redémarrage, ET la recharge au
lancement. C'est donc par ce fichier-là qu'on entre et qu'on sort, sans jamais
reconstruire la ROM.

⚠️ LES ÉCHANTILLONS VOYAGENT AVEC, ET C'EST TOUT L'ENJEU.
Sur Mega Drive ils sont compilés DANS la ROM ; un morceau ne porte que leur
NUMÉRO. « verser » reconstruit donc la banque à partir des morceaux versés,
RENUMÉROTE leurs instruments — chaque .mdm a sa propre numérotation — et
rebâtit la ROM. Sans ça un morceau jouerait les sons d'un autre.

Le seul contrat entre les deux projets est le FORMAT `.mdm`. Cet outil vit du
côté Mega Drive et ne touche pas au projet DS.
"""
import os, subprocess, sys, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mdm2sram, sram2mdm

# ── La forme compacte, et la bibliothèque (voir moteur/morceau/md_song.h) ──
TAILLE_TOTALE = 32576
MD_VIDE = 0xFF
CANAUX, SONG_LIGNES = 10, 256
MAX_CHAINS, MAX_PHRASES, MAX_INSTR, MAX_TABLES = 96, 160, 32, 16
# ⚠️ 216 : la place des macros PSG (voir moteur/morceau/md_song.h).
INSTR_OCTETS, LIGNES_TABLE, TABLE_OCTETS = 216, 16, 8
OFF_SONG, OFF_CHAINS = 64, 2624
OFF_PHRASES, OFF_INSTR, OFF_TABLES = 5696, 23616, 30528
NEUTRE = (0, 0, MD_VIDE, MD_VIDE, 0, MD_VIDE, 0)

BIB_MAGIE = b'GTLIB1'
BIB_EMPLACEMENTS, BIB_NOM, BIB_DONNEES, BIB_FIN = 16, 10, 232, 31728
SRAM_UTILE = 32768

# ── Le fichier de la cartouche : un octet sur deux ─────────────────────────
def depaquette(d):
    """La fenêtre fait 128 Ko mais seuls les octets IMPAIRS portent quelque
    chose. On ne se fie pas à la taille — 65 536 côté cartouche, 65 404 côté
    banc — mais au contenu : si un octet sur deux est constant, c'est
    entrelacé. Le remplissage vaut 00 sur la cartouche et FF dans
    l'émulateur, d'où le test « tous pareils » et non « tous nuls »."""
    ech = d[0:4096:2]
    if len(d) > 40000 and ech and len(set(ech)) == 1:
        return bytearray(d[1::2])
    return bytearray(d)

def empaquette(u, remplissage=0x00):
    d = bytearray(len(u) * 2)
    for i, x in enumerate(u):
        d[i * 2] = remplissage
        d[i * 2 + 1] = x
    return d

# ── Étage 1 : le codage creux ─────────────────────────────────────────────
def creux(s):
    o = bytearray(s[:32])
    for c in range(CANAUX):
        col = s[OFF_SONG + c * SONG_LIGNES : OFF_SONG + (c + 1) * SONG_LIGNES]
        ln = 0
        for l in range(SONG_LIGNES):
            if col[l] != MD_VIDE:
                ln = l + 1
        o += bytes([ln >> 8, ln & 0xFF]) + col[:ln]

    def bloc(n, taille, masque_ligne, ecrit_ligne):
        corps = bytearray()
        compte = 0
        for i in range(n):
            b = taille(i)
            mk = 0
            for r in range(16):
                if masque_ligne(b, r):
                    mk |= 1 << r
            if not mk:
                continue
            corps += bytes([i, mk & 0xFF, mk >> 8])
            for r in range(16):
                if mk >> r & 1:
                    corps += ecrit_ligne(b, r)
            compte += 1
        return bytes([compte]) + corps

    o += bloc(MAX_CHAINS,
              lambda i: s[OFF_CHAINS + i * 32 : OFF_CHAINS + (i + 1) * 32],
              lambda b, r: b[r * 2] != MD_VIDE,
              lambda b, r: b[r * 2 : r * 2 + 2])

    def phrase_ligne(b, r):
        fm = 0
        vals = bytearray()
        for k in range(7):
            if b[r * 7 + k] != NEUTRE[k]:
                fm |= 1 << k
                vals.append(b[r * 7 + k])
        return bytes([fm]) + vals

    o += bloc(MAX_PHRASES,
              lambda i: s[OFF_PHRASES + i * 112 : OFF_PHRASES + (i + 1) * 112],
              lambda b, r: any(b[r * 7 + k] != NEUTRE[k] for k in range(7)),
              phrase_ligne)

    # Les instruments : l'ÉCART au premier, qui sert de référence.
    dft = s[OFF_INSTR : OFF_INSTR + INSTR_OCTETS]
    corps = bytearray()
    compte = 0
    for i in range(1, MAX_INSTR):
        r = s[OFF_INSTR + i * INSTR_OCTETS : OFF_INSTR + (i + 1) * INSTR_OCTETS]
        if r == dft:
            continue
        pm = bytearray(INSTR_OCTETS // 8)
        vals = bytearray()
        for k in range(INSTR_OCTETS):
            if r[k] != dft[k]:
                pm[k // 8] |= 1 << (k & 7)
                vals.append(r[k])
        corps += bytes([i]) + pm + vals
        compte += 1
    o += bytes([compte]) + dft + corps

    def table_vive(b, r):
        l = b[r * 8 : r * 8 + 8]
        return bool(l[0] or l[1] or l[2] != MD_VIDE
                    or l[4] != MD_VIDE or l[6] != MD_VIDE)

    o += bloc(MAX_TABLES,
              lambda i: s[OFF_TABLES + i * 128 : OFF_TABLES + (i + 1) * 128],
              table_vive,
              lambda b, r: b[r * 8 : r * 8 + 8])
    return bytes(o)

def creux_inverse(e, vide):
    s = bytearray(vide)
    n = 0
    s[0:32] = e[0:32]; n = 32
    for c in range(CANAUX):
        ln = (e[n] << 8) | e[n + 1]; n += 2
        s[OFF_SONG + c * SONG_LIGNES : OFF_SONG + c * SONG_LIGNES + ln] = e[n:n + ln]
        n += ln
    cpt = e[n]; n += 1
    for _ in range(cpt):
        i = e[n]; n += 1
        mk = e[n] | (e[n + 1] << 8); n += 2
        for r in range(16):
            if mk >> r & 1:
                s[OFF_CHAINS + i * 32 + r * 2] = e[n]
                s[OFF_CHAINS + i * 32 + r * 2 + 1] = e[n + 1]
                n += 2
    cpt = e[n]; n += 1
    for _ in range(cpt):
        i = e[n]; n += 1
        mk = e[n] | (e[n + 1] << 8); n += 2
        for r in range(16):
            if not (mk >> r & 1):
                continue
            fm = e[n]; n += 1
            for k in range(7):
                if fm >> k & 1:
                    s[OFF_PHRASES + i * 112 + r * 7 + k] = e[n]; n += 1
    cpt = e[n]; n += 1
    dft = e[n:n + INSTR_OCTETS]; n += INSTR_OCTETS
    for i in range(MAX_INSTR):
        s[OFF_INSTR + i * INSTR_OCTETS : OFF_INSTR + (i + 1) * INSTR_OCTETS] = dft
    for _ in range(cpt):
        i = e[n]; n += 1
        pm = e[n:n + INSTR_OCTETS // 8]; n += INSTR_OCTETS // 8
        for k in range(INSTR_OCTETS):
            if pm[k // 8] >> (k & 7) & 1:
                s[OFF_INSTR + i * INSTR_OCTETS + k] = e[n]; n += 1
    cpt = e[n]; n += 1
    for _ in range(cpt):
        i = e[n]; n += 1
        mk = e[n] | (e[n + 1] << 8); n += 2
        for r in range(16):
            if mk >> r & 1:
                s[OFF_TABLES + i * 128 + r * 8 : OFF_TABLES + i * 128 + r * 8 + 8] = e[n:n + 8]
                n += 8
    return bytes(s)

# ── Étage 2 : LZSS ────────────────────────────────────────────────────────
# Le décodeur vit dans la ROM et ne bouge pas : recul sur douze bits, longueur
# de trois à dix-huit. On peut donc coder plus simplement qu'en C — le seul
# contrat est le FORMAT des jetons, pas la façon de les choisir.
FENETRE, LONG_MAX = 2048, 18

def lzss(d):
    o = bytearray()
    i = 0
    table = {}
    while i < len(d):
        pd = len(o)
        o.append(0)
        drap = 0
        for b in range(8):
            if i >= len(d):
                break
            ml, mp = 0, 0
            if i + 2 < len(d):
                cle = d[i:i + 3]
                for pos in reversed(table.get(bytes(cle), ())):
                    if i - pos > FENETRE:
                        break
                    l = 0
                    while l < LONG_MAX and i + l < len(d) and d[pos + l] == d[i + l]:
                        l += 1
                    if l > ml:
                        ml, mp = l, pos
                        if l == LONG_MAX:
                            break
            if ml >= 3:
                rec = i - mp
                o.append(rec >> 4)
                o.append(((rec & 0xF) << 4) | (ml - 3))
            else:
                drap |= 1 << b
                o.append(d[i])
                ml = 1
            for _ in range(ml):
                if i + 2 < len(d):
                    table.setdefault(bytes(d[i:i + 3]), []).append(i)
                i += 1
        o[pd] = drap
    return bytes(o)

def lzss_inverse(e):
    o = bytearray()
    i = 0
    while i < len(e):
        drap = e[i]; i += 1
        for b in range(8):
            if i >= len(e):
                break
            if drap >> b & 1:
                o.append(e[i]); i += 1
            else:
                rec = (e[i] << 4) | (e[i + 1] >> 4)
                ln = (e[i + 1] & 0xF) + 3
                i += 2
                for _ in range(ln):
                    o.append(o[len(o) - rec])
    return bytes(o)

# ── Le morceau VIDE, pour que creux_inverse ait un fond ───────────────────
def morceau_vide():
    s = bytearray(TAILLE_TOTALE)
    for i in range(CANAUX * SONG_LIGNES):
        s[OFF_SONG + i] = MD_VIDE
    for i in range(MAX_CHAINS):
        for r in range(16):
            s[OFF_CHAINS + i * 32 + r * 2] = MD_VIDE
    for i in range(MAX_PHRASES):
        for r in range(16):
            for k in range(7):
                s[OFF_PHRASES + i * 112 + r * 7 + k] = NEUTRE[k]
    for i in range(MAX_TABLES):
        for r in range(16):
            b = OFF_TABLES + i * 128 + r * 8
            s[b + 2] = s[b + 4] = s[b + 6] = MD_VIDE
    return bytes(s)

# ── La bibliothèque ───────────────────────────────────────────────────────
def entree(e):
    return 8 + e * 14

def lit_bibliotheque(u):
    if bytes(u[:6]) != BIB_MAGIE:
        return None
    slots = []
    for e in range(BIB_EMPLACEMENTS):
        o = entree(e)
        nom = bytes(u[o:o + BIB_NOM]).rstrip(b' \x00').decode('ascii', 'replace')
        dec = u[o + 10] | (u[o + 11] << 8)
        taille = u[o + 12] | (u[o + 13] << 8)
        slots.append((nom, dec, taille))
    return slots

def ecrit_bibliotheque(u, morceaux):
    """morceaux : liste de (nom, paquet). Ils sont posés à la suite."""
    u[0:6] = BIB_MAGIE
    u[6] = 1
    u[7] = BIB_EMPLACEMENTS
    for e in range(BIB_EMPLACEMENTS):
        o = entree(e)
        u[o:o + 14] = bytes(14)
    haut = BIB_DONNEES
    for e, (nom, paquet) in enumerate(morceaux[:BIB_EMPLACEMENTS]):
        if haut + len(paquet) > BIB_FIN:
            raise SystemExit(f"plus de place a partir de « {nom} » : "
                             f"{haut + len(paquet) - BIB_FIN} octets de trop")
        o = entree(e)
        n10 = (nom.upper()[:BIB_NOM]).ljust(BIB_NOM)
        u[o:o + BIB_NOM] = n10.encode('ascii', 'replace')
        u[o + 10] = haut & 0xFF
        u[o + 11] = haut >> 8
        u[o + 12] = len(paquet) & 0xFF
        u[o + 13] = len(paquet) >> 8
        u[haut:haut + len(paquet)] = paquet
        haut += len(paquet)
    return haut

# ── Les échantillons ──────────────────────────────────────────────────────
# ⚠️ SUR MEGA DRIVE, UN MORCEAU NE PORTE PAS SES SONS.
# Il ne range que le NUMÉRO de ses échantillons ; les sons sont compilés dans
# la ROM, parce que la cartouche n'ouvre pas sa carte SD à une ROM lancée
# comme un jeu. Verser un morceau sans sa banque le ferait donc jouer avec les
# sons d'un autre — un kick à la place d'une caisse claire.
#
# On reconstruit donc la banque À PARTIR DES MORCEAUX VERSÉS, et on renumérote
# leurs instruments : chaque .mdm a sa propre numérotation, et deux morceaux
# qui utilisent tous deux « l'échantillon 3 » n'en veulent pas le même.

# ⚠️ Ce n'est plus une fenetre de ROM mais la RAM DU Z80 : la cartouche ne
# repond pas aux lectures qu'il initie, donc on lui recopie l'echantillon.
MAX_SORTIES = 0xFFFFFF  # le compteur de sorties du pilote, sur 24 bits
# ⚠️ La ROM fait 512 Ko et le code en occupe environ 65. On s'autorise 384 Ko
# pour la banque : de quoi laisser large au code, aux tables et au bourrage
# d'alignement, sans quoi l'édition de liens échouerait au lieu d'écarter
# proprement le dernier échantillon.
PLACE_ROM = 320 * 1024
# ── Les capacites FIXES de la ROM rapiecable ─────────────────────────────
# Elles ne dependent pas du contenu : c'est ce qui permet a la DS d'ecrire
# dans l'image sans recompiler. Voir source/rom_plan.c.
ROM_MORCEAUX_MAX = 16          # combien de morceaux tiennent dans une ROM
ROM_MORCEAUX_OCTETS = 24576    # la place totale de leurs donnees comprimees
M_FIXE = 180943             # le bloc fixe d'un .mdm v12
M_HDR = 40

def echantillons_du_mdm(chemin):
    """Les 32 emplacements d'un .mdm : (nom, note, boucle, octets)."""
    d = open(chemin, 'rb').read()
    octets = struct.unpack_from('<I', d, M_FIXE)[0]
    debut = M_FIXE + 4 + 32 * M_HDR
    brut = d[debut:debut + octets]
    out = []
    for i in range(32):
        h = M_FIXE + 4 + i * M_HDR
        off = struct.unpack_from('<I', d, h + 24)[0]
        lon = struct.unpack_from('<I', d, h + 28)[0]
        bcl = struct.unpack_from('<i', d, h + 32)[0]
        nom = d[h:h + 24].split(b'\x00')[0].decode('latin1', 'replace')[:16].upper()
        note = d[h + 36]
        out.append(None if not lon else (nom, note, bcl, brut[off:off + lon]))
    return out

def banque_commune(sources):
    """Fusionne les échantillons de plusieurs morceaux en une seule banque.

    Rend (banque, entrees, remaps) où `remaps[i]` fait passer de l'ancien
    numéro au nouveau pour le morceau i. Deux échantillons identiques ne sont
    stockés qu'une fois — c'est fréquent, les morceaux d'un même auteur
    partagent leurs percussions.
    """
    entrees, donnees, connus = [], [], {}
    remaps = []
    for src in sources:
        remap = {}
        for i, e in enumerate(echantillons_du_mdm(src)):
            if e is None:
                continue
            nom, note, bcl, oct_ = e
            cle = (len(oct_), hash(oct_))
            if cle in connus:
                remap[i] = connus[cle]
                continue
            # ⚠️ ÉCARTÉ, jamais tronqué. Un échantillon coupé en plein milieu
            # ne joue pas « un peu moins bien », il claque. La RAM du Z80 n'est
            # plus la borne — le pilote lit un anneau réapprovisionné — mais
            # son compteur de sorties reste sur 16 bits.
            if len(oct_) > MAX_SORTIES:
                print(f"    ! {nom} : {len(oct_)} o, le pilote compte "
                      f"{MAX_SORTIES} sorties au plus — ecarte")
                continue
            if len(entrees) >= MAX_INSTR:
                print(f"    ! {nom} : plus d'emplacement (32) — ecarte")
                continue
            connus[cle] = len(entrees)
            remap[i] = len(entrees)
            entrees.append((nom, note, bcl, oct_))
        remaps.append(remap)

    # Bout a bout : plus rien n'oblige a aligner, l'echantillon est recopie
    # dans la RAM du Z80 avant lecture.
    banque, places = bytearray(), []
    for nom, note, bcl, oct_ in entrees:
        o = len(banque)
        if o + len(oct_) > PLACE_ROM:
            print(f"    ! {nom} : plus de place en ROM — ecarte")
            places.append(None)
            continue
        places.append(o)
        banque.extend(oct_)
    return bytes(banque), entrees, places, remaps

def renumerote(compact, remap):
    """Fait passer les instruments d'un morceau à la nouvelle numérotation.

    Un instrument dont l'échantillon a été écarté perd son échantillon (255) :
    sa voie se taira, ce qui est franc — plutôt que de jouer le son du voisin.
    """
    for i in range(MAX_INSTR):
        b = OFF_INSTR + i * INSTR_OCTETS
        if compact[b + 60] != 3:
            continue
        v = compact[b + 61]
        compact[b + 61] = remap.get(v, MD_VIDE) if v < 32 else MD_VIDE

def ecrit_banque(chemin, entrees, places, banque):
    """Ecrit la banque en DEUX fichiers, via wav2banque.

    ⚠️ On delegue au lieu de recopier : les deux outils ecrivaient le meme
    fichier dans deux formats differents, et le second laissait un
    banque_pcm.c perime que le premier faisait entrer en collision — « erreur :
    redefinition de pcm_offset », sans que rien ne dise d'ou venait le
    doublon. Un seul ecrivain, un seul format.
    """
    import wav2banque
    gardes = [(e, p_) for e, p_ in zip(entrees, places) if p_ is not None]
    wav2banque.ecris_banque(
        chemin, "les morceaux verses",
        [(e[0], e[3]) for e, _ in gardes],       # (nom, octets)
        [p_ for _, p_ in gardes],
        banque,
        [e[1] for e, _ in gardes],               # notes de base
        [e[2] for e, _ in gardes],               # boucles
        [e[0][:16] for e, _ in gardes])

def cmd_lire(chemin):
    u = depaquette(open(chemin, 'rb').read())
    slots = lit_bibliotheque(u)
    if slots is None:
        raise SystemExit("marque GTLIB1 absente : ce n'est pas une bibliotheque "
                         "GeneTracker")
    vus = 0
    for e, (nom, dec, taille) in enumerate(slots):
        if not taille:
            continue
        vus += 1
        print(f"  {e:2d}  {nom:10s}  {taille:5d} octets")
    print(f"  {vus} morceau(x), {BIB_FIN - BIB_DONNEES} octets de rayon")

def nom_de_rom(src):
    """geneTrackerTUTU, a partir de morceaux/TUTU.MDM."""
    base = os.path.splitext(os.path.basename(src))[0]
    propre = "".join(c for c in base if c.isalnum() or c in "_-")
    return "geneTracker" + (propre or "SANSNOM")

def cmd_verser(sources):
    """UNE ROM PAR MORCEAU.

    ⚠️ C'EST LA REGLE, ET ELLE VIENT D'UNE CONTRAINTE REELLE : la banque
    d'echantillons de la ROM ne tient que 32 emplacements. Deux morceaux qui
    la partagent s'amputent l'un l'autre des qu'ils depassent ce compte, en
    silence. Une ROM par morceau, chacune avec SA banque, et le probleme
    n'existe plus.

    Avant, verser plusieurs morceaux les mettait tous dans une seule ROM, et
    verser un morceau seul faisait DISPARAITRE les autres — un piege qu'il
    fallait avoir en tete a chaque commande.
    """
    racine = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    faites = []
    for src in sources:
        # La banque D'ABORD : c'est elle qui decide de la numerotation, et le
        # morceau doit etre renumerote avant d'etre comprime.
        banque, entrees, places, remaps = banque_commune([src])
        compact = bytearray(mdm2sram.convertis(src))
        renumerote(compact, remaps[0])
        paquet = lzss(creux(compact))
        nom = os.path.splitext(os.path.basename(src))[0].upper()[:10]
        rom = nom_de_rom(src)
        print(f"  {nom:10s} {os.path.getsize(src):7d} -> {len(paquet):5d} octets, "
              f"banque {sum(1 for x in places if x is not None)} echantillons "
              f"({len(banque)} o)")

        # ── LA ROM EST RAPIECABLE ────────────────────────────────────────
        # ⚠️ CAPACITES FIXES, ET C'EST TOUT L'ENJEU. La DS ne peut pas
        # recompiler la ROM : pour y verser un morceau elle doit ECRIRE DANS
        # L'IMAGE. Il faut donc que les zones soient a un emplacement et d'une
        # taille connus d'avance, et qu'un descripteur permette de les
        # retrouver sans deviner. Des tableaux ajustes au contenu, comme
        # avant, rendaient l'image impossible a modifier ailleurs qu'ici.
        with open(os.path.join(racine, 'source', 'morceaux_rom.h'), 'w') as g:
            g.write("// Genere par outils/bibliotheque.py — ne pas editer.\n")
            g.write("#ifndef MORCEAUX_ROM_H\n#define MORCEAUX_ROM_H\n"
                    "#include <stdint.h>\n\n")
            g.write("#define MORCEAUX_ROM_MAX %d\n" % ROM_MORCEAUX_MAX)
            g.write("#define MORCEAUX_ROM_CAPACITE %d\n\n" % ROM_MORCEAUX_OCTETS)
            g.write("extern const uint8_t  morceaux_rom_n;\n")
            g.write("extern const char     morceaux_rom_nom[MORCEAUX_ROM_MAX][11];\n")
            g.write("extern const uint16_t morceaux_rom_taille[MORCEAUX_ROM_MAX];\n")
            g.write("extern const uint32_t morceaux_rom_offset[MORCEAUX_ROM_MAX];\n")
            g.write("extern const uint8_t  morceaux_rom_data[MORCEAUX_ROM_CAPACITE];\n")
            g.write("\n#endif\n")
        with open(os.path.join(racine, 'source', 'morceaux_rom.c'), 'w') as g:
            g.write("// Genere par outils/bibliotheque.py — ne pas editer.\n")
            g.write('#include "morceaux_rom.h"\n\n')
            noms, tailles, offs, blob = [nom], [len(paquet)], [0], paquet
            if len(blob) > ROM_MORCEAUX_OCTETS:
                raise SystemExit(f"{nom} : {len(blob)} octets comprimes, la ROM "
                                 f"en reserve {ROM_MORCEAUX_OCTETS}")
            g.write("const uint8_t morceaux_rom_n = %d;\n" % len(noms))
            g.write("const char morceaux_rom_nom[MORCEAUX_ROM_MAX][11] = {%s};\n"
                    % ",".join('"%s"' % (noms[k] if k < len(noms) else "")
                               for k in range(ROM_MORCEAUX_MAX)))
            g.write("const uint16_t morceaux_rom_taille[MORCEAUX_ROM_MAX] = {%s};\n"
                    % ",".join(str(tailles[k] if k < len(tailles) else 0)
                               for k in range(ROM_MORCEAUX_MAX)))
            g.write("const uint32_t morceaux_rom_offset[MORCEAUX_ROM_MAX] = {%s};\n\n"
                    % ",".join(str(offs[k] if k < len(offs) else 0)
                               for k in range(ROM_MORCEAUX_MAX)))
            g.write("const uint8_t morceaux_rom_data[MORCEAUX_ROM_CAPACITE] = {\n")
            for i2 in range(0, len(blob), 16):
                g.write("  " + ",".join(str(x) for x in blob[i2:i2+16]) + ",\n")
            g.write("};\n")
        # ⚠️ ET LA BANQUE. Sans elle, le morceau verse jouerait les sons de la
        # banque precedente : le fichier ne porte que des numeros.
        ecrit_banque(os.path.join(racine, 'source', 'banque_pcm.h'),
                     entrees, places, banque)
        r = subprocess.run([os.path.join(racine, 'build.sh'), rom], cwd=racine,
                           capture_output=True, text=True)
        for l in (r.stdout + r.stderr).splitlines():
            if 'octets' in l or 'carte' in l or 'error' in l:
                print("  " + l.strip())
        if r.returncode:
            raise SystemExit("la ROM n'a pas ete construite")
        faites.append(rom + ".bin")
    print(f"  {len(faites)} ROM(s) : " + ", ".join(faites))

def banque_de_la_rom():
    """Relit la banque compilée dans la ROM, pour la rendre au .mdm.

    ⚠️ SANS ÇA LE SENS RETOUR EST BOITEUX : un morceau sorti de la Mega Drive
    ne porterait que des numéros, et il faudrait lui prêter les échantillons
    d'un autre fichier pour l'entendre. On les reprend donc là où ils sont
    vraiment — dans `source/banque_pcm.h`, qui EST la banque de la console.
    """
    # ⚠️ Les DONNEES vivent dans banque_pcm.c, pas dans l'en-tete : celui-ci
    # ne porte plus que des declarations depuis qu'on a scinde les deux (une
    # banque `static const` dans un en-tete etait emise une fois par fichier
    # qui l'incluait, et faisait deborder la ROM).
    racine = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    chemin = os.path.join(racine, 'source', 'banque_pcm.c')
    if not os.path.exists(chemin):
        return None
    import re
    s = open(chemin).read()
    def tab(nom):
        m = re.search(nom + r'\[32\] = \{([^}]*)\}', s)
        return [int(x) for x in m.group(1).split(',')] if m else [0] * 32
    offs, lons = tab('pcm_offset'), tab('pcm_longueur')
    notes, bcls = tab('pcm_note'), tab('pcm_boucle')
    mn = re.search(r'pcm_nom\[32\]\[17\] = \{([^}]*)\}', s)
    noms = [x.strip().strip('"') for x in mn.group(1).split(',')] if mn else [""] * 32
    mb = re.search(r'pcm_banque\[\d+\][^=]*= \{([^}]*)\}', s, re.S)
    banque = bytes(int(x) for x in mb.group(1).replace('\n', '').split(',') if x.strip())
    return offs, lons, notes, bcls, noms, banque

def attache_banque(mdm):
    """Recolle la banque de la ROM dans un .mdm fraîchement converti."""
    b = banque_de_la_rom()
    if b is None:
        return mdm
    offs, lons, notes, bcls, noms, brut = b
    m = bytearray(mdm[:M_FIXE])
    corps = bytearray()
    entetes = bytearray()
    for i in range(32):
        h = bytearray(M_HDR)
        if lons[i]:
            n = noms[i].encode('latin1', 'replace')[:23]
            h[0:len(n)] = n
            struct.pack_into('<I', h, 24, len(corps))
            struct.pack_into('<I', h, 28, lons[i])
            struct.pack_into('<i', h, 32, bcls[i])
            h[36] = notes[i]
            corps += brut[offs[i]:offs[i] + lons[i]]
        entetes += h
    return bytes(m) + struct.pack('<I', len(corps)) + bytes(entetes) + bytes(corps)

def cmd_sortir(chemin, dossier, source=None):
    u = depaquette(open(chemin, 'rb').read())
    slots = lit_bibliotheque(u)
    if slots is None:
        raise SystemExit("marque GTLIB1 absente : ce n'est pas une bibliotheque "
                         "GeneTracker")
    os.makedirs(dossier, exist_ok=True)
    vide = morceau_vide()
    n = 0
    for e, (nom, dec, taille) in enumerate(slots):
        if not taille:
            continue
        compact = creux_inverse(lzss_inverse(u[dec:dec + taille]), vide)
        mdm = sram2mdm.convertis(compact, source)
        if not source:
            mdm = attache_banque(mdm)
        # ⚠️ Le numero d'emplacement entre TOUJOURS dans le nom : rien
        # n'empeche deux morceaux de s'appeler « SONG » sur la console, et
        # sans ca le second ecrasait le premier en silence.
        base = nom.strip() or "SANS-NOM"
        sortie = os.path.join(dossier, f"{e:02d}-{base}.mdm")
        open(sortie, 'wb').write(mdm)
        print(f"  {e:2d}  {nom:10s} -> {sortie}")
        n += 1
    print(f"  {n} morceau(x) sortis")

def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    verbe, chemin = sys.argv[1], sys.argv[2]
    if verbe == 'lire':
        cmd_lire(chemin)
    elif verbe == 'verser':
        # ⚠️ « verser » ne prend PLUS de sauvegarde : il ne fabrique que des
        # ROMs. Le deuxieme argument est deja un .mdm.
        cmd_verser(sys.argv[2:])
    elif verbe == 'sortir':
        if len(sys.argv) < 4:
            raise SystemExit("il faut un dossier de sortie")
        cmd_sortir(chemin, sys.argv[3],
                   sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        raise SystemExit(__doc__)

if __name__ == '__main__':
    main()
