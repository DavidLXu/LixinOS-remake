ENTRY(_start)
SECTIONS {
  . = 0x44010000;
  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
}
