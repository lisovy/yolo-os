; boot.asm - MBR bootloader
;
; Loads the kernel from floppy sectors 2+ to physical address 0x10000,
; enables the A20 line, sets up a GDT and switches to 32-bit protected mode.
;
; Boot media: 1.44 MB floppy image (-fda in QEMU).
; Floppy geometry: 80 cylinders, 2 heads, 18 sectors/track.
; Kernel sits at CHS(0, 0, 2) — the sector immediately after the MBR.

[BITS 16]
[ORG 0x7C00]

KERNEL_SEGMENT  equ 0x1000   ; ES for INT 13h: 0x1000:0x0000 = phys 0x10000
KERNEL_ADDR     equ 0x10000  ; Physical address of kernel in protected mode

; Floppy geometry: 18 sectors/track, 2 heads, 80 cylinders.
; Sector numbering is 1-based; sector 1 of track 0 head 0 is the MBR.
; We split the load across two track reads to stay within track boundaries:
;   Read 1: CHS(0,0,2) — 17 sectors (sectors 2-18 of head 0)
;   Read 2: CHS(0,1,1) — 18 sectors (all of head 1, track 0)
; Total capacity: 35 sectors = 17920 bytes.
KERNEL_SECS_H0  equ 17   ; sectors from track 0, head 0 (starting at sector 2)
KERNEL_SECS_H1  equ 18   ; sectors from track 0, head 1 (starting at sector 1)

start:
    cli                     ; Disable interrupts for the entire boot sequence.
    xor ax, ax              ; Interrupts stay OFF until a proper IDT is set up.
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Stack below bootloader

    ; ------------------------------------------------------------------
    ; Load kernel from floppy via BIOS INT 13h CHS (AH=0x02).
    ; Read 1: track 0, head 0, sectors 2-18 → phys 0x10000
    ; Read 2: track 0, head 1, sectors 1-18 → phys 0x12200
    ; (BIOS reads must not cross track boundaries.)
    ; ------------------------------------------------------------------
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor bx, bx              ; ES:BX = 0x1000:0x0000 = phys 0x10000

    mov ah, 0x02
    mov al, KERNEL_SECS_H0  ; 17 sectors
    mov ch, 0               ; cylinder 0
    mov cl, 2               ; starting at sector 2
    mov dh, 0               ; head 0
    mov dl, 0               ; drive 0 = floppy A
    int 0x13
    jc disk_error

    ; Read 2: head 1 → 0x1000:(17*512) = 0x1000:0x2200 = phys 0x12200
    mov bx, KERNEL_SECS_H0 * 512
    mov ah, 0x02
    mov al, KERNEL_SECS_H1  ; 18 sectors
    mov ch, 0               ; cylinder 0
    mov cl, 1               ; starting at sector 1
    mov dh, 1               ; head 1
    mov dl, 0
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
msg_disk_error:  db "Disk read error!", 0

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
