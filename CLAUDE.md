# YOLO-OS — Developer Notes

## Project overview
Simple bare-metal x86 OS (IBM PC). Educational, minimalistic.
Working directory on the development machine: `/tmp/os`

## Architecture decisions
- **CPU mode**: 32-bit protected mode, ring 0 (kernel) + ring 3 (user programs)
- **Boot media**: IDE disk image (`disk.img`) booted via QEMU `-drive if=ide -boot c`
- **Kernel load address**: 0x10000 (loaded by bootloader via INT 13h AH=0x42 LBA read)
- **Stack**: 0x90000 (kernel stack, set by bootloader before jumping to kernel)
- **Executables**: flat binary, loaded at 0x400000, run in ring 3, single process at a time
- **Paging**: enabled; identity-mapped, 4 KB pages, U/S bits enforce kernel/user separation

## Hardware access
- **Video**: VGA text mode, direct writes to 0xB8000 (80×25)
- **Keyboard**: PS/2 polling via ports 0x60/0x64, scan code set 1, US QWERTY
- **Serial**: COM1 (0x3F8), 38400 baud — mirrors all VGA output when built with `-DDEBUG`
- **RTC**: IBM PC RTC via ports 0x70/0x71
- **IDE disk**: ATA PIO, primary channel (0x1F0–0x1F7), master drive, LBA28
  - `disk.img` (4 MB raw) mounted as `-drive if=ide` in QEMU
  - persistent boot counter stored in FAT16 file `BOOT.TXT`

## GDT layout
6 descriptors, loaded by `gdt_init()` in `kernel/idt.c`:
```
[0x00] null
[0x08] ring-0 code  DPL=0, 32-bit, flat 4 GB
[0x10] ring-0 data  DPL=0, 32-bit, flat 4 GB
[0x18] ring-3 code  DPL=3, 32-bit, flat 4 GB  → selector 0x1B (0x18|3)
[0x20] ring-3 data  DPL=3, 32-bit, flat 4 GB  → selector 0x23 (0x20|3)
[0x28] TSS          type=0x89, DPL=0           → selector 0x28
```
TSS holds a separate 4 KB kernel stack (`tss_stack[]`); `tss.esp0` is updated before
each `exec_run()` so ring-3 → ring-0 transitions land there.

## Interrupts
- IDT fully set up (256 entries)
- PIC 8259 remapped: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- All IRQs masked (polling used for keyboard and disk)
- INT 0x80 gate (DPL=3) — fully implemented syscall interface
- CPU exceptions → kernel panic (red screen + halt)
- **#PF from ring 3** → prints "Segmentation fault", exit code 139, returns to shell

## Paging layout
4 KB pages, identity-mapped. Two page tables cover 0–8 MB:

| Page table   | Virtual range    | PDE U | PTE U | Purpose                            |
|--------------|-----------------|-------|-------|------------------------------------|
| `pt_kernel`  | 0x000000–0x09FFFF | 1   | 0     | kernel, stack, BIOS — supervisor   |
| `pt_kernel`  | 0x0A0000–0x0BFFFF | 1   | 1     | VGA framebuffers — user-accessible |
| `pt_kernel`  | 0x0C0000–0x3FFFFF | 1   | 0     | BIOS ROM — supervisor              |
| `pt_user`    | 0x400000–0x7FFFFF | 1   | 1     | user program + stack + args        |

