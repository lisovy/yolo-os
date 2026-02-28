/*
 * kernel.c - Simple bare-metal kernel for IBM PC x86
 *
 * Video:     VGA text mode — direct writes to memory at 0xB8000.
 *            BIOS interrupts are unavailable in 32-bit protected mode.
 * Serial:    COM1 (0x3F8) mirrors all output; run QEMU with -serial stdio
 *            to read it directly in the terminal.
 * Keyboard:  PS/2 polling via I/O ports 0x60 / 0x64.
 *            Scan code set 1, US QWERTY layout.
 * RTC:       IBM PC Real Time Clock via ports 0x70/0x71.
 */

/* VGA text mode */
#define VGA_MEMORY  0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25
#define TEXT_ROWS   24      /* rows 0-23 are text area; row 24 is the status bar */
#define STATUS_ROW  24

/* Attribute byte: high nibble = background, low nibble = foreground color */
#define COLOR_DEFAULT      0x07   /* light gray on black  */
#define COLOR_HELLO        0x0F   /* bright white on black */
#define COLOR_PROMPT       0x0A   /* light green on black  */
#define COLOR_STATUS_BG    0x17   /* white on blue  — status bar fill  */
#define COLOR_STATUS_TIME  0x1E   /* yellow on blue — date/time text   */

/* PS/2 keyboard I/O ports */
#define KBD_DATA    0x60
#define KBD_STATUS  0x64

/* COM1 serial port */
#define COM1        0x3F8

/* Error color for panic screen */
#define COLOR_ERR  0x4F   /* white on red */

/* Arrow key codes returned by kbd_getchar() / SYS_GETCHAR */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83


/* ============================================================
 * I/O port helpers
 * ============================================================ */

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(unsigned short port, unsigned short val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* ============================================================
 * COM1 serial driver (16550 UART)
 * ============================================================ */

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);  /* disable interrupts           */
    outb(COM1 + 3, 0x80);  /* enable DLAB (baud rate mode) */
    outb(COM1 + 0, 0x03);  /* baud divisor lo: 38400 baud  */
    outb(COM1 + 1, 0x00);  /* baud divisor hi              */
    outb(COM1 + 3, 0x03);  /* 8 bits, no parity, 1 stop    */
    outb(COM1 + 2, 0xC7);  /* enable FIFO, clear, 14-byte threshold */
}

static void serial_putchar(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    if (c == '\n')
        serial_putchar('\r');
    outb(COM1, (unsigned char)c);
}

static void serial_print(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}

/* ============================================================
 * VGA text mode driver
 * ============================================================ */

static int cursor_col = 0;
static int cursor_row = 0;

#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5

static void vga_update_hw_cursor(void)
{
    unsigned short pos = (unsigned short)(cursor_row * VGA_COLS + cursor_col);
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA,  (unsigned char)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA,  (unsigned char)(pos >> 8));
}

static void vga_clear(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;
    unsigned short blank = (COLOR_DEFAULT << 8) | ' ';

    /* Clear entire screen (all VGA_ROWS rows) */
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = blank;

    cursor_col = 0;
    cursor_row = 0;
    vga_update_hw_cursor();
}

static void vga_scroll(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

    /* Shift every row one line up across the full screen */
    for (int row = 0; row < VGA_ROWS - 1; row++)
        for (int col = 0; col < VGA_COLS; col++)
            vga[row * VGA_COLS + col] = vga[(row + 1) * VGA_COLS + col];

    /* Clear the last row */
    unsigned short blank = (COLOR_DEFAULT << 8) | ' ';
    for (int col = 0; col < VGA_COLS; col++)
        vga[(VGA_ROWS - 1) * VGA_COLS + col] = blank;

    cursor_row = VGA_ROWS - 1;
}

static void vga_putchar(char c, unsigned char color)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

#ifdef DEBUG
    serial_putchar(c);
#endif

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_COLS - 1;
        }
        vga[cursor_row * VGA_COLS + cursor_col] = (COLOR_DEFAULT << 8) | ' ';
    } else {
        vga[cursor_row * VGA_COLS + cursor_col] =
            ((unsigned short)color << 8) | (unsigned char)c;
        cursor_col++;
        if (cursor_col >= VGA_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_ROWS)
        vga_scroll();
    vga_update_hw_cursor();
}

static void vga_print(const char *s, unsigned char color)
{
    while (*s)
        vga_putchar(*s++, color);
}

/* ============================================================
 * VGA mode save / restore
 * Saves the text-mode register state and font at startup so the
 * kernel can recover after a graphics-mode user program exits.
 * ============================================================ */

static struct {
    unsigned char misc;
    unsigned char seq[5];
    unsigned char crtc[25];
    unsigned char gc[9];
    unsigned char ac[21];
} saved_text_regs;

static unsigned char saved_font[4096];  /* 256 chars × 16 bytes */

