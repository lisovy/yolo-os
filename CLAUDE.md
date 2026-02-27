# YOLO-OS — Developer Notes

## Project overview
Simple bare-metal x86 OS (IBM PC). Educational, minimalistic.
Working directory on the development machine: `/tmp/os`

## Architecture decisions
- **CPU mode**: 32-bit protected mode, ring 0 only — no user mode, no multitasking
- **Boot media**: IDE disk image (`disk.img`) booted via QEMU `-drive if=ide -boot c`
- **Kernel load address**: 0x10000 (loaded by bootloader via INT 13h AH=0x42 LBA read)
- **Stack**: 0x90000 (set by bootloader before jumping to kernel)
- **Executables**: flat binary, loaded at 0x400000, single process at a time

## Hardware access
- **Video**: VGA text mode, direct writes to 0xB8000 (80x25, row 24 = status bar)
- **Keyboard**: PS/2 polling via ports 0x60/0x64, scan code set 1, US QWERTY
- **Serial**: COM1 (0x3F8), 38400 baud — mirrors all VGA output when built with `-DDEBUG`
- **RTC**: IBM PC RTC via ports 0x70/0x71 — used for status bar clock
- **IDE disk**: ATA PIO, primary channel (0x1F0-0x1F7), master drive, LBA28
  - `disk.img` (4 MB raw) mounted as `-drive if=ide` in QEMU
  - persistent boot counter stored in FAT16 file `BOOT.TXT`

## Interrupts
- IDT fully set up (256 entries)
- PIC 8259 remapped: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- All IRQs masked (polling used for keyboard and disk)
- INT 0x80 gate (DPL=3) — fully implemented syscall interface
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
0x3FF800            ARGS_BASE — args string written here by kernel before exec_run()
0x400000            PROG_BASE — user programs loaded and executed here
```

## Syscall interface (int 0x80)
```
EAX = syscall number, EBX = arg1, ECX = arg2, EDX = arg3, return value in EAX
```
| # | Name  | Args                        | Notes                         |
|---|-------|-----------------------------|-------------------------------|
| 0 | exit  | code                        | unwinds kernel stack via exec_ret_esp |
| 1 | write | fd, buf, len → bytes        | fd 1 = stdout (VGA + serial)  |
| 2 | read  | fd, buf, len → bytes        | fd 0 = stdin (keyboard, line-buffered) |
| 3 | open  | path, flags → fd or -1      | flags: 0=O_RDONLY, 1=O_WRONLY |
| 4 | close | fd → 0 or -1               | O_WRONLY: flushes to FAT16    |

File descriptors: 0=stdin, 1=stdout, 2-5=FAT16 files (max 4 open, 16 KB buffer each)

## Program loader
- `program_exec(name, args)` in kernel.c: appends `.bin`, reads file from FAT16 into 0x400000
- Copies `args` string to `ARGS_BASE` (0x3FF800) before calling `exec_run()`
- `exec_run()` in entry.asm: saves callee-saved regs + ESP to `exec_ret_esp`, calls 0x400000
- `SYS_EXIT`: restores that ESP, pops saved regs, `sti`, `ret` — returns to `program_exec()`

## Shell commands
```
ls              list files on FAT16
run <name>      load and execute <name>.bin from FAT16 (e.g. "run hello", "run xxd BOOT.TXT")
```
Arguments after the program name are passed via ARGS_BASE; read in programs with `get_args()`.

## User programs (bin/)
Built as freestanding 32-bit flat binaries, linked at 0x400000 via `bin/user.ld`.
Include `bin/os.h` for all syscall wrappers (no linking needed — all `static inline`).

| File          | Description                      |
|---------------|----------------------------------|
| bin/os.h      | syscall wrappers + get_args()    |
| bin/user.ld   | linker script (ENTRY=main, base 0x400000) |
| bin/hello.c   | hello world                      |
| bin/xxd.c     | hexdump: `run xxd <file>`        |

FAT16 naming convention: binaries stored as `hello.bin`, `xxd.bin` (lowercase).
`fat83_to_str` always lowercases on display; `str_to_fat83` uppercases for lookup.

## Source layout
```
boot/boot_ide.asm   16-bit MBR bootloader (NASM, -f bin); LBA INT 13h AH=0x42
kernel/entry.asm    32-bit kernel entry (_start → kernel_main); exec_run / exec_ret_esp
kernel/isr.asm      ISR stubs for INT 0-47 + 128
kernel/idt.c        IDT setup, PIC remapping, idt_init()
kernel/kernel.c     VGA, keyboard, serial, RTC, ATA, status bar, syscalls, program loader
kernel/fat16.c      FAT16 R/W driver (fat16_init, fat16_read, fat16_write, fat16_listdir)
kernel/linker.ld    links kernel at 0x10000, entry.o first in .text
bin/os.h            syscall header for user programs
bin/user.ld         user program linker script
bin/hello.c         hello world user program
bin/xxd.c           hexdump utility
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

## Key constants
- `KERNEL_SECTORS = 128` (Makefile) — sectors reserved for kernel; single source of truth
- `TEXT_ROWS = 24` (kernel.c) — rows 0-23 are text, row 24 is status bar
- `PROG_BASE = 0x400000`, `PROG_MAX_SIZE = 256 KB` (kernel.c)
- `ARGS_BASE = 0x3FF800`, `ARGS_MAX = 200` (kernel.c + os.h)
- `FILE_BUF_SIZE = 16384` (kernel.c) — per-fd file buffer

## Roadmap
- [x] Bootloader (16-bit, IDE, LBA, switches to 32-bit protected mode)
- [x] VGA text mode driver + hardware cursor
- [x] PS/2 keyboard driver (scan code set 1, Shift support)
- [x] COM1 serial debug output (mirrors VGA via `#ifdef DEBUG` in vga_putchar)
- [x] RTC clock in status bar (blue bar, yellow text, blinking colon)
- [x] ATA PIO driver (read/write sectors)
- [x] IDT (exceptions, PIC remapping)
- [x] FAT16 R/W filesystem (persistent boot counter in BOOT.TXT)
- [x] Syscall interface (int 0x80: exit, write, read, open, close)
- [x] Program loader (flat binary from FAT16 → 0x400000, args via ARGS_BASE)
- [x] Shell (ls, run <name> [args])
- [x] User programs: hello, xxd
- [ ] Text editor (user-space, uses syscalls)

## User preferences
- All code comments, commit messages and documentation: **English only**
- Communicate with the user in **Czech**
- No extra features beyond what is explicitly requested
- git user: lisovy@gmail.com
- After every commit: run git push. If push fails (permission denied), tell user to run "ssh-add"
