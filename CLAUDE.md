# YOLO-OS — Developer Notes

## Project overview
Simple bare-metal x86 OS (IBM PC). Educational, minimalistic.
Working directory on the development machine: `/tmp/os`

## Architecture decisions
- **CPU mode**: 32-bit protected mode, ring 0 (kernel) + ring 3 (user programs)
- **Boot media**: IDE disk image (`disk.img`) booted via QEMU `-drive if=ide -boot c`
- **Kernel load address**: 0x10000 (loaded by bootloader via INT 13h AH=0x42 LBA read)
- **Stack**: 0x90000 (kernel stack, set by bootloader before jumping to kernel)
- **Executables**: flat binary, linked at virtual 0x400000, run in ring 3, single active process at a time
- **Paging**: PMM bitmap allocator (0x100000–0x7FFFFFFF) hands out physical frames per process;
  each process owns a page directory (CR3) with shared `pt_kernel` (PDE[0]) + per-process user PT
  (PDE[1]); PDE[2–511] are 4 MB PSE supervisor-only identity pages so the kernel can write to any
  physical frame; U/S bits enforce ring separation; unlimited nesting depth

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
TSS holds a separate 4 KB kernel stack (`tss_stack[]`); `tss.esp0` is updated inside
`exec_run()` so ring-3 → ring-0 transitions (syscalls) land safely below the saved context.

## Interrupts
- IDT fully set up (256 entries)
- PIC 8259 remapped: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- All IRQs masked (polling used for keyboard and disk)
- INT 0x80 gate (DPL=3) — fully implemented syscall interface
- CPU exceptions → kernel panic: full-screen red/yellow display with register dump + halt
- **#PF from ring 3** → prints "Segmentation fault", exit code 139, returns to shell

## Paging layout
Two static page tables; everything else is allocated dynamically by the PMM.

| Page directory       | PDE[0]                  | PDE[1]                          | PDE[2–511]                  |
|----------------------|-------------------------|---------------------------------|-----------------------------|
| `page_dir` (kernel)  | pt_kernel (4 KB pages)  | 4 MB PSE identity (supervisor)  | 4 MB PSE identity (supervisor) |
| per-process PD       | pt_kernel (shared)      | per-process user PT (ring 3)    | 4 MB PSE identity (supervisor) |

`pt_kernel` maps 0–4 MB identity, supervisor-only (0xA0000–0xBFFFF additionally U=1 for VGA).
PDE[2–511] in every page directory are 4 MB PSE entries covering 8 MB–2 GB; they let the kernel
write to any PMM-allocated physical frame without per-process kernel mappings.

## Physical memory layout
```
0x000000–0x0FFFFF   Kernel code + data + stack + BIOS  (static, mapped by pt_kernel)
0x100000–0x7FFFFFFF  PMM-managed dynamic region (~127 MB, 32 512 frames of 4 KB)
                     per process: 1 PD + 1 PT + 64 binary frames + 7 stack frames
                                  + 1 kstack = 75 frames = ~300 KB
                     maximum simultaneous processes: ~430
```

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
Physical:
0x00000 - 0x00FFF   BIOS / IVT                    (supervisor only)
0x07C00             MBR bootloader
0x10000 - ~0x14000  Kernel (~16 KB)               (supervisor only)
0x90000             Kernel stack top               (supervisor only)
0xA0000             VGA graphics framebuffer (Mode 13h)  (user-accessible)
0xB8000             VGA text mode memory           (user-accessible)
0x100000+           PMM dynamic region  (per-process frames allocated here)