/* Save every VGA register (call once while in text mode). */
static void vga_save_state(void)
{
    int i;
    saved_text_regs.misc = inb(0x3CC);
    for (i = 0; i < 5; i++)  { outb(0x3C4, (unsigned char)i); saved_text_regs.seq[i]  = inb(0x3C5); }
    for (i = 0; i < 25; i++) { outb(0x3D4, (unsigned char)i); saved_text_regs.crtc[i] = inb(0x3D5); }
    for (i = 0; i < 9; i++)  { outb(0x3CE, (unsigned char)i); saved_text_regs.gc[i]   = inb(0x3CF); }
    for (i = 0; i < 21; i++) {
        inb(0x3DA);                              /* reset AC flip-flop */
        outb(0x3C0, (unsigned char)i);           /* write index (PAS=0) */
        saved_text_regs.ac[i] = inb(0x3C1);     /* read data via 0x3C1 — flip-flop stays in data mode */
    }
    inb(0x3DA);         /* reset flip-flop back to index mode */
    outb(0x3C0, 0x20);  /* write index 0x20 (PAS=1) to re-enable display */
}

/* Save the VGA character font from plane 2 (call once while in text mode). */
static void vga_save_font(void)
{
    unsigned char old_seq4, old_gc4, old_gc5, old_gc6;
    outb(0x3C4, 0x04); old_seq4 = inb(0x3C5);
    outb(0x3CE, 0x04); old_gc4  = inb(0x3CF);
    outb(0x3CE, 0x05); old_gc5  = inb(0x3CF);
    outb(0x3CE, 0x06); old_gc6  = inb(0x3CF);

    /* Reconfigure to read plane 2 linearly at A000h */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);  /* seq: sequential, no chain4 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);  /* read map: plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);  /* GC mode: read mode 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);  /* GC misc: A000h 64 KB */

    volatile unsigned char *fb = (volatile unsigned char *)0xA0000;
    for (int i = 0; i < 4096; i++)
        saved_font[i] = fb[i];

    outb(0x3C4, 0x04); outb(0x3C5, old_seq4);
    outb(0x3CE, 0x04); outb(0x3CF, old_gc4);
    outb(0x3CE, 0x05); outb(0x3CF, old_gc5);
    outb(0x3CE, 0x06); outb(0x3CF, old_gc6);
}

/* Restore the saved VGA register state. */
static void vga_restore_state(void)
{
    int i;
    outb(0x3C2, saved_text_regs.misc);

    /* Sequencer: assert synchronous reset, restore, then deassert */
    outb(0x3C4, 0x00); outb(0x3C5, 0x01);
    for (i = 1; i < 5; i++) { outb(0x3C4, (unsigned char)i); outb(0x3C5, saved_text_regs.seq[i]); }
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);

    /* CRTC: unlock write-protected registers first */
    outb(0x3D4, 0x11); outb(0x3D5, saved_text_regs.crtc[0x11] & 0x7F);
    for (i = 0; i < 25; i++) { outb(0x3D4, (unsigned char)i); outb(0x3D5, saved_text_regs.crtc[i]); }

    for (i = 0; i < 9; i++)  { outb(0x3CE, (unsigned char)i); outb(0x3CF, saved_text_regs.gc[i]); }

    inb(0x3DA);  /* reset AC flip-flop */
    for (i = 0; i < 21; i++) { outb(0x3C0, (unsigned char)i); outb(0x3C0, saved_text_regs.ac[i]); }
    outb(0x3C0, 0x20);  /* re-enable video */
}

/* Restore the font to VGA plane 2 (call after vga_restore_state). */
static void vga_restore_font(void)
{
    /* Write to plane 2 only, sequential addressing at A000h */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);  /* map mask: plane 2 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);  /* mem mode: sequential */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);  /* GC mode: write mode 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);  /* GC misc: A000h 64 KB */

    volatile unsigned char *fb = (volatile unsigned char *)0xA0000;
    for (int i = 0; i < 4096; i++)
        fb[i] = saved_font[i];

    /* Restore exact text-mode values for the modified registers */
    outb(0x3C4, 0x02); outb(0x3C5, saved_text_regs.seq[2]);
    outb(0x3C4, 0x04); outb(0x3C5, saved_text_regs.seq[4]);
    outb(0x3CE, 0x05); outb(0x3CF, saved_text_regs.gc[5]);
    outb(0x3CE, 0x06); outb(0x3CF, saved_text_regs.gc[6]);
}

/* Full text-mode recovery — called by program_exec() after every user program.
 * Restores VGA registers and font without clearing the framebuffer so that
 * the output of text-mode programs remains visible after they exit. */
static void vga_restore_textmode(void)
{
    vga_restore_state();
    vga_restore_font();
}

/* ============================================================
 * PS/2 keyboard driver — scan code set 1, US QWERTY
 * ============================================================ */

#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36

