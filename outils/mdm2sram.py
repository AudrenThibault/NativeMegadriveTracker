#!/usr/bin/env python3
"""Convertit un .mdm en image SRAM compacte, embarquable dans la ROM.

Pourquoi une conversion sur le Mac plutot qu'un lecteur de .mdm sur la console :
le bloc fixe d'un .mdm fait 176,7 Ko, on en a 32. Et le projet DS embarquait le
.dmf brut (507 Ko) avec son analyseur — impensable ici. Tout le travail se fait
donc a la compilation, et la Mega Drive ne recoit que la forme qu'elle edite.

Heureuse coincidence, verifiee et non supposee : les 59 premiers octets d'un
enregistrement d'instrument .mdm sont EXACTEMENT ceux qu'on garde, dans le meme
ordre. Le reste (295 octets de macros DefleMask, 62 de bourrage) est jete.
"""
import struct, sys

# ── Le .mdm, version 12 ────────────────────────────────────────────────────
M_SONG, M_CHAINS, M_PHRASES = 106, 2666, 6762
M_INSTR, M_NOMS, M_TABLES = 35322, 165882, 176847
M_NB_CHAINS, M_NB_PHRASES, M_NB_INSTR, M_NB_TABLES = 128, 255, 255, 32
M_INSTR_REC = 512

# ── Notre forme compacte ───────────────────────────────────────────────────
CANAUX, SONG_LIGNES = 10, 256
MAX_CHAINS, MAX_PHRASES, MAX_INSTR, MAX_TABLES = 96, 160, 32, 16
# ⚠️ 216 et non 80 : la place des MACROS PSG (voir moteur/morceau/md_song.h).
#   63 vol_len  64 vol_boucle  65-128 vol[64]
#   129 arp_len 130 arp_boucle 131 arp_fixe  132-195 arp[64]
INSTR_OCTETS = 216
MACRO_PAS = 64

OFF_ENTETE, T_ENTETE = 0, 64
OFF_SONG = OFF_ENTETE + T_ENTETE
OFF_CHAINS = OFF_SONG + CANAUX * SONG_LIGNES
OFF_PHRASES = OFF_CHAINS + MAX_CHAINS * 16 * 2
OFF_INSTR = OFF_PHRASES + MAX_PHRASES * 16 * 7
OFF_TABLES = OFF_INSTR + MAX_INSTR * INSTR_OCTETS
TAILLE = OFF_TABLES + MAX_TABLES * 16 * 8
VIDE = 0xFF

