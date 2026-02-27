; entry.asm - Kernel entry point (32-bit protected mode)
;
; The bootloader jumps to physical address 0x10000 where this file begins.
; Calls kernel_main() from kernel.c; halts on return.

[BITS 32]

global _start
extern kernel_main

section .text

_start:
    cli                 ; Ensure interrupts are off (no IDT is set up)
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt
