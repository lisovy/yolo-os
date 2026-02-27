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

; ======================================================================
; exec_run() — load trampoline for user programs
;
; Saves callee-saved registers and current ESP into exec_ret_esp, then
; calls the program at PROG_BASE (0x400000).
;
; On SYS_EXIT the syscall handler restores ESP from exec_ret_esp, pops
; the saved registers, and rets — unwinding back to the C caller of
; exec_run() as if the call returned normally.
; ======================================================================
global exec_run
global exec_ret_esp

section .bss
exec_ret_esp resd 1

section .text

exec_run:
    push    ebp
    push    ebx
    push    esi
    push    edi
    mov     [exec_ret_esp], esp     ; save ESP (callee regs + retaddr above)
    call    0x400000                ; run the program
    pop     edi                     ; normal return path (program did `ret`)
    pop     esi
    pop     ebx
    pop     ebp
    ret