def convertis(src):
    d = open(src, 'rb').read()
    if d[:8] != b'MDMTRACK':
        raise SystemExit(f"{src} n'est pas un .mdm")
    version = struct.unpack_from('<H', d, 8)[0]
    if version != 12:
        print(f"  ! version {version}, attendue 12 — verifier les decalages", file=sys.stderr)

    s = bytearray(TAILLE)
    debordements = []

    # En-tete
    s[0:6] = b'MDTRK1'
    s[6] = 1                     # version de la forme compacte
    s[7] = d[11] or 125          # tempo
    s[8] = d[12] or 6            # vitesse
    s[9] = CANAUX
    # macro_speedup et finetune : rangés dès maintenant, meme s'ils ne servent
    # pas encore. Les ajouter plus tard deplacerait tout l'en-tete.
    s[10:12] = d[14:16]
    s[12:14] = d[16:18]
    # Le BPM, range tel quel : c'est lui que le sequenceur lit.
    tempo = d[11] or 125
    vit = d[12] or 6
    # ⚠️ LE BPM RÉEL N'EST PAS tempo*60/(vitesse*4).
    #
    # `tempo` est une fréquence d'interruption AT2, pas un BPM, et le fichier
    # porte un DÉSACCORD DE TEMPO qui le corrige. TUTU.MDM a tempo 125,
    # vitesse 6 et un désaccord de -150 : la formule naïve donne 312 BPM, la
    # vraie 125. Le morceau partait donc deux fois et demie trop vite.
    #
    # C'est le calcul du moteur DS (md_replayer_get_bpm) :
    #     BPM = tempo*60/(vitesse*lignes_par_temps) * (base+desaccord)/base
    # où `base` est la première fréquence >= 250 divisible par tempo*macro.
    def at2_base(t, m):
        t = t or 50
        m = m or 1
        div = t * m or (t or 50)
        irq = 250
        while irq % div:
            irq += 1
        return min(irq, 1000)

    macro = struct.unpack_from('<H', d, 14)[0]
    desaccord = struct.unpack_from('<h', d, 16)[0]
    lignes_par_temps = d[18] or 4
    base = at2_base(tempo, macro)
    echelle = (base + desaccord) / base if base > 0 else 1.0
    if echelle < 0.05:
        echelle = 0.05
    bpm = int(tempo * 60.0 / (vit * lignes_par_temps) * echelle + 0.5)
    bpm = max(20, min(400, bpm))
    s[14] = bpm & 0xFF
    s[15] = bpm >> 8
    nom = d[20:63].split(b'\x00')[0][:16]
    s[16:16+len(nom)] = nom

    # SONG — on ecarte les chains hors limites plutot que de les tronquer en
    # silence : un numero replie designerait un AUTRE chain, donc un autre son.
    for c in range(CANAUX):
        for l in range(SONG_LIGNES):
            v = d[M_SONG + c * SONG_LIGNES + l]
            if v != VIDE and v >= MAX_CHAINS:
                debordements.append(f"chain {v} (max {MAX_CHAINS-1})"); v = VIDE
            s[OFF_SONG + c * SONG_LIGNES + l] = v

    # CHAINS
    for i in range(MAX_CHAINS):
        for l in range(16):
            o = M_CHAINS + (i * 16 + l) * 2
            ph, tsp = d[o], d[o + 1]
            if ph != VIDE and ph >= MAX_PHRASES:
                debordements.append(f"phrase {ph} (max {MAX_PHRASES-1})"); ph, tsp = VIDE, 0
            n = OFF_CHAINS + (i * 16 + l) * 2
            s[n], s[n + 1] = ph, tsp

    # PHRASES — copie directe, les sept octets par ligne sont identiques
    for i in range(MAX_PHRASES):
        a = M_PHRASES + i * 16 * 7
        b = OFF_PHRASES + i * 16 * 7
        bloc = bytearray(d[a:a + 16 * 7])
        for l in range(16):
            ins = bloc[l * 7 + 1]
            if ins not in (0, VIDE) and ins > MAX_INSTR:
                debordements.append(f"instrument {ins} (max {MAX_INSTR})")
                bloc[l * 7 + 1] = 0
        s[b:b + 16 * 7] = bloc

    coupees = []
    # INSTRUMENTS — 59 octets repris tels quels, puis les macros et le nom
    for i in range(MAX_INSTR):
        a = M_INSTR + i * M_INSTR_REC
        b = OFF_INSTR + i * INSTR_OCTETS
        s[b:b + 59] = d[a:a + 59]
        s[b + 59] = d[a + 88]          # numero de table
        s[b + 60] = d[a + 417]         # kind
        s[b + 61] = d[a + 418]         # echantillon PCM
        s[b + 62] = d[a + 419]         # volume PCM
        # ── LES TROIS MACROS PSG ──────────────────────────────────────
        # Volume, arpege, mode de bruit. Sans elles un instrument PSG venu du
        # tracker DS arrive muet ou plat : c'est la macro de volume qui donne
        # son enveloppe, et TUTU en emploie.
        #
        # ⚠️ Le .mdm en garde 128 pas, la cartouche 64 (volume) et 32 (arpege,
        # bruit) : les trente-deux instruments doivent tenir dans 32 Ko. On
        # coupe, mais on le DIT — un morceau qui perd la fin d'une macro ne
        # sonne pas « un peu moins bien », il change de dessin.
        for (o_len, o_bcl, o_mac, m_len, m_bcl, m_mac, pas, quoi) in (
                (63, 64, 65,  156, 157, 158, 64, "volume"),
                (129, 130, 132, 286, 287, 289, 32, "arpege"),
                (164, 165, 166, 122, 123, 124, 32, "bruit")):
            n_mac = d[a + m_len]
            if n_mac > pas:
                coupees.append(f"instr {i:02d} : macro de {quoi} de {n_mac} pas, "
                               f"coupee a {pas}")
                n_mac = pas
            s[b + o_len] = n_mac
            bcl = d[a + m_bcl]
            # Un point de bouclage tombe hors de la macro coupee : on l'enleve
            # plutot que de faire boucler sur du vide.
            s[b + o_bcl] = bcl if (bcl == 0xFF or bcl < n_mac) else 0xFF
            s[b + o_mac:b + o_mac + n_mac] = d[a + m_mac:a + m_mac + n_mac]
        s[b + 131] = d[a + 288]        # arpege en notes absolues ?

        # ⚠️ Le nom vit en 198 : tout ce qui precede est occupe par les macros.
        nm = d[M_NOMS + i * 43:M_NOMS + (i + 1) * 43].split(b'\x00')[0][:16]
        s[b + 198:b + 198 + len(nm)] = nm

    # TABLES
    n = MAX_TABLES * 16 * 8
    s[OFF_TABLES:OFF_TABLES + n] = d[M_TABLES:M_TABLES + n]

    if coupees:
        print(f"  ! {len(coupees)} macro(s) coupees :", file=sys.stderr)
        for c in coupees[:8]:
            print(f"      {c}", file=sys.stderr)
    if debordements:
        vus = sorted(set(debordements))
        print(f"  ! {len(debordements)} references hors limites, mises a vide :", file=sys.stderr)
        for v in vus[:8]:
            print(f"      {v}", file=sys.stderr)
    return s

