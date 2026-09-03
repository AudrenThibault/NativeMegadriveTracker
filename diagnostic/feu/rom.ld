OUTPUT_ARCH(m68k)
ENTRY(_debut)
SECTIONS { . = 0; .text : { KEEP(*(.text.boot)) *(.text) } }
