#!/bin/sh
set -e
build() {   # $1 = suffixe, $2 = defsym, $3 = plage SRAM ou -
  m68k-elf-as -m68000 $2 feu.s -o feu_$1.o
  m68k-elf-ld -T rom.ld feu_$1.o -o feu_$1.elf
  m68k-elf-objcopy -O binary feu_$1.elf feu_$1.bin
  python3 entete.py feu_$1.bin "FEU TRICOLORE $1" "GM MDFEU00-00 " 131072 "$3"
}
build sans_sram "" -
build avec_sram "" 200001:27FFFF

carte=""
for v in /Volumes/*/; do [ -d "$v/EDMD" ] && carte="$v" && break; done
if [ -n "$carte" ]; then
  mkdir -p "$carte/Musique"
  cp feu_sans_sram.bin feu_avec_sram.bin "$carte/Musique/"
  sync
  echo "-> copiees sur la carte : $carte/Musique/"
else
  echo "-> carte SD absente, les ROMs restent ici"
fi
