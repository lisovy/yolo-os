/*
 * linker.ld - Kernel linker script
 *
 * The kernel is linked at virtual address 0x10000, where the bootloader
 * loads it. entry.o must come first in .text so that _start lands exactly
 * at 0x10000.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *entry.o(.text)     /* _start must be the very first instruction */
        *(.text)
    }

    .rodata : { *(.rodata*) }

    .data : { *(.data) }

    .bss : {
        *(.bss)
        *(COMMON)
    }
}