static const char scancode_map[] = {
    /* 0x00 */ 0,    '\x1b','1',  '2',  '3',  '4',  '5',  '6',
    /* 0x08 */ '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    /* 0x18 */ 'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    /* 0x28 */ '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    /* 0x38 */ 0,    ' ',
};

static const char scancode_map_shift[] = {
    /* 0x00 */ 0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
    /* 0x08 */ '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    /* 0x18 */ 'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    /* 0x28 */ '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    /* 0x30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    /* 0x38 */ 0,    ' ',
};

#define SCANCODE_MAP_SIZE  ((int)(sizeof(scancode_map) / sizeof(scancode_map[0])))

static int shift_pressed = 0;
static int e0_seen = 0;   /* set when 0xE0 prefix byte is received */

/*
 * Non-blocking: returns 0 immediately if no key is ready.
 * Tracks Shift state; handles 0xE0-prefixed extended keys (arrows).
 * Returns KEY_UP/DOWN/LEFT/RIGHT for arrow keys, ASCII for others.
 */
static char kbd_getchar(void)
{
    /* Check COM1 receive buffer first (used by automated tests via -serial stdio). */
    if (inb(COM1 + 5) & 0x01) {
        char c = (char)inb(COM1);
        return (c == '\r') ? '\n' : c;   /* normalise CR → LF */
    }

    if (!(inb(KBD_STATUS) & 0x01))
        return 0;

    unsigned char sc = inb(KBD_DATA);

    if (sc == 0xE0) {
        e0_seen = 1;
        return 0;
    }

    if (sc & 0x80) {
        /* key-release: clear any E0 state and update shift */
        unsigned char make = sc & 0x7F;
        if (make == SC_LSHIFT || make == SC_RSHIFT)
            shift_pressed = 0;
        e0_seen = 0;
        return 0;
    }

    if (e0_seen) {
        e0_seen = 0;
        switch (sc) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        default:   return 0;
        }
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
        shift_pressed = 1;
        return 0;
    }

    if (sc < SCANCODE_MAP_SIZE)
        return shift_pressed ? scancode_map_shift[sc] : scancode_map[sc];

    return 0;
}


/* ============================================================
 * ATA PIO driver — primary channel, master drive
 * ============================================================ */

/* Primary ATA channel I/O ports */
#define ATA_DATA      0x1F0   /* 16-bit data                      */
#define ATA_SECT_CNT  0x1F2   /* sector count                     */
#define ATA_LBA_LO    0x1F3   /* LBA bits  7:0                    */
#define ATA_LBA_MID   0x1F4   /* LBA bits 15:8                    */
#define ATA_LBA_HI    0x1F5   /* LBA bits 23:16                   */
#define ATA_DRIVE     0x1F6   /* drive select + LBA bits 27:24    */
#define ATA_CMD       0x1F7   /* command (write) / status (read)  */
#define ATA_ALT_ST    0x3F6   /* alternate status (read-only here)*/

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

/* 400 ns delay: 4 reads of alternate status (~100 ns each) */
static void ata_delay(void)
{
    inb(ATA_ALT_ST); inb(ATA_ALT_ST);
    inb(ATA_ALT_ST); inb(ATA_ALT_ST);
}