PDE[0] has U=1 so the CPU consults individual PTEs for the kernel range.
PDE[1] has U=1 for the user range.

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
0x00000 - 0x00FFF   BIOS / IVT                    (supervisor only)
0x07C00             MBR bootloader
0x10000 - ~0x13800  Kernel (~14 KB)               (supervisor only)
0x90000             Kernel stack top               (supervisor only)
0xA0000             VGA graphics framebuffer (Mode 13h)  (user-accessible)
0xB8000             VGA text mode memory           (user-accessible)
0x400000            PROG_BASE — user program loaded here (max 256 KB)
0x7FC000            ARGS_BASE — args string written by kernel before exec_run()
0x7FF000            User stack top (grows downward)
```

## Syscall interface (int 0x80)
```
EAX = syscall number, EBX = arg1, ECX = arg2, EDX = arg3, return value in EAX
```
| # | Name              | Args                   | Notes                                    |
|---|-------------------|------------------------|------------------------------------------|
| 0 | exit              | code                   | unwinds kernel stack via exec_ret_esp    |
| 1 | write             | fd, buf, len → bytes   | fd 1 = stdout (VGA + serial)             |
| 2 | read              | fd, buf, len → bytes   | fd 0 = stdin (keyboard, line-buffered)   |
| 3 | open              | path, flags → fd or -1 | flags: 0=O_RDONLY, 1=O_WRONLY            |
| 4 | close             | fd → 0 or -1           | O_WRONLY: flushes buffer to FAT16        |
| 5 | getchar           | → char                 | blocking raw keyread, no echo            |
| 6 | setpos            | row, col               | move VGA hardware cursor                 |
| 7 | clrscr            | —                      | clear text area, cursor to 0,0           |
| 8 | getchar_nonblock  | → char or 0            | non-blocking; 0 = no key ready           |

File descriptors: 0=stdin, 1=stdout, 2–5=FAT16 files (max 4 open, 16 KB buffer each)

## Program loader
- `program_exec(name, args)` in kernel.c: appends `.bin`, loads it via `fat16_read_from_root`
  (always from root, regardless of cwd) into 0x400000
- Copies `args` string to `ARGS_BASE` (0x7FC000), sets `tss.esp0` to top of `tss_stack`
- `exec_run(entry, user_stack_top)` in entry.asm: saves callee-saved regs + ESP to
  `exec_ret_esp`, then IRETes to ring 3 (CS=0x1B, SS=0x23, EFLAGS IF=1 IOPL=3)
- `SYS_EXIT` or segfault: restores ESP from `exec_ret_esp`, pops saved regs, rets to
  `program_exec()` as if `exec_run()` returned normally
- After return: detects graphics-mode use via GC Misc register; calls `vga_restore_textmode()`

## Shell commands
```
ls                    list files and dirs in cwd (dirs shown with trailing /)
run <name> [args]     load <name>.bin from root, execute in cwd context
rm <name>             delete file or empty dir (prompts y/N)
mkdir <name>          create a subdirectory in cwd
mv <src> <dst>        rename file or dir within cwd (no cross-dir moves)
cd <dir>              change directory; cd .. to go up; cd / or bare cd for root
```
Arguments after the program name are passed via ARGS_BASE; read in programs with `get_args()`.
Shell prompt shows cwd when not at root: `/subdir> `.
`run` always loads the `.bin` from the root directory; file syscalls (open/read/write) inside
the program use cwd.
Shell supports inline editing with left/right arrow keys.

## User programs (bin/)
Built as freestanding 32-bit flat binaries, linked at 0x400000 via `bin/user.ld`.
Include `bin/os.h` for all syscall wrappers (no linking needed — all `static inline`).
**Note:** all `.o` files depend on `bin/os.h` in the Makefile — changing `os.h` triggers recompile.

| File             | Binary          | Description                                         |
|------------------|-----------------|-----------------------------------------------------|
| bin/os.h         | —               | syscall wrappers, get_args(), outb/inb              |
| bin/user.ld      | —               | linker script (entry=main, base 0x400000)           |
| bin/hello.c      | hello.bin       | hello world                                         |
| bin/xxd.c        | xxd.bin         | hexdump: `run xxd <file>`                           |
| bin/vi.c         | vi.bin          | vi-like text editor: `run vi <file>`                |
| bin/demo.c       | demo.bin        | VGA Mode 13h snow animation: `run demo`; q to quit  |
| bin/segfault.c   | segfault.bin    | writes to kernel address 0x1000 → triggers segfault |

FAT16 naming: binaries stored as lowercase `hello.bin`, `xxd.bin`, etc.

## Source layout
```
boot/boot_ide.asm      16-bit MBR bootloader (NASM, -f bin); LBA INT 13h AH=0x42
kernel/entry.asm       32-bit kernel entry (_start → kernel_main); exec_run (IRET to ring 3)
kernel/isr.asm         ISR stubs for INT 0-47 + 128
kernel/idt.c           GDT + TSS setup (gdt_init), IDT setup, PIC remapping
kernel/kernel.c        paging_init, VGA, keyboard, serial, ATA, syscalls, program loader, shell
kernel/fat16.c         FAT16 R/W driver (fat16_init, fat16_read, fat16_write, fat16_listdir,
                       fat16_delete, fat16_mkdir, fat16_rename, fat16_chdir; cwd support)
