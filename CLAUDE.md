# YOLO-OS — Developer Notes

## Project overview
Simple bare-metal x86 OS (IBM PC). Educational, minimalistic.
Working directory on the development machine: `/tmp/os`

## Architecture decisions
- **CPU mode**: 32-bit protected mode (ring 0 only for now)
- **Boot media**: 1.44 MB floppy image (`os.img`) booted via QEMU `-fda`
- **Kernel load address**: 0x10000 (loaded by bootloader via BIOS INT 13h CHS)
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
  - persistent boot counter in sector 0 (magic 0x4F534479 + uint32 count)

## Interrupts
- IDT fully set up (256 entries)
- PIC 8259 remapped: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- All IRQs masked (polling used for keyboard and disk)
- INT 0x80 gate installed (DPL=3) — syscall stub, not yet implemented
- CPU exceptions → kernel panic (red screen + halt)

## Memory layout
```
0x00000 - 0x00FFF   BIOS / IVT
0x07C00             MBR bootloader (loaded here by BIOS)
0x10000 - ~0x14000  Kernel (entry.o + isr.o + idt.o + kernel.o)
0x90000             Stack top
0xB8000             VGA text mode memory
```

## Source layout
```
boot/boot.asm       16-bit MBR bootloader (NASM, -f bin)
kernel/entry.asm    32-bit kernel entry point (_start → kernel_main)
kernel/isr.asm      ISR stubs for INT 0-47 + 128
kernel/idt.c        IDT setup, PIC remapping, idt_init()
kernel/kernel.c     VGA, keyboard, serial, RTC, ATA, status bar, kernel_main
kernel/linker.ld    links kernel at 0x10000, entry.o first in .text
Makefile            build + run targets
```

## Build & run
```bash
make          # build os.img (and disk.img if missing)
make run      # launch QEMU; serial output goes to stdout
make clean    # remove build artifacts (NOT disk.img)
```

## Roadmap (agreed)
- [x] Bootloader (16-bit, floppy, switches to 32-bit protected mode)
- [x] VGA text mode driver + hardware cursor
- [x] PS/2 keyboard driver (scan code set 1, Shift support)
- [x] COM1 serial debug output
- [x] RTC clock in status bar (blue bar, yellow text, blinking colon)
- [x] ATA PIO driver (read/write sectors, persistent boot counter)
- [x] IDT (exceptions, PIC remapping, int 0x80 stub)
- [ ] FAT16 R/W filesystem on disk.img
- [ ] Syscall interface (int 0x80: write_char, read_char, open, read, write, exit)
- [ ] Program loader (load flat binary from FAT16 into RAM at 0x400000, execute)
- [ ] Shell (ls, run <file>, cls)
- [ ] Text editor (user-space flat binary, uses syscalls)

## QEMU invocation
```bash
qemu-system-i386 \
  -drive file=os.img,format=raw,if=floppy \
  -drive file=disk.img,format=raw,if=ide \
  -serial stdio
```

## Key constants (kernel.c)
- `TEXT_ROWS = 24` — rows 0-23 are text, row 24 is status bar
- `DISK_MAGIC = 0x4F534479` — "OSDy" LE, marks initialized disk sector 0
- Syscall int: `int 0x80`, EAX = syscall number (not yet implemented)

## User preferences
- All code comments, commit messages and documentation: **English only**
- Commit after every logical change; push to origin after every commit
- No extra features beyond what is explicitly requested
- git user: lisovy@gmail.com