/* Wait until BSY clears; returns 0 OK, -1 error/timeout */
static int ata_wait_bsy(void)
{
    for (int i = 0; i < 0x100000; i++) {
        unsigned char s = inb(ATA_CMD);
        if (s & ATA_SR_ERR)    return -1;
        if (!(s & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* Wait until BSY clears and DRQ sets; returns 0 OK, -1 error/timeout */
static int ata_wait_drq(void)
{
    for (int i = 0; i < 0x100000; i++) {
        unsigned char s = inb(ATA_CMD);
        if (s & ATA_SR_ERR)                         return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))  return 0;
    }
    return -1;
}

/*
 * Read one 512-byte sector at LBA address into buf[256].
 * Returns 0 on success, -1 on error.
 */
int ata_read_sector(unsigned int lba, unsigned short *buf)
{
    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_DRIVE,    (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECT_CNT, 1);
    outb(ATA_LBA_LO,   (unsigned char)(lba         & 0xFF));
    outb(ATA_LBA_MID,  (unsigned char)((lba >>  8) & 0xFF));
    outb(ATA_LBA_HI,   (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_CMD,      ATA_CMD_READ);

    ata_delay();
    if (ata_wait_drq() < 0) return -1;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return 0;
}

/*
 * Write one 512-byte sector from buf[256] to LBA address.
 * Returns 0 on success, -1 on error.
 */
int ata_write_sector(unsigned int lba, const unsigned short *buf)
{
    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_DRIVE,    (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECT_CNT, 1);
    outb(ATA_LBA_LO,   (unsigned char)(lba         & 0xFF));
    outb(ATA_LBA_MID,  (unsigned char)((lba >>  8) & 0xFF));
    outb(ATA_LBA_HI,   (unsigned char)((lba >> 16) & 0xFF));
    outb(ATA_CMD,      ATA_CMD_WRITE);

    ata_delay();
    if (ata_wait_drq() < 0) return -1;

    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, buf[i]);

    /* Flush drive write cache */
    outb(ATA_CMD, ATA_CMD_FLUSH);
    ata_delay();
    ata_wait_bsy();

    return 0;
}

/* ============================================================
 * Status bar (row 24)
 * ============================================================ */


/* ============================================================
 * Kernel entry point
 * ============================================================ */

/* Convert unsigned int to decimal string (null-terminated) */
static void uint_to_str(unsigned int n, char *out)
{
    if (n == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[12];
    int i = 0;
    while (n) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

/* Must match the stack layout built by isr_common (see isr.asm) */
struct registers {
    unsigned int gs, fs, es, ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha */
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags;  /* pushed by CPU */
};

/* ============================================================
 * Syscall interface — int 0x80
 *
 * Register convention:
 *   EAX = syscall number
 *   EBX = arg1,  ECX = arg2,  EDX = arg3
 *   Return value written back to EAX in the saved register frame.
 *
 * Syscall numbers:
 *   0  exit(code)
 *   1  write(fd, buf, len)  -> bytes written;  fd 1 = stdout
 *   2  read(fd, buf, len)   -> bytes read;     fd 0 = stdin (line-buffered)
 *   3  open(path, flags)    -> fd or -1;       flags: 0=read, 1=write
 *   4  close(fd)            -> 0 or -1
 *
 * File descriptors:
 *   0  stdin  (PS/2 keyboard, line-buffered)
 *   1  stdout (VGA + serial)
 *   2+ FAT16 file (up to MAX_FILE_FDS open at once)
 * ============================================================ */

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_GETCHAR          5   /* blocking raw keyread, no echo          */
#define SYS_SETPOS           6   /* set VGA cursor: EBX=row, ECX=col       */
#define SYS_CLRSCR           7   /* clear text area, cursor to 0,0         */
#define SYS_GETCHAR_NONBLOCK 8   /* non-blocking keyread; 0 = no key ready */
#define SYS_READDIR  9   /* (buf_ptr, max) → count of direntry structs */
#define SYS_UNLINK  10   /* (name_ptr)     → 0/-1/-2(not-empty)        */
#define SYS_MKDIR   11   /* (name_ptr)     → 0/-1                      */
#define SYS_RENAME  12   /* (src, dst)     → 0/-1                      */
#define SYS_EXEC    13   /* (name, args)   → child exit code or -1     */
#define SYS_CHDIR   14   /* (name_ptr)     → 0/-1                      */
#define SYS_GETPOS  15   /* ()             → row*256 + col             */

struct direntry { char name[13]; unsigned int size; int is_dir; };

#define FD_STDIN   0
#define FD_STDOUT  1
#define FD_FILE0   2

#define O_RDONLY   0
#define O_WRONLY   1

#define MAX_FILE_FDS  4
#define FILE_BUF_SIZE 16384   /* 16 KB per file descriptor */

struct fd_entry {
    int           used;
    int           mode;
    unsigned int  size;
    unsigned int  pos;
    char          name[13];
    unsigned char buf[FILE_BUF_SIZE];
};

static struct fd_entry g_fds[MAX_FILE_FDS];

extern int fat16_read(const char *filename, unsigned char *buf, unsigned int max_bytes);
extern int fat16_write(const char *filename, const unsigned char *data, unsigned int size);
extern int fat16_read_from_bin(const char *name, unsigned char *buf, unsigned int max_bytes);

static int sys_write(unsigned int fd, const char *buf, unsigned int len)
{
    unsigned int i;
    if (fd == FD_STDOUT) {
        for (i = 0; i < len; i++)
            vga_putchar(buf[i], COLOR_DEFAULT);
        return (int)len;
    }
    if (fd >= FD_FILE0 && fd < (unsigned int)(FD_FILE0 + MAX_FILE_FDS)) {
        struct fd_entry *f = &g_fds[fd - FD_FILE0];
        if (!f->used || f->mode != O_WRONLY) return -1;
        for (i = 0; i < len; i++) {
            if (f->pos >= FILE_BUF_SIZE) return (int)i;
            f->buf[f->pos++] = (unsigned char)buf[i];
        }
        if (f->pos > f->size) f->size = f->pos;
        return (int)len;
    }
    return -1;
}

static int sys_read(unsigned int fd, char *buf, unsigned int len)
{
    if (fd == FD_STDIN) {
        unsigned int i = 0;
        while (i < len) {
            char c = 0;
            while (!c) c = kbd_getchar();
            buf[i++] = c;
            vga_putchar(c, COLOR_DEFAULT);
            serial_putchar(c);
            if (c == '\n') break;
        }
        return (int)i;
    }
    if (fd >= FD_FILE0 && fd < (unsigned int)(FD_FILE0 + MAX_FILE_FDS)) {
        struct fd_entry *f = &g_fds[fd - FD_FILE0];
        if (!f->used || f->mode != O_RDONLY) return -1;
        unsigned int i;
        for (i = 0; i < len && f->pos < f->size; i++)
            buf[i] = (char)f->buf[f->pos++];
        return (int)i;
    }
    return -1;
}

static int sys_open(const char *path, int flags)
{
    int i;
    for (i = 0; i < MAX_FILE_FDS; i++) {
        if (!g_fds[i].used) break;
    }
    if (i == MAX_FILE_FDS) return -1;

    struct fd_entry *f = &g_fds[i];

    int j;
    for (j = 0; j < 12 && path[j]; j++) f->name[j] = path[j];
    f->name[j] = '\0';

    f->mode = flags;
    f->pos  = 0;

    if (flags == O_RDONLY) {
        int n = fat16_read(path, f->buf, FILE_BUF_SIZE);
        if (n < 0) return -1;
        f->size = (unsigned int)n;
    } else {
        f->size = 0;
    }

    f->used = 1;
    return i + FD_FILE0;
}

static int sys_close(unsigned int fd)
{
    if (fd < FD_FILE0 || fd >= (unsigned int)(FD_FILE0 + MAX_FILE_FDS)) return -1;
    struct fd_entry *f = &g_fds[fd - FD_FILE0];
    if (!f->used) return -1;

    if (f->mode == O_WRONLY)
        fat16_write(f->name, f->buf, f->size);

    f->used = 0;
    return 0;
}

extern unsigned int exec_ret_esp;  /* defined in entry.asm; used by SYS_EXIT */
static unsigned int g_exit_code;   /* set by SYS_EXIT, returned by SYS_EXEC */

/* ============================================================
 * Forward declarations needed by syscall_dispatch
 * ============================================================ */

/* Program loader constants */
#define PROG_BASE      0x400000
#define PROG_MAX_SIZE  (256 * 1024)
#define ARGS_BASE      0x7FC000
#define ARGS_MAX       200
#define USER_STACK_TOP 0x7FF000

/* High-memory kernel virtual addresses for loading binaries */
#define SHELL_LOAD_VIRT  0x800000u
#define SHELL_ARGS_KERN  0xBFC000u
#define CHILD_LOAD_VIRT  0xC00000u
#define CHILD_ARGS_KERN  0xFFC000u

extern void         exec_run(unsigned int entry, unsigned int user_stack_top);
extern int            fat16_listdir(void (*cb)(const char *name, unsigned int size, int is_dir));
extern int            fat16_delete(const char *name);
extern int            fat16_mkdir(const char *name);
extern int            fat16_rename(const char *src, const char *dst);
extern int            fat16_chdir(const char *name);
extern unsigned short fat16_get_cwd_cluster(void);
extern void           fat16_set_cwd_cluster(unsigned short c);

static void switch_to_shell_pagedir(void);
static void switch_to_child_pagedir(void);

/* Directory listing buffer used by SYS_READDIR */
#define LS_MAX_ENTRIES 64

struct ls_entry {
    char         name[13];
    unsigned int size;
    int          is_dir;
};

static struct ls_entry ls_buf[LS_MAX_ENTRIES];
static int             ls_count;

static void ls_collect(const char *name, unsigned int size, int is_dir)
{
    if (ls_count >= LS_MAX_ENTRIES) return;
    int i = 0;
    while (name[i] && i < 12) { ls_buf[ls_count].name[i] = name[i]; i++; }
    ls_buf[ls_count].name[i] = '\0';
    ls_buf[ls_count].size   = size;
    ls_buf[ls_count].is_dir = is_dir;
    ls_count++;
}

static void syscall_dispatch(struct registers *r)
{
    switch (r->eax) {
    case SYS_EXIT:
        g_exit_code = r->ebx;
        /* Restore the kernel stack saved by exec_run() and return to
         * program_exec() as if exec_run() returned normally.
         * exec_ret_esp points at: edi, esi, ebx, ebp, retaddr (low→high). */
        __asm__ volatile (
            "mov %0, %%esp\n"
            "pop %%edi\n"
            "pop %%esi\n"
            "pop %%ebx\n"
            "pop %%ebp\n"
            "sti\n"
            "ret\n"
            :
            : "r"(exec_ret_esp)
        );
        break;
    case SYS_WRITE:
        r->eax = (unsigned int)sys_write(r->ebx, (const char *)r->ecx, r->edx);
        break;
    case SYS_READ:
        r->eax = (unsigned int)sys_read(r->ebx, (char *)r->ecx, r->edx);
        break;
    case SYS_OPEN:
        r->eax = (unsigned int)sys_open((const char *)r->ebx, (int)r->ecx);
        break;
    case SYS_CLOSE:
        r->eax = (unsigned int)sys_close(r->ebx);
        break;
    case SYS_GETCHAR: {
        char c = 0;
        while (!c) c = kbd_getchar();
        r->eax = (unsigned int)(unsigned char)c;
        break;
    }
    case SYS_SETPOS: {
        int row = (int)r->ebx;
        int col = (int)r->ecx;
        if (row < 0) row = 0;
        if (row >= VGA_ROWS) row = VGA_ROWS - 1;
        if (col < 0) col = 0;
        if (col >= VGA_COLS) col = VGA_COLS - 1;
        cursor_row = row;
        cursor_col = col;
        vga_update_hw_cursor();
        r->eax = 0;
        break;
    }
    case SYS_CLRSCR:
        vga_clear();
        r->eax = 0;
        break;
    case SYS_GETCHAR_NONBLOCK:
        r->eax = (unsigned int)(unsigned char)kbd_getchar();
        break;
    case SYS_READDIR: {
        struct direntry *user_buf = (struct direntry *)r->ebx;
        int max = (int)r->ecx;
        ls_count = 0;
        if (fat16_listdir(ls_collect) < 0) { r->eax = (unsigned int)-1; break; }
        int rn = ls_count < max ? ls_count : max;
        for (int ri = 0; ri < rn; ri++) {
            int j;
            for (j = 0; j < 12 && ls_buf[ri].name[j]; j++)
                user_buf[ri].name[j] = ls_buf[ri].name[j];
            user_buf[ri].name[j] = '\0';
            user_buf[ri].size   = ls_buf[ri].size;
            user_buf[ri].is_dir = ls_buf[ri].is_dir;
        }
        r->eax = (unsigned int)rn;
        break;
    }
    case SYS_UNLINK:
        r->eax = (unsigned int)fat16_delete((const char *)r->ebx);
        break;
    case SYS_MKDIR:
        r->eax = (unsigned int)fat16_mkdir((const char *)r->ebx);
        break;
    case SYS_RENAME:
        r->eax = (unsigned int)fat16_rename((const char *)r->ebx, (const char *)r->ecx);
        break;
    case SYS_CHDIR:
        r->eax = (unsigned int)fat16_chdir((const char *)r->ebx);
        break;
    case SYS_GETPOS:
        r->eax = (unsigned int)(cursor_row * 256 + cursor_col);
        break;
    case SYS_EXEC: {
        char name[13], args[ARGS_MAX];
        int xi;
        const char *src_name = (const char *)r->ebx;
        for (xi = 0; xi < 12 && src_name[xi]; xi++) name[xi] = src_name[xi];
        name[xi] = '\0';
        const char *src_args = (const char *)r->ecx;
        for (xi = 0; xi < ARGS_MAX - 1 && src_args[xi]; xi++) args[xi] = src_args[xi];
        args[xi] = '\0';

        /* Save shell's exec return context and current working directory */
        unsigned int   saved_exec_ret = exec_ret_esp;
        unsigned short saved_cwd      = fat16_get_cwd_cluster();

        /* Switch to child page directory and load binary */
        switch_to_child_pagedir();
        int xn = fat16_read_from_bin(name, (unsigned char *)CHILD_LOAD_VIRT, PROG_MAX_SIZE);
        if (xn <= 0) {
            switch_to_shell_pagedir();
            exec_ret_esp = saved_exec_ret;
            r->eax = (unsigned int)-1;
            break;
        }

        /* Copy args to child's ARGS_BASE (kernel virt 0xFFC000 = phys 0xFFC000) */
        char *dst = (char *)CHILD_ARGS_KERN;
        for (xi = 0; xi < ARGS_MAX - 1 && args[xi]; xi++) dst[xi] = args[xi];
        dst[xi] = '\0';

        g_exit_code = 0;
        exec_run(PROG_BASE, USER_STACK_TOP);

        /* Child returned — detect and restore VGA if it switched to graphics */
        outb(0x3CE, 0x06);
        int xgfx = (inb(0x3CF) != saved_text_regs.gc[6]);
        vga_restore_textmode();
        if (xgfx) vga_clear();

        /* Switch back to shell page directory and restore exec context + cwd */
        switch_to_shell_pagedir();
        fat16_set_cwd_cluster(saved_cwd);
        exec_ret_esp = saved_exec_ret;

        r->eax = (unsigned int)g_exit_code;
        break;
    }
    default:
        r->eax = (unsigned int)-1;
        break;
    }
}

/* ============================================================
 * ISR handler — called from isr_common in isr.asm
 * ============================================================ */

static const char *exception_name(unsigned int n)
{
    static const char *names[] = {
        "Division by zero",        /* 0  */
        "Debug",                   /* 1  */
        "NMI",                     /* 2  */
        "Breakpoint",              /* 3  */
        "Overflow",                /* 4  */
        "Bound range exceeded",    /* 5  */
        "Invalid opcode",          /* 6  */
        "Device not available",    /* 7  */
        "Double fault",            /* 8  */
        "Coprocessor overrun",     /* 9  */
        "Invalid TSS",             /* 10 */
        "Segment not present",     /* 11 */
        "Stack fault",             /* 12 */
        "General protection fault",/* 13 */
        "Page fault",              /* 14 */
        "Reserved",                /* 15 */
        "x87 FPU error",           /* 16 */
        "Alignment check",         /* 17 */
        "Machine check",           /* 18 */
        "SIMD FP exception",       /* 19 */
    };
    if (n < 20) return names[n];
    return "Reserved";
}

/* Called from isr_common; r points to the saved register frame on the stack */
void isr_handler(struct registers *r)
{
    if (r->int_no < 32) {
        /* Page fault from user space: deliver segfault and return to shell */
        if (r->int_no == 14 && (r->err_code & 0x04) && exec_ret_esp != 0) {
            vga_print("\nSegmentation fault\n", COLOR_DEFAULT);
            serial_print("[exec] Segmentation fault\n");
            g_exit_code = 139;
            /* Restore kernel stack saved by exec_run() and return to
             * program_exec() via exec_run_return label.
             * Stack at exec_ret_esp: edi, esi, ebx, ebp, retaddr. */
            __asm__ volatile (
                "mov %0, %%esp\n"
                "pop %%edi\n"
                "pop %%esi\n"
                "pop %%ebx\n"
                "pop %%ebp\n"
                "sti\n"
                "ret\n"
                :
                : "r"(exec_ret_esp)
            );
        }

        /* CPU exception — print panic screen and halt */
        vga_print("\n\n *** KERNEL PANIC: ", COLOR_ERR);
        vga_print(exception_name(r->int_no), COLOR_ERR);
        vga_print(" *** \n", COLOR_ERR);
        serial_print("[PANIC] exception ");
        char tmp[4];
        uint_to_str(r->int_no, tmp);
        serial_print(tmp);
        serial_print(": ");
        serial_print(exception_name(r->int_no));
        serial_putchar('\n');
        for (;;) __asm__ volatile ("hlt");

    } else if (r->int_no < 48) {
        /* Hardware IRQ — send EOI and return */
        if (r->int_no >= 40)
            outb(0xA0, 0x20);   /* slave EOI */
        outb(0x20, 0x20);       /* master EOI */

    } else if (r->int_no == 0x80) {
        syscall_dispatch(r);
    }
}

extern void idt_init(void);
extern void gdt_init(void);
extern int  fat16_init(void);

/* ============================================================
 * Kernel entry point
 * ============================================================ */

/* Parse leading decimal digits from a byte buffer; stop at non-digit. */
static unsigned int parse_uint(const unsigned char *s, int len)
{
    unsigned int n = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '9'; i++)
        n = n * 10 + (unsigned int)(s[i] - '0');
    return n;
}

/* ============================================================
 * Paging setup — identity map with U/S protection
 * ============================================================ */

static unsigned int page_dir[1024]       __attribute__((aligned(4096)));
static unsigned int pt_kernel[1024]      __attribute__((aligned(4096)));  /* 0–4 MB, U=0 */
static unsigned int pt_user[1024]        __attribute__((aligned(4096)));  /* 4–8 MB, U=1 */
static unsigned int pt_kern_high0[1024]  __attribute__((aligned(4096)));  /* 8–12 MB, U=0 */
static unsigned int pt_kern_high1[1024]  __attribute__((aligned(4096)));  /* 12–16 MB, U=0 */
static unsigned int page_dir_shell[1024] __attribute__((aligned(4096)));
static unsigned int pt_user_shell[1024]  __attribute__((aligned(4096)));  /* virt 4–8MB → phys 8–12MB */
static unsigned int page_dir_child[1024] __attribute__((aligned(4096)));
static unsigned int pt_user_child[1024]  __attribute__((aligned(4096)));  /* virt 4–8MB → phys 12–16MB */

static void paging_init(void)
{
    int i;

    /* Kernel page table: identity map 0–4MB, supervisor only (bits P+RW, U=0) */
    for (i = 0; i < 1024; i++)
        pt_kernel[i] = (unsigned int)(i << 12) | 0x03;   /* P + RW */

    /* VGA framebuffer pages 0xA0000–0xBFFFF in kernel PT need U=1 so the
     * demo program can access them from ring 3.
     * Page indices for 0xA0000–0xBFFFF: 0xA0..0xBF */
    for (i = 0xA0; i <= 0xBF; i++)
        pt_kernel[i] = (unsigned int)(i << 12) | 0x07;   /* P + RW + U */

    /* User page table: identity map 4–8MB, user accessible (P+RW+U) */
    for (i = 0; i < 1024; i++)
        pt_user[i] = (unsigned int)(0x400000 + (i << 12)) | 0x07;  /* P + RW + U */

    /* Page directory: first two 4-MB windows */
    for (i = 0; i < 1024; i++)
        page_dir[i] = 0;

    page_dir[0] = (unsigned int)pt_kernel | 0x07;   /* U=1 in PDE; individual PTEs enforce S/U */
    page_dir[1] = (unsigned int)pt_user   | 0x07;   /* user:   user-accessible */

    /* Kernel high mappings (supervisor-only, identity): used to load binaries */
    for (i = 0; i < 1024; i++) {
        pt_kern_high0[i] = (unsigned int)(0x800000 + (i << 12)) | 0x03;  /* P+RW, U=0 */
        pt_kern_high1[i] = (unsigned int)(0xC00000 + (i << 12)) | 0x03;
    }
    page_dir[2] = (unsigned int)pt_kern_high0 | 0x03;  /* virtual 0x800000–0xBFFFFF */
    page_dir[3] = (unsigned int)pt_kern_high1 | 0x03;  /* virtual 0xC00000–0xFFFFFF */

    /* Shell page directory: virtual 0x400000 → physical 0x800000 (U=1) */
    for (i = 0; i < 1024; i++)
        pt_user_shell[i] = (unsigned int)(0x800000 + (i << 12)) | 0x07;
    page_dir_shell[0] = (unsigned int)pt_kernel     | 0x07;
    page_dir_shell[1] = (unsigned int)pt_user_shell | 0x07;
    page_dir_shell[2] = (unsigned int)pt_kern_high0 | 0x03;
    page_dir_shell[3] = (unsigned int)pt_kern_high1 | 0x03;

    /* Child page directory: virtual 0x400000 → physical 0xC00000 (U=1) */
    for (i = 0; i < 1024; i++)
        pt_user_child[i] = (unsigned int)(0xC00000 + (i << 12)) | 0x07;
    page_dir_child[0] = (unsigned int)pt_kernel     | 0x07;
    page_dir_child[1] = (unsigned int)pt_user_child | 0x07;
    page_dir_child[2] = (unsigned int)pt_kern_high0 | 0x03;
    page_dir_child[3] = (unsigned int)pt_kern_high1 | 0x03;

    /* Load CR3 and enable paging in CR0 */
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_dir) : "eax"
    );
}

static void switch_to_shell_pagedir(void)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir_shell) : "memory");
}

