# YOLO-OS

A minimal bare-metal x86 operating system for IBM PC, built from scratch.
Runs in QEMU. Educational and intentionally simple.

## Quick start

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install nasm gcc gcc-multilib binutils qemu-system-x86 dosfstools mtools python3-pexpect

# Build and run
make
make run

# Run automated tests
make test
```

---

## Architecture

- **CPU**: 32-bit protected mode; kernel in ring 0, user programs in ring 3
- **Paging**: enabled, identity-mapped; U/S bits enforce kernel/user separation; segfaults caught
- **Boot**: 16-bit MBR bootloader → ATA PIO LBA read → jumps to 32-bit kernel at `0x10000`
- **Video**: VGA text mode 80×25 (`0xB8000`); user programs may switch to Mode 13h graphics
- **Keyboard**: PS/2 polling, scan code set 1, US QWERTY, arrow keys supported
- **Filesystem**: FAT16 on the same IDE disk image, read/write via ATA PIO
- **Syscalls**: `int 0x80` (EAX=number, EBX/ECX/EDX=args, return in EAX)
- **Programs**: flat 32-bit binaries loaded from FAT16 into RAM at `0x400000`, run in ring 3

---

## Disk layout

![disk layout](docs/disk-layout.png)

The single `disk.img` (4 MB raw) holds both the kernel and the filesystem:

| Region | Content |
|--------|---------|
| Sector 0 | Boot sector — MBR code + FAT16 BPB (patched by `scripts/patch_boot.sh`) |
| Sectors 1 – 128 | Kernel binary (controlled by `KERNEL_SECTORS` in Makefile) |
| Sector 129+ | FAT16 filesystem — FAT tables, root directory, data clusters |

User programs and persistent data (`BOOT.TXT`) live in the FAT16 partition.

---

## Memory layout

![memory layout](docs/memory-layout.png)

| Address | Content | Ring |
|---------|---------|------|
| `0x00000` | IVT / BIOS data area | 0 only |
| `0x07C00` | MBR bootloader (512 B) | 0 only |
| `0x10000` | Kernel (~14 KB) | 0 only |
| `0x90000` | Kernel stack top (grows down) | 0 only |
| `0xA0000` | VGA graphics framebuffer (Mode 13h, 320×200) | 0 + 3 |
| `0xB8000` | VGA text framebuffer (80×25) | 0 + 3 |
| `0x400000` | `PROG_BASE` — user program loaded here (max 256 KB) | 3 |
| `0x7FC000` | `ARGS_BASE` — argument string passed to user programs | 3 |
| `0x7FF000` | User stack top (grows down) | 3 |

---

## Shell

After boot you get a simple interactive shell:

```
> ls                    # list files and dirs (dirs shown with trailing /)
> run hello             # run hello.bin
> run xxd BOOT.TXT      # run xxd.bin with argument "BOOT.TXT"
> run vi notes.txt      # open notes.txt in the text editor
> run demo              # start the graphics demo
> mkdir docs            # create a subdirectory
> cd docs               # enter it (prompt changes to /docs> )
> cd ..                 # go up to parent
> rm hello.bin          # delete file (prompts y/N)
> mv foo.txt bar.txt    # rename within current directory
```

- Left/right arrow keys move the cursor within the current line
- Up/down arrows are ignored (no history)
- Prompt shows current directory when not at root: `/docs> `

---

## User programs

All programs are freestanding 32-bit flat binaries linked at `0x400000`, run in ring 3.
Include `bin/os.h` to get syscall wrappers — no libc, no linking required.

### hello

Prints "Hello, World!" and exits.

### xxd

Hex dump of a file (16 bytes per line, ASCII sidebar).

```
> run xxd BOOT.TXT
> run xxd hello.bin
```

### vi

A vi-like text editor.

| Key | Action |
|-----|--------|
| Arrow keys | Move cursor |
| `i` | Enter insert mode |
| `o` | Open new line below, enter insert mode |
| `x` | Delete character under cursor |
| `Esc` | Return to normal mode |
| `:w` | Save |
| `:q` | Quit (refuses if unsaved) |
| `:q!` | Force quit |
| `:wq` / `:x` | Save and quit |

### demo

VGA Mode 13h (320×200, 256 colours) snow animation.
Press `q` to quit. The kernel automatically restores text mode on exit.

### segfault

Deliberately writes to a kernel-only address (`0x1000`) to trigger a page fault.
The kernel prints "Segmentation fault" and returns to the shell.
Useful for verifying that ring-3 memory protection works.

---

## Adding a new user program

1. Create `bin/myprog.c`, include `bin/os.h`, implement `void main(void)`.
2. Add to `Makefile`:
   ```makefile
   USER_BINS += $(BUILD)/myprog.bin

   $(BUILD)/myprog.o: bin/myprog.c bin/os.h | $(BUILD)
       $(CC) $(CFLAGS) -c $< -o $@

   $(BUILD)/myprog.elf: $(BUILD)/myprog.o bin/user.ld
       $(LD) -m elf_i386 -T bin/user.ld $< -o $@

   $(BUILD)/myprog.bin: $(BUILD)/myprog.elf
       $(OBJCPY) -O binary $< $@
   ```
3. `make` and `run myprog`.

Use `get_args()` to read the argument string passed after the program name.

---

## Syscall reference

All syscalls use `int 0x80`: `EAX` = syscall number, `EBX`/`ECX`/`EDX` = args, return value in `EAX`.
Include `bin/os.h` for the C wrappers below.

| Function | Description |
|----------|-------------|
| `exit(code)` | Terminate the program. `code` is printed by the shell as "exited N". |
| `write(fd, buf, len)` → bytes | Write `len` bytes from `buf` to `fd`. `fd=1` = stdout (VGA + serial). For open files (`fd≥2`) data is buffered until `close()`. Returns bytes written or -1. |
| `read(fd, buf, len)` → bytes | Read up to `len` bytes into `buf`. `fd=0` = stdin (keyboard, line-buffered, echoed). For open files reads from current position. Returns bytes read or -1. |
| `open(path, flags)` → fd | Open a FAT16 file in the cwd. `flags`: `O_RDONLY=0` reads file into memory; `O_WRONLY=1` creates/truncates for writing. Returns fd (≥2) or -1. |
| `close(fd)` → 0/-1 | Close fd. For `O_WRONLY` files flushes the buffer to disk. |
| `get_char()` → char | Blocking read of one raw keystroke (no echo). Arrow keys return `KEY_UP/DOWN/LEFT/RIGHT` (0x80–0x83). |
| `get_char_nonblock()` → char/0 | Non-blocking version of `get_char`; returns 0 immediately if no key is ready. |
| `set_pos(row, col)` | Move the VGA hardware cursor to `row` (0–24), `col` (0–79). |
| `clrscr()` | Clear the entire text screen and move cursor to (0, 0). |
| `outb(port, val)` | Write byte `val` to I/O port `port`. Available because IOPL=3 in ring 3. |
| `inb(port)` → byte | Read one byte from I/O port `port`. |

File descriptors: `0` = stdin, `1` = stdout, `2`–`5` = FAT16 files (max 4 open, 16 KB buffer each).

```c
// Quick usage example (from bin/os.h)
int fd = open("data.txt", O_RDONLY);
char buf[64];
int n = read(fd, buf, sizeof(buf));
close(fd);

fd = open("out.txt", O_WRONLY);
write(fd, buf, n);
close(fd);   // flushes to disk
exit(0);
```

---

## Automated tests

`make test` spawns QEMU headlessly and drives it via the serial port using pexpect.

| Test | What it checks |
|------|----------------|
| boot | OS boots, welcome message, shell prompt |
| unknown_command | unknown input prints "unknown command" |
| hello | `run hello` output contains "Hello" |
| ls | `ls` lists all expected `.bin` files |
| xxd | `run xxd BOOT.TXT` prints a hex dump |
| xxd_missing_file | `run xxd NOSUCHFILE.TXT` prints "cannot open" |
| vi_quit | `run vi` + `:q!` returns to shell |
| segfault | `run segfault` prints "Segmentation fault" and returns to shell |

---

## Build targets

| Target | Description |
|--------|-------------|
| `make` | Build everything; create `disk.img` if missing |
| `make run` | Build and launch QEMU (serial output on stdout) |
| `make test` | Run automated test suite (requires `python3-pexpect`) |
| `make newdisk` | Wipe and recreate `disk.img` (needed after changing `KERNEL_SECTORS`) |
| `make clean` | Remove `build/` (keeps `disk.img`) |
