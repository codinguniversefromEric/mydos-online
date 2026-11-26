/* 使用者程式的連結腳本：從 0x400000 開始放（跟核心的 MYDOS 約定好的位址） */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text   : { *(.text._start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
