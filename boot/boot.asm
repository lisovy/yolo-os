; boot.asm - MBR bootloader
;
; Nacte kernel z disku (sektory 2+) na fyzickou adresu 0x10000,
; zapne A20, nastavi GDT a prepne do 32-bit protected mode.

[BITS 16]
[ORG 0x7C00]

KERNEL_SEGMENT  equ 0x1000   ; ES pro INT 13h: 0x1000:0x0000 = fyzicky 0x10000
KERNEL_SECTORS  equ 32       ; Pocet sektoru kernelu (max 16 KB)
KERNEL_ADDR     equ 0x10000  ; Fyzicka adresa kernelu v protected mode

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Stack pod bootloaderem
    sti

    mov [boot_drive], dl    ; BIOS nastavi DL na cislo bootovaci mechaniky

    ; ------------------------------------------------------------------
    ; Nacteni kernelu z disku pres BIOS INT 13h
    ; ------------------------------------------------------------------
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor bx, bx              ; ES:BX = 0x1000:0x0000 = 0x10000

    mov ah, 0x02            ; BIOS: cti sektory
    mov al, KERNEL_SECTORS
    mov ch, 0               ; Cylinder 0
    mov cl, 2               ; Prvni sektor kernelu (sektor 2, za MBR)
    mov dh, 0               ; Hlava 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; ------------------------------------------------------------------
    ; Zapnuti A20 linky (rychla metoda pres port 0x92)
    ; ------------------------------------------------------------------
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    ; ------------------------------------------------------------------
    ; Nacteni GDT a prepnuti do protected mode
    ; ------------------------------------------------------------------
    lgdt [gdt_descriptor]

    mov eax, cr0
    or  eax, 0x01
    mov cr0, eax

    ; Far jump: vyprazdni pipeline a nacti CS = selektor kod. segmentu
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
; 32-bit protected mode
; ======================================================================
[BITS 32]
protected_mode_entry:
    mov ax, 0x10            ; Selektor datoveho segmentu
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000        ; Stack na 576 KB

    jmp KERNEL_ADDR         ; Skok na kernel

; ======================================================================
; Global Descriptor Table (GDT)
; ======================================================================
align 8
gdt_null:
    dq 0                    ; Povinny nulovy deskriptor

gdt_code:                   ; Selektor 0x08 — ring 0, kod
    dw 0xFFFF               ; Limit [15:0]
    dw 0x0000               ; Baze [15:0]
    db 0x00                 ; Baze [23:16]
    db 10011010b            ; Access: present, ring0, kod, executable, readable
    db 11001111b            ; Flags: 4KB gran, 32-bit; Limit [19:16]=0xF
    db 0x00                 ; Baze [31:24]

gdt_data:                   ; Selektor 0x10 — ring 0, data
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b            ; Access: present, ring0, data, writable
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_null - 1   ; Velikost GDT - 1
    dd gdt_null                  ; Fyzicka adresa GDT

; Data
boot_drive:      db 0
msg_disk_error:  db "Disk read error!", 0

; Boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
