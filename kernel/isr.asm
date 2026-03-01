; isr.asm - ISR stubs for CPU exceptions, IRQs and syscall (int 0x80)
;
; Two macro variants:
;   ISR_NOERR n  — exception that does NOT push an error code (we push 0)
;   ISR_ERR   n  — exception that DOES push an error code (CPU already did it)
;
; After the stub the stack looks like:
;   [ESP]    int_no
;   [ESP+4]  err_code  (0 for NOERR variants)
;   [ESP+8]  EIP       (pushed by CPU)
;   [ESP+12] CS
;   [ESP+16] EFLAGS

[BITS 32]

extern isr_handler

; --------------------------------------------------------------------------
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; dummy error code
    push dword %1       ; interrupt number
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1       ; interrupt number (error code already on stack)
    jmp isr_common
%endmacro

; --------------------------------------------------------------------------
; CPU exceptions (0-31)
ISR_NOERR  0   ; #DE  division by zero
ISR_NOERR  1   ; #DB  debug
ISR_NOERR  2   ;      NMI
ISR_NOERR  3   ; #BP  breakpoint
ISR_NOERR  4   ; #OF  overflow
ISR_NOERR  5   ; #BR  bound range exceeded
ISR_NOERR  6   ; #UD  invalid opcode
ISR_NOERR  7   ; #NM  device not available
ISR_ERR    8   ; #DF  double fault          (error code = 0)
ISR_NOERR  9   ;      coprocessor overrun
ISR_ERR   10   ; #TS  invalid TSS
ISR_ERR   11   ; #NP  segment not present
ISR_ERR   12   ; #SS  stack fault
ISR_ERR   13   ; #GP  general protection fault
ISR_ERR   14   ; #PF  page fault
ISR_NOERR 15   ;      reserved
ISR_NOERR 16   ; #MF  x87 FPU error
ISR_ERR   17   ; #AC  alignment check
ISR_NOERR 18   ; #MC  machine check
ISR_NOERR 19   ; #XF  SIMD FP exception
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; IRQs remapped by PIC to INT 32-47
ISR_NOERR 32   ; IRQ0  system timer
ISR_NOERR 33   ; IRQ1  PS/2 keyboard
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40   ; IRQ8  RTC
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46   ; IRQ14 primary ATA
ISR_NOERR 47   ; IRQ15 secondary ATA

; Syscall
ISR_NOERR 128  ; int 0x80

; --------------------------------------------------------------------------
; Common stub: saves full register state and calls the C handler.
;
; Stack layout when isr_handler is called (ESP points to struct registers):
;   [ESP+0 ]  GS
;   [ESP+4 ]  FS
;   [ESP+8 ]  ES
;   [ESP+12]  DS
;   [ESP+16]  EDI  \
;   [ESP+20]  ESI   |
;   [ESP+24]  EBP   | saved by pusha
;   [ESP+28]  ESP   |
;   [ESP+32]  EBX   |
;   [ESP+36]  EDX   |
;   [ESP+40]  ECX   |
;   [ESP+44]  EAX  /
;   [ESP+48]  int_no
;   [ESP+52]  err_code
;   [ESP+56]  EIP    \
;   [ESP+60]  CS      | pushed by CPU
;   [ESP+64]  EFLAGS /

section .text
isr_common:
    pusha               ; save EAX..EDI
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10        ; reload kernel data segment selectors
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp            ; argument: pointer to saved register state
    call isr_handler
    add esp, 4          ; remove argument; return value (new ESP or 0) is in EAX

    ; If isr_handler returned a non-zero value, switch to that process's
    ; kernel stack (preemptive context switch).
    test eax, eax
    jz   .no_switch
    mov  esp, eax       ; switch to new process's saved kernel stack frame
.no_switch:

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8          ; discard int_no and err_code
    iret
