/*
 * user.ld - linker script for YOLO-OS user programs
 *
 * Programs are flat binaries loaded at PROG_BASE (0x400000).
 * The first byte of .text must be the program entry point.
 */

ENTRY(main)

SECTIONS
{
    . = 0x400000;

    .text   : { *(.text*)   }
    .rodata : { *(.rodata*) }
    .data   : { *(.data)    }
    .bss    : { *(.bss) *(COMMON) }
}
