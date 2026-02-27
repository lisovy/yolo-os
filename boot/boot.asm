; boot.asm - MBR bootloader
;
; Loads the kernel from disk sectors 2+ to physical address 0x10000,
; enables the A20 line, sets up a GDT and switches to 32-bit protected mode.

[BITS 16]
[ORG 0x7C00]

KERNEL_SEGMENT  equ 0x1000   ; Segment for INT 13h: 0x1000:0x0000 = phys 0x10000
KERNEL_SECTORS  equ 32       ; Number of kernel sectors to load (max 16 KB)
KERNEL_ADDR     equ 0x10000  ; Physical address of kernel in protected mode

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Stack below bootloader
    sti

    mov [boot_drive], dl    ; BIOS stores boot drive number in DL

    ; ------------------------------------------------------------------
    ; Load kernel from disk via BIOS INT 13h
    ; ------------------------------------------------------------------
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor bx, bx              ; ES:BX = 0x1000:0x0000 = phys 0x10000

    mov ah, 0x02            ; BIOS: read sectors
    mov al, KERNEL_SECTORS
    mov ch, 0               ; Cylinder 0
    mov cl, 2               ; Start at sector 2 (sector 1 is the MBR)
    mov dh, 0               ; Head 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; ------------------------------------------------------------------
    ; Enable A20 line (fast A20 via port 0x92)
    ; ------------------------------------------------------------------
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    ; ------------------------------------------------------------------
    ; Load GDT and switch to protected mode
    ; ------------------------------------------------------------------
    lgdt [gdt_descriptor]

    mov eax, cr0
    or  eax, 0x01
    mov cr0, eax

    ; Far jump: flush pipeline and load CS with the code segment selector
    jmp 0x08:protected_mode_entry

disk_error:
    mov si, msg_disk_error
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
    mov ax, 0x10            ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; Stack at 576 KB

    jmp KERNEL_ADDR

; ======================================================================
; Global Descriptor Table
; ======================================================================
align 8
gdt_null:
    dq 0                    ; Mandatory null descriptor

gdt_code:                   ; Selector 0x08 — ring 0 code segment
    dw 0xFFFF               ; Limit [15:0]
    dw 0x0000               ; Base [15:0]
    db 0x00                 ; Base [23:16]
    db 10011010b            ; Access: present, ring 0, code, executable, readable
    db 11001111b            ; Flags: 4 KB granularity, 32-bit; Limit[19:16]=0xF
    db 0x00                 ; Base [31:24]

gdt_data:                   ; Selector 0x10 — ring 0 data segment
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b            ; Access: present, ring 0, data, writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_null - 1   ; GDT size minus 1
    dd gdt_null                  ; Physical address of GDT

; Data
boot_drive:      db 0
msg_disk_error:  db "Disk read error!", 0

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