Virtual (per process, all link at 0x400000):
0x000000 - 0x3FFFFF  kernel  (supervisor only, pt_kernel)
0x400000 - 0x43FFFF  binary  (PROG_BASE, 256 KB, ring 3)
0x7FC000             ARGS_BASE — args string
0x7FF000             USER_STACK_TOP — stack top (grows downward)
```

## Syscall interface (int 0x80)
```
EAX = syscall number, EBX = arg1, ECX = arg2, EDX = arg3, return value in EAX
```
| #  | Name              | Args                      | Notes                                     |
|----|-------------------|---------------------------|-------------------------------------------|
|  0 | exit              | code                      | unwinds kernel stack via exec_ret_esp     |
|  1 | write             | fd, buf, len → bytes      | fd 1 = stdout (VGA + serial)              |
|  2 | read              | fd, buf, len → bytes      | fd 0 = stdin (keyboard, line-buffered)    |
|  3 | open              | path, flags → fd or -1    | flags: 0=O_RDONLY, 1=O_WRONLY; path max 127 chars, returns -1 if exceeded |
|  4 | close             | fd → 0 or -1              | O_WRONLY: flushes buffer to FAT16         |
|  5 | getchar           | → char                    | blocking raw keyread, no echo             |
|  6 | setpos            | row, col                  | move VGA hardware cursor                  |
|  7 | clrscr            | —                         | clear text area, cursor to 0,0            |
|  8 | getchar_nonblock  | → char or 0               | non-blocking; 0 = no key ready            |
|  9 | readdir           | buf, max → count          | fills struct direntry array for cwd       |
| 10 | unlink            | name → 0/-1/-2            | -2 = directory not empty                  |
| 11 | mkdir             | name → 0/-1               | create subdirectory in cwd                |
| 12 | rename            | src, dst → 0/-1           | rename within cwd                         |
| 13 | exec              | name, args → exit_code/-1 | load /bin/name, run as child process      |
| 14 | chdir             | name → 0/-1               | change cwd; "/" or ".." supported         |
| 15 | getpos            | → row*256+col             | get current VGA cursor position           |
| 16 | panic             | msg_ptr → (no return)     | full-screen red panic + register dump + hlt |
| 17 | meminfo           | meminfo_ptr → 0           | fills struct meminfo with PMM + virtual stats |

File descriptors: 0=stdin, 1=stdout, 2–5=FAT16 files (max 4 open, 16 KB buffer each)

## Process execution model
- `kernel_main()` calls `pmm_init()`, then `process_create("sh", "")` which allocates PMM
  frames, builds a per-process page directory, and loads `/bin/sh`; sets CR3 to shell's PD,
  then `exec_run(0x400000, 0x7FF000)`.
- `SYS_EXEC`: copies name/args from parent user space, switches CR3 to kernel `page_dir`,
  calls `process_create()` (allocates PMM frames, loads binary, builds page tables), saves
  parent's `exec_ret_esp` in child PCB, switches CR3 to child PD, calls `exec_run()`.
  After child exits: restores `exec_ret_esp` from child PCB, switches back to kernel `page_dir`,
  calls `process_destroy()` (frees all PMM frames), restores parent CR3.
  Supports unlimited nesting depth: each level's `exec_ret_esp` is saved in its child's PCB.
- `exec_run()` in entry.asm: saves callee-saved regs + ESP to `exec_ret_esp`, then calls
  `tss_set_ring0_stack(exec_ret_esp)` so nested syscalls land safely below saved context,
  then IRETes to ring 3.
- `SYS_EXIT` or segfault: restores ESP from `exec_ret_esp`, pops saved regs, returns.

## Shell commands (user-space, in /bin)
```
ls [dir]              list files and dirs in cwd or given dir (dirs shown with trailing /)
<name> [args]         load /bin/<name>, execute with args
rm <name>             delete file or empty dir (prompts y/N)
mkdir <name>          create a subdirectory in cwd
mv <src> <dst>        rename file or dir within cwd (no cross-dir moves)
cd [dir]              change directory; cd .. to go up; cd / or bare cd for root
clear                 clear the screen (clrscr syscall)
exit                  exit the shell (kernel prints "Shell exited. System halted." and halts)
__exit                signal QEMU to exit (automated tests only)
free                  show physical and virtual memory usage in kB (runs /bin/free)
```
Shell prompt shows cwd when not at root: `/subdir> ` (green, color 0x0A).
Shell supports inline editing with left/right arrow keys.

## User programs (bin/)
Built as freestanding 32-bit flat binaries, linked at 0x400000 via `bin/user.ld`.
Include `bin/os.h` for all syscall wrappers (no linking needed — all `static inline`).
**Note:** all `.o` files depend on `bin/os.h` in the Makefile — changing `os.h` triggers recompile.
Stored in `/bin` on FAT16 **without** `.bin` extension.

| File             | /bin name  | Description                                           |
|------------------|------------|-------------------------------------------------------|
| bin/os.h         | —          | syscall wrappers, get_args(), outb/inb, struct direntry, struct meminfo |
| bin/user.ld      | —          | linker script (entry=main, base 0x400000)             |
| bin/sh.c         | sh         | user-space shell (first process)                      |
| bin/hello.c      | hello      | hello world                                           |
| bin/xxd.c        | xxd        | hexdump: `xxd <file>`                                 |
| bin/vi.c         | vi         | vi-like text editor: `vi <file>`                      |
| bin/demo.c       | demo       | VGA Mode 13h snow animation: `demo`; q to quit        |
| bin/segfault.c   | segfault   | writes to kernel address 0x1000 → triggers segfault   |
| bin/ls.c         | ls         | list directory contents                               |
| bin/rm.c         | rm         | remove file or empty directory (prompts y/N)          |
| bin/mkdir.c      | mkdir      | create directory                                      |
| bin/mv.c         | mv         | rename file or directory                              |
| bin/panic.c      | panic      | trigger kernel panic with optional message            |
| bin/free.c       | free       | show physical and virtual memory usage in kB          |

## Source layout
```
boot/boot_ide.asm      16-bit MBR bootloader (NASM, -f bin); LBA INT 13h AH=0x42
kernel/entry.asm       32-bit kernel entry (_start → kernel_main); exec_run (IRET to ring 3)
kernel/isr.asm         ISR stubs for INT 0-47 + 128
kernel/idt.c           GDT + TSS setup (gdt_init), IDT setup, PIC remapping
kernel/kernel.c        paging_init (PSE), PMM include, PCB (struct process, g_procs),
                       process_create/destroy, VGA, keyboard, serial, ATA,
                       syscalls 0-17, panic_screen, kernel_main
