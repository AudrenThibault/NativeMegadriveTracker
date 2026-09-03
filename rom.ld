OUTPUT_ARCH(m68k)
ENTRY(_debut)
MEMORY {
  rom (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
  ram (rwx) : ORIGIN = 0x00FF0000, LENGTH = 64K
}
SECTIONS {
  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(2);
  } > rom
  .data : {
    __data_debut = .;
    *(.data .data.*)
    . = ALIGN(2);
    __data_fin = .;
  } > ram AT > rom
  __data_rom = LOADADDR(.data);
  .bss  : { __bss_debut = .; *(.bss .bss.* COMMON) . = ALIGN(2); __bss_fin = .; } > ram
}
