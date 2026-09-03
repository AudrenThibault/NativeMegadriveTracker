# Pose l'en-tête de cartouche Mega Drive aux décalages exacts, puis la somme de
# contrôle. Écrit ici une fois pour toutes : compter des espaces dans un .ascii
# est une source d'erreurs, et l'assembleur ne prévient qu'au débordement.
import struct, sys

def entete(d, nom, serie, sram=None, notes=""):
    def txt(off, s, n):
        b = s.encode('ascii')[:n].ljust(n, b' ')
        d[off:off + n] = b
    txt(0x100, "SEGA MEGA DRIVE ", 16)
    txt(0x110, "(C)AUDREN 2026  ", 16)
    txt(0x120, nom, 48)
    txt(0x150, nom, 48)
    txt(0x180, serie, 14)
    struct.pack_into('>H', d, 0x18E, 0)
    txt(0x190, "J", 16)
    struct.pack_into('>IIII', d, 0x1A0, 0, len(d) - 1, 0x00FF0000, 0x00FFFFFF)
    if sram:
        debut, fin = sram
        d[0x1B0:0x1B2] = b"RA"
        d[0x1B2:0x1B4] = bytes((0xF8, 0x20))
        struct.pack_into('>II', d, 0x1B4, debut, fin)
    else:
        txt(0x1B0, "", 12)
    txt(0x1BC, "", 12)
    txt(0x1C8, notes, 40)
    txt(0x1F0, "JUE", 16)
    s = 0
    for i in range(0x200, len(d), 2):
        s = (s + struct.unpack_from('>H', d, i)[0]) & 0xFFFF
    struct.pack_into('>H', d, 0x18E, s)
    return s

if __name__ == "__main__":
    f, nom, serie, taille = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    sram = None
    if len(sys.argv) > 5 and sys.argv[5] != "-":
        a, b = sys.argv[5].split(":")
        sram = (int(a, 16), int(b, 16))
    d = bytearray(open(f, 'rb').read())
    d += b'\xFF' * (taille - len(d))
    s = entete(d, nom, serie, sram, "DIAGNOSTIC")
    open(f, 'wb').write(d)
    print(f"{f} : {len(d)} octets, sram={sram}, somme {s:04X}")