kernel/pmm.c           bitmap physical memory manager (pmm_init, pmm_alloc,
                       pmm_alloc_contiguous, pmm_free, pmm_total, pmm_count_used)
kernel/pmm.h           PMM public API
kernel/fat16.c         FAT16 R/W driver (fat16_init, fat16_read, fat16_write, fat16_listdir,
                       fat16_delete, fat16_mkdir, fat16_rename, fat16_chdir, fat16_read_from_bin)
kernel/linker.ld       links kernel at 0x10000, entry.o first in .text
bin/os.h               syscall header (wrappers, get_args(), struct direntry, struct meminfo)
bin/user.ld            user program linker script
bin/sh.c               user-space shell (first process)
bin/hello.c            hello world
bin/xxd.c              hexdump utility
bin/vi.c               vi-like text editor
bin/demo.c             VGA Mode 13h snow + animation demo
bin/segfault.c         deliberate kernel-memory access for segfault testing
bin/ls.c               list directory contents
bin/rm.c               remove file or empty directory
bin/mkdir.c            create directory
bin/mv.c               rename file or directory
bin/panic.c            trigger kernel panic via SYS_PANIC syscall
bin/free.c             physical + virtual memory usage (SYS_MEMINFO)
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

| Test              | What it checks                                                   |
|-------------------|------------------------------------------------------------------|
| boot              | OS boots, welcome message, shell prompt                          |
| unknown_command   | unknown input prints "unknown command"                           |
| hello             | `hello` output contains "Hello"                                  |
| ls                | `ls` lists `bin/`                                                |
| xxd               | `xxd BOOT.TXT` prints hex dump starting "00000000:"              |
| xxd_missing_file  | `xxd NOSUCHFILE.TXT` prints "cannot open"                        |
| vi_quit           | `vi test.txt` + `:q!` returns to shell                           |
| segfault          | `segfault` prints "Segmentation fault", returns to shell         |
| fs_operations     | mkdir / vi (create file) / rm file / cd .. / rm dir             |
| paths             | absolute paths: `xxd /bin/hello`, `cd /bin`, `vi /dir/file`, `xxd`/`rm` via full paths |
| free              | Phys/Virt rows, total=130048 kB (32512×4), kB units, (2 procs)  |
| panic             | `panic` prints `[PANIC]` on serial, system halts (run last)      |

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
- `PMM_BASE = 0x100000`, `PMM_END = 0x8000000` (kernel/pmm.c) — dynamic frame region
- `PMM_TOTAL_FRAMES = 32512` (kernel/pmm.c) — total managed frames; if changed, update `130048` in test_free
- `PROC_MAX_PROCS = 32`, `PROC_MAX_FRAMES = 80` (kernel.c) — PCB table limits
- `MAX_PATH = 127` (kernel.c fd_entry.name[128]) — max open/write path length; `sys_open` returns -1 if exceeded
- `CWD_MAX = 128` (bin/sh.c) — shell prompt path buffer; `cd` prints "cd: path too long" if exceeded

