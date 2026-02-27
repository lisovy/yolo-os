# YOLO-OS — Developer Notes

## Project overview
Simple bare-metal x86 OS (IBM PC). Educational, minimalistic.
Working directory on the development machine: `/tmp/os`

## Architecture decisions
- **CPU mode**: 32-bit protected mode (ring 0 only for now)
- **Boot media**: IDE disk image (`disk.img`) booted via QEMU `-drive if=ide -boot c`
- **Kernel load address**: 0x10000 (loaded by bootloader via INT 13h AH=0x42 LBA read)
- **Stack**: 0x90000 (set by bootloader before jumping to kernel)
- **Executables**: flat binary, single process at a time (no multitasking)
- **No user mode (ring 3) yet** — all code runs in ring 0

## Hardware access
- **Video**: VGA text mode, direct writes to 0xB8000 (80x25, row 24 = status bar)
- **Keyboard**: PS/2 polling via ports 0x60/0x64, scan code set 1, US QWERTY
- **Serial**: COM1 (0x3F8), 38400 baud — mirrors all VGA output for debugging
- **RTC**: IBM PC RTC via ports 0x70/0x71 — used for status bar clock
- **IDE disk**: ATA PIO, primary channel (0x1F0-0x1F7), master drive, LBA28
  - `disk.img` (4 MB raw) mounted as `-drive if=ide` in QEMU
  - persistent boot counter stored in FAT16 file `BOOT.TXT`

## Interrupts
- IDT fully set up (256 entries)
- PIC 8259 remapped: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- All IRQs masked (polling used for keyboard and disk)
- INT 0x80 gate installed (DPL=3) — syscall stub, not yet implemented
- CPU exceptions → kernel panic (red screen + halt)

## disk.img layout
```
Sector 0                  : boot sector (MBR + FAT16 BPB, patched by scripts/patch_boot.sh)
Sectors 1..KERNEL_SECTORS : kernel binary (dd'd by Makefile)
Sectors KERNEL_SECTORS+1..: FAT16 filesystem (FAT tables, root dir, data clusters)
```
`KERNEL_SECTORS` is defined **once** in the Makefile and passed to the NASM bootloader via
`-DKERNEL_SECTORS=N`. Changing it there automatically updates the bootloader, the
`mkfs.fat -R` reserved-sector count, and the build-time size check.
After changing `KERNEL_SECTORS` run `make newdisk` to reformat the FAT16 partition.

## Memory layout
```
0x00000 - 0x00FFF   BIOS / IVT
0x07C00             MBR bootloader (loaded here by BIOS)
0x10000 - ~0x12800  Kernel (entry.o + isr.o + idt.o + kernel.o + fat16.o)  ~10 KB currently
0x90000             Stack top
0xB8000             VGA text mode memory
```

## Source layout
```
boot/boot_ide.asm   16-bit MBR bootloader (NASM, -f bin); LBA INT 13h AH=0x42
kernel/entry.asm    32-bit kernel entry point (_start → kernel_main)
kernel/isr.asm      ISR stubs for INT 0-47 + 128
kernel/idt.c        IDT setup, PIC remapping, idt_init()
kernel/kernel.c     VGA, keyboard, serial, RTC, ATA, status bar, boot counter, kernel_main
kernel/fat16.c      FAT16 R/W driver (fat16_init, fat16_read, fat16_write, fat16_listdir)
kernel/linker.ld    links kernel at 0x10000, entry.o first in .text
scripts/patch_boot.sh  splices boot code with BPB from mkfs.fat into sector 0
Makefile            build + run targets; KERNEL_SECTORS is the single size constant
```

## Build & run
```bash
make              # build disk.img (creates fresh FAT16 image if missing)
make run          # launch QEMU; serial output goes to stdout
make newdisk      # destroy and recreate disk.img (needed after KERNEL_SECTORS change)
make clean        # remove build artifacts (NOT disk.img — it holds persistent data)
```

## QEMU invocation
```bash
qemu-system-i386 \
  -drive file=disk.img,format=raw,if=ide \
  -serial stdio \
  -boot c
```

## Key constants (Makefile / kernel.c)
- `KERNEL_SECTORS = 128` (Makefile) — sectors reserved for kernel; single source of truth
- `TEXT_ROWS = 24` (kernel.c) — rows 0-23 are text, row 24 is status bar
- Syscall int: `int 0x80`, EAX = syscall number (not yet implemented)

## Roadmap (agreed)
- [x] Bootloader (16-bit, IDE, LBA, switches to 32-bit protected mode)
- [x] VGA text mode driver + hardware cursor
- [x] PS/2 keyboard driver (scan code set 1, Shift support)
- [x] COM1 serial debug output
- [x] RTC clock in status bar (blue bar, yellow text, blinking colon)
- [x] ATA PIO driver (read/write sectors)
- [x] IDT (exceptions, PIC remapping, int 0x80 stub)
- [x] FAT16 R/W filesystem on disk.img (persistent boot counter in BOOT.TXT)
- [ ] Syscall interface (int 0x80: write_char, read_char, open, read, write, exit)
- [ ] Program loader (load flat binary from FAT16 into RAM at 0x400000, execute)
- [ ] Shell (ls, run <file>, cls)
- [ ] Text editor (user-space flat binary, uses syscalls)

## User preferences
- All code comments, commit messages and documentation: **English only**
- Communicate with the user in **Czech**
- No extra features beyond what is explicitly requested
- git user: lisovy@gmail.com