def en_tete_c(s, nom_var, src):
    lignes = [f"// Genere par outils/mdm2sram.py depuis {src} — ne pas editer.",
              "#ifndef MORCEAU_DEMO_H", "#define MORCEAU_DEMO_H", "#include <stdint.h>", "",
              f"#define MORCEAU_DEMO_TAILLE {len(s)}",
              f"static const uint8_t {nom_var}[{len(s)}] = {{"]
    for i in range(0, len(s), 16):
        lignes.append("  " + ",".join(f"0x{b:02X}" for b in s[i:i+16]) + ",")
    lignes += ["};", "", "#endif"]
    return "\n".join(lignes) + "\n"

# ── La banque d'echantillons ───────────────────────────────────────────────
# Elle ne peut PAS vivre en SRAM : 45 Ko pour le seul morceau de reference,
# contre 32 Ko de sauvegarde en tout. Elle part donc en ROM, convertie a la
# compilation comme le morceau.
#
# Consequence a assumer : on ne peut pas enregistrer un echantillon SUR la Mega
# Drive. La cartouche V3 n'offre aucune ecriture vers la carte SD, donc rien ne
# permettrait de le conserver. Les echantillons viennent du .mdm, point.
FIXE = 180943   # taille du bloc fixe d'un .mdm v12
HDR = 40

FENETRE = 32768    # la fenetre que le Z80 voit en 0x8000-0xFFFF
PLACE_ROM = 160 * 1024   # ce qu'on s'autorise pour la banque, code compris

