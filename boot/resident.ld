ENTRY(boot_entry)
SECTIONS {
  . = 0x40080000;
  .text : { KEEP(*(.text.start)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : ALIGN(16) { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(16); __bss_end = .; }
  end = .;
  ASSERT(end < 0x41000000, "resident overlaps compiler heap")
}