static void switch_to_child_pagedir(void)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir_child) : "memory");
}

/* ============================================================
 * Kernel entry point
 * ============================================================ */

void kernel_main(void)
{
    serial_init();
    serial_print("[kernel] started\n");

    paging_init();
    serial_print("[kernel] paging ready\n");

    gdt_init();
    serial_print("[kernel] GDT ready\n");

    idt_init();
    __asm__ volatile ("sti");
    serial_print("[kernel] IDT ready\n");

    vga_save_state();   /* capture BIOS text-mode register state */
    vga_save_font();    /* capture BIOS font from VGA plane 2    */
    vga_clear();
    serial_print("[kernel] VGA cleared\n");

    vga_print("Welcome to the YOLO-OS\n\n", COLOR_HELLO);
    serial_print("[kernel] Welcome to the YOLO-OS\n");

    /* ---- FAT16: persistent boot counter in BOOT.TXT ---- */
    static unsigned char fat_buf[32];  /* file content buffer */

    if (fat16_init() == 0) {
        int n = fat16_read("BOOT.TXT", fat_buf, sizeof(fat_buf) - 1);
        unsigned int count = (n > 0) ? parse_uint(fat_buf, n) : 0;

        count++;

        /* Render new count into fat_buf as "NNN\n" */
        char cnt_str[12];
        uint_to_str(count, cnt_str);
        int slen = 0;
        while (cnt_str[slen]) { fat_buf[slen] = (unsigned char)cnt_str[slen]; slen++; }
        fat_buf[slen++] = '\n';

        fat16_write("BOOT.TXT", fat_buf, (unsigned int)slen);

        vga_print("Boot #", COLOR_DEFAULT);
        vga_print(cnt_str, COLOR_HELLO);
        vga_print("\n\n", COLOR_DEFAULT);
        serial_print("[disk] boot #"); serial_print(cnt_str); serial_putchar('\n');
    } else {
        vga_print("Disk: error\n\n", COLOR_DEFAULT);
        serial_print("[disk] error\n");
    }

    serial_print("[kernel] ready\n");

    /* Load shell binary into physical 0x800000 (kernel virtual 0x800000) */
    int sh_n = fat16_read_from_bin("sh", (unsigned char *)SHELL_LOAD_VIRT, PROG_MAX_SIZE);
    if (sh_n <= 0) {
        vga_print("FATAL: /bin/sh not found\n", COLOR_ERR);
        for (;;) __asm__ volatile("hlt");
    }
    serial_print("[kernel] launching /bin/sh\n");

    /* Switch to shell page directory and exec shell at virtual 0x400000 */
    switch_to_shell_pagedir();
    exec_run(PROG_BASE, USER_STACK_TOP);

    /* Shell called exit() — unrecoverable */
    vga_print("Shell exited. System halted.\n", COLOR_DEFAULT);
    for (;;) __asm__ volatile("hlt");
}