kernel/linker.ld       links kernel at 0x10000, entry.o first in .text
bin/os.h               syscall header for user programs
bin/user.ld            user program linker script
bin/hello.c            hello world
bin/xxd.c              hexdump utility
bin/vi.c               vi-like text editor
bin/demo.c             VGA Mode 13h snow + animation demo
bin/segfault.c         deliberate kernel-memory access for segfault testing
scripts/patch_boot.sh  splices boot code with BPB from mkfs.fat into sector 0
Makefile               build + run targets; KERNEL_SECTORS is the single size constant
tests/run_tests.py     automated test suite (pexpect + QEMU)
```

## Build & run
```bash
make              # build disk.img (creates fresh FAT16 image if missing)
make run          # launch QEMU; serial output goes to stdout
make test         # run automated test suite (requires python3-pexpect)
make newdisk      # destroy and recreate disk.img (needed after KERNEL_SECTORS change)
make clean        # remove build artifacts (NOT disk.img — it holds persistent data)
```

## Automated tests
`tests/run_tests.py` spawns QEMU with `-serial stdio` and `-device isa-debug-exit`,
sends commands over the serial port, and checks output with pexpect.

| Test              | What it checks                                          |
|-------------------|---------------------------------------------------------|
| boot              | OS boots, welcome message, shell prompt                 |
| unknown_command   | unknown input prints "unknown command"                  |
| hello             | `run hello` output contains "Hello"                     |
| ls                | `ls` lists all expected `.bin` files                    |
| xxd               | `run xxd BOOT.TXT` prints hex dump starting "00000000:" |
| xxd_missing_file  | `run xxd NOSUCHFILE.TXT` prints "cannot open"           |
| vi_quit           | `run vi test.txt` + `:q!` returns to shell              |
| segfault          | `run segfault` prints "Segmentation fault", returns to shell |

## QEMU invocation
```bash
qemu-system-i386 \
  -drive file=disk.img,format=raw,if=ide \
  -serial stdio \
  -boot c
```

## Key constants
- `KERNEL_SECTORS = 128` (Makefile) — sectors reserved for kernel; single source of truth
- `VGA_ROWS = 25`, `VGA_COLS = 80` (kernel.c)
- `PROG_BASE = 0x400000`, `PROG_MAX_SIZE = 256 KB` (kernel.c)
- `ARGS_BASE = 0x7FC000`, `ARGS_MAX = 200` (kernel.c + os.h)
- `USER_STACK_TOP = 0x7FF000` (kernel.c)
- `FILE_BUF_SIZE = 16384` (kernel.c) — per-fd file buffer

## Roadmap
- [x] Bootloader (16-bit, IDE, LBA, switches to 32-bit protected mode)
- [x] VGA text mode driver + hardware cursor
- [x] PS/2 keyboard driver (scan code set 1, Shift + arrow keys)
- [x] COM1 serial debug output
- [x] RTC driver
- [x] ATA PIO driver (read/write sectors)
- [x] IDT (exceptions, PIC remapping)
- [x] FAT16 R/W filesystem (persistent boot counter in BOOT.TXT)
- [x] Syscall interface (int 0x80: exit, write, read, open, close, getchar, setpos, clrscr, getchar_nonblock)
- [x] Program loader (flat binary from FAT16 → 0x400000, args via ARGS_BASE)
- [x] Shell (ls, run [args], inline editing with arrow keys)
- [x] User programs: hello, xxd, vi, demo, segfault
- [x] VGA Mode 13h support in user programs (kernel auto-detects and restores text mode)
- [x] GDT with ring-0/ring-3 segments + TSS
- [x] x86 paging (identity map, U/S protection)
- [x] Ring-3 execution via IRET (IOPL=3, user stack 0x7FF000)
- [x] Segmentation fault detection (#PF from ring 3 → message + return to shell)
- [x] Automated test suite (8 tests, pexpect + QEMU)

## User preferences
- All code comments, commit messages and documentation: **English only**
- Communicate with the user in **Czech**
- No extra features beyond what is explicitly requested
- git user: lisovy@gmail.com
- After every commit: run git push. If push fails (permission denied), tell user to run "ssh-add"
