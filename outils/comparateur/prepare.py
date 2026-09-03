#!/usr/bin/env python3
"""Fabrique un morceau compact d'UN instrument, pour le comparateur.

    prepare.py sortie.compact [champ=valeur ...]

Les champs sont ceux du disque d'instrument (voir md_codec.c) :
    alg fb ams pms lfo lfohz pan finetune
    op<N>.<mul|det|tl|ks|ar|d1r|d2r|d1l|rr|am|ssg>   N de 1 a 4

L'instrument de depart est celui de md_instr_defaut : celui de la DS.
"""
import sys

TOTAL, OFF_INSTR, INSTR_OCTETS = 28224, 23616, 80
OP_DEFAUT = [[0,1,22,0,31,8,12,2,8,0,0],
             [0,1, 4,0,31,6,14,2,8,0,0],
             [1,2,30,0,31,8,12,2,8,0,0],
             [0,1, 4,0,31,6,14,2,8,0,0]]
OP_CHAMP = {'det':0,'mul':1,'tl':2,'ks':3,'ar':4,'d1r':5,'d2r':6,
            'd1l':7,'rr':8,'am':9,'ssg':10}
GEN = {'alg':44,'fb':45,'ams':46,'pms':47,'lfo':48,'lfohz':49,
       'pan':50,'finetune':51}

def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    s = bytearray(TOTAL)
    b = OFF_INSTR
    for op in range(4):
        for k in range(11):
            s[b + op*11 + k] = OP_DEFAUT[op][k]
    s[b+44] = 4; s[b+45] = 4; s[b+52] = 7
    s[b+53] = 0xD; s[b+54] = 8; s[b+57] = 0xFF
    s[b+59] = 0xFF; s[b+61] = 0xFF; s[b+62] = 127

    for arg in sys.argv[2:]:
        nom, val = arg.split('='); val = int(val, 0)
        if nom in GEN:
            s[b + GEN[nom]] = val
        elif nom.startswith('op') and '.' in nom:
            n, champ = nom[2:].split('.')
            s[b + (int(n)-1)*11 + OP_CHAMP[champ]] = val
        else:
            raise SystemExit(f"champ inconnu : {nom}")
    open(sys.argv[1], 'wb').write(bytes(s))

if __name__ == '__main__':
    main()
