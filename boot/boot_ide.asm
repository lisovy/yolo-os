; boot_ide.asm - MBR bootloader for IDE disk
;
; Sector 0 layout (standard FAT16 boot sector):
;   Bytes   0-2:   JMP short to bootloader code + NOP
;   Bytes   3-10:  OEM identifier
;   Bytes  11-61:  FAT16 BPB — written by build script from mkfs.fat output
;   Bytes  62-509: Bootloader code (this file)
;   Bytes 510-511: Boot signature 0xAA55
;
; Load strategy: INT 13h AH=0x42 LBA extended read from drive 0x80.
; Reads KERNEL_SECTORS sectors starting at LBA 1 into phys 0x10000.
; No track-boundary arithmetic needed.

[BITS 16]
[ORG 0x7C00]

KERNEL_SEGMENT  equ 0x1000
KERNEL_ADDR     equ 0x10000
; KERNEL_SECTORS is passed via -DKERNEL_SECTORS=N from the Makefile.
; The fallback value here must match Makefile's KERNEL_SECTORS variable.
%ifndef KERNEL_SECTORS
KERNEL_SECTORS  equ 128
%endif

; --- Byte 0: JMP short past the BPB ---
jmp short bootloader_start
nop

; --- Bytes 3-10: OEM ID ---
db "YOLOOS  "

; --- Bytes 11-61: FAT16 BPB placeholder (patched by build) ---
times 51 db 0

; ======================================================================
; Bootloader code — starts at byte 62 of the sector
; ======================================================================
bootloader_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; ------------------------------------------------------------------
    ; LBA extended read (INT 13h AH=0x42)
    ; DL = 0x80 (first hard disk — set by BIOS, but we hardcode for safety)
    ; ------------------------------------------------------------------
    mov si, dap
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc disk_error

    ; ------------------------------------------------------------------
    ; Enable A20 (fast A20 via port 0x92)
    ; ------------------------------------------------------------------
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    ; ------------------------------------------------------------------
    ; Load GDT and switch to 32-bit protected mode
    ; ------------------------------------------------------------------
    lgdt [gdt_descriptor]

    mov eax, cr0
    or  eax, 0x01
    mov cr0, eax

    jmp 0x08:protected_mode_entry

disk_error:
    mov si, msg_err
.loop:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .loop
.halt:
    hlt
    jmp .halt

; ======================================================================
; 32-bit protected mode entry
; ======================================================================
[BITS 32]
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp KERNEL_ADDR

; ======================================================================
; Data (back to BITS 16 for assembler — these are just raw bytes)
; ======================================================================
[BITS 16]

; Global Descriptor Table
align 8
gdt_null:   dq 0
gdt_code:   dw 0xFFFF, 0x0000
            db 0x00, 10011010b, 11001111b, 0x00
gdt_data:   dw 0xFFFF, 0x0000
            db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_null - 1
    dd gdt_null

; Disk Address Packet for INT 13h AH=0x42
dap:
    db 0x10             ; packet size = 16 bytes
    db 0                ; reserved
    dw KERNEL_SECTORS   ; sectors to read
    dw 0x0000           ; buffer offset
    dw KERNEL_SEGMENT   ; buffer segment  (0x1000:0x0000 = phys 0x10000)
    dq 1                ; starting LBA

msg_err: db "Disk error!", 0

; --- Boot signature ---
times 510 - ($ - $$) db 0
dw 0xAA55
