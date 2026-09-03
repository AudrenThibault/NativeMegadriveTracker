#!/usr/bin/env python3
"""Assembleur Z80 minimal : juste ce qu'il faut pour le pilote PCM.

Ecrit parce qu'aucun assembleur Z80 n'est installe sur cette machine et qu'un
pilote assemble a la main est une source d'erreurs qu'on ne voit pas — un
octet de travers donne un bruit, pas un message. Ici chaque instruction est
nommee, les etiquettes sont resolues, et le resultat est verifiable.

On n'implemente QUE les instructions employees. Toute autre leve une erreur
plutot que de produire un opcode faux.
"""
import sys, re

def assemble(src):
    lignes = []
    for l in src.splitlines():
        l = l.split(';')[0].strip()
        if l: lignes.append(l)

    etiq, taille = {}, 0
    def encode(ins, resolu):
        m = lambda p: re.fullmatch(p, ins, re.I)
        def val(x):
            x = x.strip()
            if x.lower() in etiq: return etiq[x.lower()]
            return int(x, 0)
        if (g := m(r'(\w+):')): return None, g.group(1).lower()
        if m(r'di'): return [0xF3], None
        if m(r'nop'): return [0x00], None
        if (g := m(r'ld sp,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0x31, v & 0xFF, v >> 8], None
        if (g := m(r'ld hl,\s*\((\S+)\)')): v = val(g.group(1)) if resolu else 0; return [0x2A, v & 0xFF, v >> 8], None
        if (g := m(r'ld \((\S+)\),\s*hl')): v = val(g.group(1)) if resolu else 0; return [0x22, v & 0xFF, v >> 8], None
        if (g := m(r'ld hl,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0x21, v & 0xFF, v >> 8], None
        if (g := m(r'ld de,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0x11, v & 0xFF, v >> 8], None
        if (g := m(r'ld bc,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0x01, v & 0xFF, v >> 8], None
        if m(r'ld a,\s*\(hl\)'): return [0x7E], None
        if (g := m(r'ld a,\s*\((\S+)\)')): v = val(g.group(1)) if resolu else 0; return [0x3A, v & 0xFF, v >> 8], None
        if (g := m(r'ld \((\S+)\),\s*a')): v = val(g.group(1)) if resolu else 0; return [0x32, v & 0xFF, v >> 8], None
        if (g := m(r'ld a,\s*(\S+)')):
            r = {'b':0x78,'c':0x79,'d':0x7A,'e':0x7B,'h':0x7C,'l':0x7D}.get(g.group(1).lower())
            if r: return [r], None
            if g.group(1).lower() == '(hl)': return [0x7E], None
            v = val(g.group(1)) if resolu else 0; return [0x3E, v & 0xFF], None
        if (g := m(r'ld (\w),\s*a')):
            r = {'b':0x47,'c':0x4F,'d':0x57,'e':0x5F,'h':0x67,'l':0x6F}[g.group(1).lower()]
            return [r], None
        if (g := m(r'ld (b|c|d|e|h|l),\s*(\S+)')):
            r = {'b':0x06,'c':0x0E,'d':0x16,'e':0x1E,'h':0x26,'l':0x2E}[g.group(1).lower()]
            v = val(g.group(2)) if resolu else 0; return [r, v & 0xFF], None
        if (g := m(r'ld \(hl\),\s*a')): return [0x77], None
        if (g := m(r'ld \(de\),\s*a')): return [0x12], None
        if m(r'inc hl'): return [0x23], None
        if m(r'dec hl'): return [0x2B], None
        if m(r'inc de'): return [0x13], None
        if m(r'dec de'): return [0x1B], None
        if (g := m(r'dec (\w)')):
            return [{'a':0x3D,'b':0x05,'c':0x0D,'d':0x15,'e':0x1D,'h':0x25,'l':0x2D}[g.group(1).lower()]], None
        if (g := m(r'inc (\w)')):
            return [{'a':0x3C,'b':0x04,'c':0x0C,'d':0x14,'e':0x1C,'h':0x24,'l':0x2C}[g.group(1).lower()]], None
        if m(r'or a'): return [0xB7], None
        if m(r'cp c'): return [0xB9], None
        if (g := m(r'add a,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0xC6, v & 0xFF], None
        # adc a,n (0xCE) : l'addition qui reprend la retenue de la fraction.
        if (g := m(r'adc a,\s*(\S+)')): v = val(g.group(1)) if resolu else 0; return [0xCE, v & 0xFF], None
        if (g := m(r'cp\s+(\S+)')): v = val(g.group(1)) if resolu else 0; return [0xFE, v & 0xFF], None
        if m(r'rrca'): return [0x0F], None
        # srl a (CB 3F) : decalage a droite, zero entrant. C'est ce qu'il faut
        # pour presenter les neuf bits de la banque, du plus faible au plus fort.
        if m(r'srl a'): return [0xCB, 0x3F], None
        # call nn (0xCD), ret (0xC9), push/pop bc : le pilote appelle une
        # routine de banque, il lui faut la pile.
        if (g := m(r'call\s+(\S+)')): v = val(g.group(1)) if resolu else 0; return [0xCD, v & 0xFF, (v >> 8) & 0xFF], None
        if m(r'ret'): return [0xC9], None
        if m(r'push bc'): return [0xC5], None
        if m(r'pop bc'): return [0xC1], None
        # exx : bascule vers le jeu de registres alternatif.
        if m(r'exx'): return [0xD9], None
        if m(r'ei'): return [0xFB], None
        if (g := m(r'jr (nz|z|nc|c),\s*(\S+)')):
            op = {'nz':0x20,'z':0x28,'nc':0x30,'c':0x38}[g.group(1).lower()]
            d = (etiq[g.group(2).lower()] - (taille + 2)) if resolu else 0
            return [op, d & 0xFF], None
        if (g := m(r'jr\s+(\S+)')):
            d = (etiq[g.group(1).lower()] - (taille + 2)) if resolu else 0
            return [0x18, d & 0xFF], None
        if (g := m(r'jp\s+(\S+)')): v = etiq[g.group(1).lower()] if resolu else 0; return [0xC3, v & 0xFF, v >> 8], None
        if (g := m(r'djnz\s+(\S+)')):
            d = (etiq[g.group(1).lower()] - (taille + 2)) if resolu else 0
            return [0x10, d & 0xFF], None
        raise SystemExit(f"instruction non geree : {ins}")

    for passe in (0, 1):
        taille, sortie = 0, []
        for ins in lignes:
            octets, lab = encode(ins, passe == 1)
            if lab is not None:
                if passe == 0: etiq[lab] = taille
                continue
            sortie += octets
            taille = len(sortie)
    return bytes(sortie), etiq

if __name__ == '__main__':
    b, etiq = assemble(open(sys.argv[1]).read())
    nom = sys.argv[3] if len(sys.argv) > 3 else 'pilote_z80'
    with open(sys.argv[2], 'w') as f:
        f.write(f"// Assemble par outils/z80asm.py depuis {sys.argv[1]} — ne pas editer.\n")
        f.write("#ifndef PILOTE_Z80_H\n#define PILOTE_Z80_H\n#include <stdint.h>\n\n")
        f.write(f"#define PILOTE_Z80_TAILLE {len(b)}\n")
        # Les points de RETOUCHE : le 68000 y ecrit le pas de lecture, ce qui
        # transpose l'echantillon. Leur position vient de l'assemblage, jamais
        # d'un comptage a la main.
        for nom_e, adr in sorted(etiq.items()):
            if nom_e.startswith('patch_'):
                f.write(f"#define PILOTE_{nom_e[6:].upper()} {adr + 1}\n")
        f.write(f"static const uint8_t {nom}[{len(b)}] = {{\n")
        for i in range(0, len(b), 16):
            f.write("  " + ",".join(f"0x{x:02X}" for x in b[i:i+16]) + ",\n")
        f.write("};\n\n#endif\n")
    print(f"{sys.argv[2]} : {len(b)} octets")