def banque(src):
    """Extrait les echantillons et les REPLACE pour qu'aucun ne franchisse une
    frontiere de 32 Ko.

    C'est ce qui permet au pilote Z80 de ne jamais toucher au registre de
    banque : il lit droit devant lui du debut a la fin. Un pilote qui devrait
    changer de banque en cours d'echantillon serait trois fois plus long, donc
    trois fois plus difficile a verifier — et il n'y a pas d'assembleur Z80 sur
    cette machine pour le deboguer confortablement.

    Le prix est un peu de ROM perdue en bourrage. Sur 256 Ko, c'est indolore.
    """
    d = open(src, 'rb').read()
    octets = struct.unpack_from('<I', d, FIXE)[0]
    debut = FIXE + 4 + 32 * HDR
    brut = d[debut:debut + octets]

    sortie = bytearray()
    ech = []
    for i in range(32):
        h = FIXE + 4 + i * HDR
        off = struct.unpack_from('<I', d, h + 24)[0]
        lon = struct.unpack_from('<I', d, h + 28)[0]
        bcl = struct.unpack_from('<i', d, h + 32)[0]
        nom = d[h:h + 24].split(b'\x00')[0].decode('latin1', 'replace')[:16].upper()
        note = d[h + 36]
        if not lon:
            ech.append((0, 0, note, -1, "")); continue
        # Un echantillon plus grand qu'une fenetre est ECARTE, pas tronque.
        # Un sample coupe en plein milieu ne joue pas « un peu moins bien » :
        # il claque. Mieux vaut que la voie se taise — l'instrument existe
        # toujours, il ne sort simplement aucun son.
        if lon > FENETRE:
            print(f"  ! echantillon {i} : {lon} o, plus qu'une fenetre de "
                  f"{FENETRE} — ECARTE, cette voie sera muette", file=sys.stderr)
            ech.append((0, 0, note, -1, "")); continue
        # Et si la ROM est pleine, on ecarte aussi plutot que de deborder.
        if len(sortie) + FENETRE > PLACE_ROM:
            print(f"  ! echantillon {i} : plus de place en ROM — ECARTE",
                  file=sys.stderr)
            ech.append((0, 0, note, -1, "")); continue
        # Bourrer jusqu'a la fenetre suivante s'il ne tient pas dans celle-ci.
        if (len(sortie) % FENETRE) + lon > FENETRE:
            sortie += b'\x80' * (FENETRE - (len(sortie) % FENETRE))
        ech.append((len(sortie), lon, note, bcl, nom))
        sortie += brut[off:off + lon]
    return ech, bytes(sortie)

def entete_banque(ech, pcm, src):
    L = [f"// Genere par outils/mdm2sram.py depuis {src} — ne pas editer.",
         "#ifndef BANQUE_PCM_H", "#define BANQUE_PCM_H", "#include <stdint.h>", "",
         "// Les echantillons sont deja en 8 bits non signes a 32 kHz : c'est",
         "// exactement ce que recevra le convertisseur du YM2612.",
         f"#define PCM_OCTETS {len(pcm)}", "",
         "static const uint32_t pcm_offset[32] = {" + ",".join(str(e[0]) for e in ech) + "};",
         "static const uint32_t pcm_longueur[32] = {" + ",".join(str(e[1]) for e in ech) + "};",
         "static const uint8_t  pcm_note[32] = {" + ",".join(str(e[2]) for e in ech) + "};",
         "static const int32_t  pcm_boucle[32] = {" + ",".join(str(e[3]) for e in ech) + "};",
         "// Les noms, pour la page SAMPLE : on montre CE QU'ON JOUE, pas un numero.",
         "static const char pcm_nom[32][17] = {"
         + ",".join('"' + e[4].replace('"', ' ')[:16] + '"' for e in ech) + "};", "",
         f"// Alignee sur 32 Ko : les decalages ne valent que si la base l est aussi.",
         f"static const uint8_t pcm_banque[{max(len(pcm),1)}] __attribute__((aligned(32768))) = {{"]
    for i in range(0, len(pcm), 24):
        L.append("  " + ",".join(f"0x{b:02X}" for b in pcm[i:i+24]) + ",")
    if not pcm: L.append("  0,")
    L += ["};", "", "#endif"]
    return "\n".join(L) + "\n"

if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit("usage: mdm2sram.py <source.mdm> <sortie.h>")
    img = convertis(sys.argv[1])
    open(sys.argv[2], 'w').write(en_tete_c(img, "morceau_demo", sys.argv[1].split('/')[-1]))
    plein = sum(1 for b in img if b not in (0, VIDE))
    print(f"{sys.argv[2]} : {len(img)} octets ({plein} non triviaux), tient dans 32768 : {len(img) <= 32768}")
    if len(sys.argv) > 3:
        ech, pcm = banque(sys.argv[1])
        open(sys.argv[3], 'w').write(entete_banque(ech, pcm, sys.argv[1].split('/')[-1]))
        n = sum(1 for e in ech if e[1])
        print(f"{sys.argv[3]} : {n} echantillons, {len(pcm)} octets de PCM")
