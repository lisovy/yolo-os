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
; exec_run(uint32_t entry, uint32_t user_stack_top) — ring-3 launcher
;
; Saves callee-saved registers and current ESP into exec_ret_esp, then
; uses IRET to transfer control to ring 3 at the given entry point with
; the given user stack top.
;
; On SYS_EXIT or segfault the handler restores ESP from exec_ret_esp and
; jumps to exec_run_return — unwinding back to the C caller of exec_run().
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
    mov     [exec_ret_esp], esp     ; save kernel ESP

    ; Arguments on stack (adjusted for 4 pushes above):
    ;   [esp+20] = entry  (first arg)
    ;   [esp+24] = user_stack_top (second arg)
    mov     eax, [esp+20]   ; entry point
    mov     ecx, [esp+24]   ; user stack top

    ; Switch data segments to ring-3 selector before IRET
    mov     dx, 0x23        ; ring-3 data selector (0x20 | RPL=3)
    mov     ds, dx
    mov     es, dx
    mov     fs, dx
    mov     gs, dx

    ; Build ring-3 IRET frame: SS, ESP, EFLAGS, CS, EIP
    push    0x23            ; SS  (ring-3 data)
    push    ecx             ; ESP (user stack top)
    push    0x3200          ; EFLAGS: IF=1, IOPL=3
    push    0x1B            ; CS  (ring-3 code, 0x18 | RPL=3)
    push    eax             ; EIP (entry point)
    iret

; Return path used by SYS_EXIT and segfault handler:
; restore kernel data segments, then pop callee-saved regs and return.
exec_run_return:
    mov     dx, 0x10        ; ring-0 data selector
    mov     ds, dx
    mov     es, dx
    mov     fs, dx
    mov     gs, dx
    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret
