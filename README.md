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
> ls                    # list files on FAT16
> run hello             # run hello.bin
> run xxd BOOT.TXT      # run xxd.bin with argument "BOOT.TXT"
> run vi notes.txt      # open notes.txt in the text editor
> run demo              # start the graphics demo
```

- Left/right arrow keys move the cursor within the current line
- Up/down arrows are ignored (no history)

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

```c
// from bin/os.h
void  exit(int code);
int   write(int fd, const char *buf, int len);
int   read(int fd, char *buf, int len);
int   open(const char *path, int flags);   // O_RDONLY=0, O_WRONLY=1
int   close(int fd);
int   get_char(void);                      // blocking
int   get_char_nonblock(void);             // returns 0 if no key ready
void  set_pos(int row, int col);
void  clrscr(void);
void  outb(unsigned short port, unsigned char val);  // direct I/O (IOPL=3)
unsigned char inb(unsigned short port);
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
