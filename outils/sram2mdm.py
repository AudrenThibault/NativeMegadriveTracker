#!/usr/bin/env python3
"""Extrait un morceau de la sauvegarde de la cartouche et le rend en .mdm.

Le chemin retour, et il ferme la boucle. La cartouche n'ouvre pas sa carte SD a
une ROM lancee comme un jeu (mesure : les registres rendent tous 4A78, la propre
prelecture du 68000 sur un bus non decode). GeneTracker ne peut donc pas ecrire
un .mdm lui-meme. Mais l'OS de la cartouche recopie la SRAM vers
EDMD/SAVE/<nom>.md des qu'on change de jeu — et ce fichier, on le lit ici.

Usage :
    sram2mdm.py "EDMD/SAVE/geneTracker.md" sortie.mdm [source.mdm]

Le troisieme argument est facultatif : il sert a RATTACHER la banque
d'echantillons. Elle ne peut pas changer sur la Mega Drive — les samples sont
embarques en ROM a la compilation — donc les reprendre du .mdm d'origine est
exact, pas un a-peu-pres. Sans lui, le .mdm sort sans echantillons.
"""
import struct, sys

# ── La forme compacte (voir moteur/morceau/md_song.h) ─────────────────────
CANAUX, SONG_LIGNES = 10, 256
MAX_CHAINS, MAX_PHRASES, MAX_INSTR, MAX_TABLES = 96, 160, 32, 16
# ⚠️ 216 et non 80 : la place des MACROS PSG (voir moteur/morceau/md_song.h).
#   63 vol_len  64 vol_boucle  65-128 vol[64]
#   129 arp_len 130 arp_boucle 131 arp_fixe  132-195 arp[64]
INSTR_OCTETS = 216
MACRO_PAS = 64

C_SONG = 64
C_CHAINS = C_SONG + CANAUX * SONG_LIGNES
C_PHRASES = C_CHAINS + MAX_CHAINS * 16 * 2
C_INSTR = C_PHRASES + MAX_PHRASES * 16 * 7
C_TABLES = C_INSTR + MAX_INSTR * INSTR_OCTETS

# ── Le .mdm v12 ───────────────────────────────────────────────────────────
M_SONG, M_CHAINS, M_PHRASES = 106, 2666, 6762
M_INSTR, M_NOMS, M_TABLES = 35322, 165882, 176847
M_FIXE = 180943
VIDE = 0xFF

def lit_sauvegarde(chemin):
    """La cartouche ecrit 64 Ko dont SEULS LES OCTETS IMPAIRS portent des
    donnees — mesure sur les six sauvegardes de la carte : 100 % des octets
    pairs valent zero. On accepte aussi un vidage deja depaquete."""
    d = open(chemin, 'rb').read()
    if len(d) >= 65536:
        u = d[1::2]
        if u[:6] == b'MDTRK1':
            return u
        if d[:6] == b'MDTRK1':      # deja depaquete
            return d
        raise SystemExit("signature MDTRK1 absente : ce n'est pas une sauvegarde GeneTracker")
    if d[:6] != b'MDTRK1':
        raise SystemExit("signature MDTRK1 absente")
    return d

def convertis(s, source=None):
    m = bytearray(M_FIXE + 4 + 32 * 40)
    m[0:8] = b'MDMTRACK'
    struct.pack_into('<H', m, 8, 12)
    m[10] = CANAUX
    m[11] = s[7] or 125          # tempo
    m[12] = s[8] or 6            # vitesse
    m[13] = 0
    m[14:16] = bytes(s[10:12])   # macro_speedup
    m[16:18] = bytes(s[12:14])   # finetune
    m[18] = 4
    nom = bytes(s[16:32]).split(b'\x00')[0]
    m[20:20 + len(nom)] = nom

    for c in range(CANAUX):
        a = C_SONG + c * SONG_LIGNES
        m[M_SONG + c * SONG_LIGNES:M_SONG + (c + 1) * SONG_LIGNES] = s[a:a + SONG_LIGNES]

    # Les chains et phrases au-dela de nos limites restent VIDES cote .mdm :
    # le format en accepte plus que nous, et le silence est franc.
    for i in range(128):
        b = M_CHAINS + i * 32
        if i < MAX_CHAINS:
            a = C_CHAINS + i * 32
            m[b:b + 32] = s[a:a + 32]
        else:
            for r in range(16):
                m[b + r * 2] = VIDE; m[b + r * 2 + 1] = 0

    for i in range(255):
        b = M_PHRASES + i * 112
        if i < MAX_PHRASES:
            a = C_PHRASES + i * 112
            m[b:b + 112] = s[a:a + 112]
        else:
            for r in range(16):
                o = b + r * 7
                m[o] = 0; m[o + 1] = 0; m[o + 2] = VIDE
                m[o + 3] = VIDE; m[o + 4] = 0; m[o + 5] = VIDE; m[o + 6] = 0

    for i in range(255):
        b = M_INSTR + i * 512
        if i < MAX_INSTR:
            a = C_INSTR + i * INSTR_OCTETS
            m[b:b + 59] = s[a:a + 59]      # les 59 premiers octets coincident
            m[b + 88] = s[a + 59]          # numero de table
            m[b + 417] = s[a + 60]         # kind
            m[b + 418] = s[a + 61]         # echantillon
            m[b + 419] = s[a + 62]         # volume PCM
            # Les trois macros PSG reprennent le chemin inverse. Le .mdm en
            # garde 128 pas, la cartouche 64 ou 32 : le reste part a zero,
            # comme une macro qui s'arrete.
            for (o_len, o_bcl, o_mac, m_len, m_bcl, m_mac, pas) in (
                    (63, 64, 65,  156, 157, 158, 64),
                    (129, 130, 132, 286, 287, 289, 32),
                    (164, 165, 166, 122, 123, 124, 32)):
                n_mac = min(s[a + o_len], pas)
                m[b + m_len] = n_mac
                m[b + m_bcl] = s[a + o_bcl]
                m[b + m_mac:b + m_mac + n_mac] = s[a + o_mac:a + o_mac + n_mac]
            m[b + 288] = s[a + 131]        # arpege en notes absolues ?

            nm = bytes(s[a + 198:a + 214]).split(b'\x00')[0]
            m[M_NOMS + i * 43:M_NOMS + i * 43 + len(nm)] = nm
        else:
            m[b + 88] = VIDE

    n = MAX_TABLES * 16 * 8
    m[M_TABLES:M_TABLES + n] = s[C_TABLES:C_TABLES + n]

    # ── La banque d'echantillons ─────────────────────────────────────────
    if source:
        d = open(source, 'rb').read()
        if d[:8] != b'MDMTRACK':
            raise SystemExit(f"{source} n'est pas un .mdm")
        m = bytearray(m[:M_FIXE]) + bytearray(d[M_FIXE:])
    return bytes(m)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    s = lit_sauvegarde(sys.argv[1])
    src = sys.argv[3] if len(sys.argv) > 3 else None
    m = convertis(s, src)
    open(sys.argv[2], 'wb').write(m)
    pcm = struct.unpack_from('<I', m, M_FIXE)[0] if len(m) > M_FIXE + 4 else 0
    print(f"{sys.argv[2]} : {len(m)} octets, banque PCM {pcm} o"
          + (f" (reprise de {src})" if src else " (aucun echantillon)"))
    # Le journal du tracker vit apres le morceau : on le rend au passage.
    j = bytes(s[28224:28224 + 1024])
    if j[:12] == b'GENETRK-LOG:':
        print("\n--- journal du tracker ---")
        print(j[12:].split(b'\x00')[0].decode('latin1'))