## Roadmap
- [x] Bootloader (16-bit, IDE, LBA, switches to 32-bit protected mode)
- [x] VGA text mode driver + hardware cursor
- [x] PS/2 keyboard driver (scan code set 1, Shift + arrow keys)
- [x] COM1 serial debug output
- [x] RTC driver
- [x] ATA PIO driver (read/write sectors)
- [x] IDT (exceptions, PIC remapping)
- [x] FAT16 R/W filesystem (persistent boot counter in BOOT.TXT)
- [x] Syscall interface (int 0x80: exit, write, read, open, close, getchar, setpos, clrscr,
      getchar_nonblock, readdir, unlink, mkdir, rename, exec, chdir, getpos)
- [x] Per-process page tables via PMM (dynamic allocation, unlimited nesting depth)
- [x] PMM bitmap allocator (kernel/pmm.c) — 0x100000–0x7FFFFFFF, 32 512 frames
- [x] PCB (struct process, g_procs[]) with per-process CR3, frame list, saved exec context
- [x] User-space shell (/bin/sh) as first process; kernel only boots and execs sh
- [x] User-space file tools: ls, rm, mkdir, mv (all in /bin)
- [x] GDT with ring-0/ring-3 segments + TSS
- [x] x86 paging (identity map, U/S protection)
- [x] Ring-3 execution via IRET (IOPL=3, user stack 0x7FF000)
- [x] Segmentation fault detection (#PF from ring 3 → message + return to shell)
- [x] Kernel panic: full-screen red/yellow VGA display with GP + CR register dump
- [x] SYS_PANIC (syscall 16) — user programs can trigger kernel panic with a message
- [x] Shell built-ins: `clear` (clrscr), `exit` (halts system with message)
- [x] Path support in FAT16 (absolute + multi-component paths in open/write/delete/mkdir/chdir)
- [x] Path length limit 127 bytes enforced with user-visible errors
- [x] SYS_MEMINFO (syscall 17) + `free` utility — physical/virtual memory stats in kB
- [x] Automated test suite (12 tests, pexpect + QEMU)

## User preferences
- All code comments, commit messages and documentation: **English only**
- Communicate with the user in **Czech**
- No extra features beyond what is explicitly requested
- git user: lisovy@gmail.com
- After every commit: run git push. If push fails (permission denied), tell user to run "ssh-add"
