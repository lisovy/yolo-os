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

/* Full text-mode recovery — called after every user program exits.
 * Restores VGA registers and font without clearing the framebuffer so that
 * the output of text-mode programs remains visible after they exit. */
static void vga_restore_textmode(void)
{
    vga_restore_state();
    vga_restore_font();
}

/* Check if we left graphics mode and restore text mode.
 * Clears the screen only if the program had switched to graphics mode. */
static void vga_check_and_restore_textmode(void)
{
    outb(0x3CE, 0x06);
    int was_graphics = (inb(0x3CF) != saved_text_regs.gc[6]);
    vga_restore_textmode();
    if (was_graphics) vga_clear();
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

/* ── panic screen ─────────────────────────────────────────────────────────
 * Fills the entire 80×25 screen with a red background, prints the reason
 * and a register dump (GP regs + CR0/CR2/CR3/CR4), then returns to the
 * caller which should halt the CPU.
 * -------------------------------------------------------------------------- */

#define PANIC_BODY  0x4E   /* yellow on red */
#define PANIC_HDR   0x4F   /* white  on red */

static void ps_str(volatile unsigned short *v, int row, int col,
                   const char *s, unsigned char a)
{
    while (*s && col < 80)
        v[row * 80 + col++] = ((unsigned short)a << 8) | (unsigned char)*s++;
}

static void ps_hex(volatile unsigned short *v, int row, int col,
                   unsigned int val, unsigned char a)
{
    const char h[] = "0123456789ABCDEF";
    v[row*80+col+0] = ((unsigned short)a<<8)|'0';
    v[row*80+col+1] = ((unsigned short)a<<8)|'x';
    for (int i = 0; i < 8; i++)
        v[row*80+col+2+i] = ((unsigned short)a<<8)|h[(val>>(28-4*i))&0xF];
}

/* Print label (padded to 6 chars) then "0xXXXXXXXX" at (row, col).
 * Total width: 6 (label) + 1 (space) + 10 (hex) = 17 chars.
 * Slots at cols 2, 22, 42, 62 → hex at cols 9, 29, 49, 69. */
static void ps_reg(volatile unsigned short *v, int row, int col,
                   const char *lbl, unsigned int val, unsigned char a)
{
    int c = col;
    while (*lbl && (c - col) < 6)
        v[row*80+c++] = ((unsigned short)a<<8)|(unsigned char)*lbl++;
    while ((c - col) < 6)
        v[row*80+c++] = ((unsigned short)a<<8)|' ';
    v[row*80+c] = ((unsigned short)a<<8)|' ';   /* 1 space gap */
    ps_hex(v, row, c + 1, val, a);
}

static void serial_hex(unsigned int val)
{
    const char h[] = "0123456789ABCDEF";
    serial_putchar('0'); serial_putchar('x');
    for (int i = 7; i >= 0; i--)
        serial_putchar(h[(val >> (i*4)) & 0xF]);
}

static void panic_screen(const char *msg, struct registers *r)
{
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;

    /* Fill entire screen with red background */
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = ((unsigned short)PANIC_BODY << 8) | ' ';

    /* Title (centred) */
    ps_str(vga,  0, 30, "*** KERNEL PANIC ***", PANIC_HDR);

    /* Reason */
    ps_str(vga,  2,  1, "Reason: ", PANIC_HDR);
    ps_str(vga,  2,  9, msg,        PANIC_BODY);

    /* General purpose register section */
    ps_str(vga,  4,  1, "General Purpose Registers", PANIC_HDR);
    ps_reg(vga,  5,  2, "EAX",    r->eax,    PANIC_BODY);
    ps_reg(vga,  5, 22, "EBX",    r->ebx,    PANIC_BODY);
    ps_reg(vga,  5, 42, "ECX",    r->ecx,    PANIC_BODY);
    ps_reg(vga,  5, 62, "EDX",    r->edx,    PANIC_BODY);
    ps_reg(vga,  6,  2, "ESI",    r->esi,    PANIC_BODY);
    ps_reg(vga,  6, 22, "EDI",    r->edi,    PANIC_BODY);
    ps_reg(vga,  6, 42, "EBP",    r->ebp,    PANIC_BODY);
    ps_reg(vga,  6, 62, "ESP",    r->esp,    PANIC_BODY);
    ps_reg(vga,  7,  2, "EIP",    r->eip,    PANIC_BODY);
    ps_reg(vga,  7, 22, "EFLAGS", r->eflags, PANIC_BODY);
    ps_reg(vga,  7, 42, "CS",     r->cs,     PANIC_BODY);
    ps_reg(vga,  7, 62, "DS",     r->ds,     PANIC_BODY);

    /* Control register section */
    ps_str(vga,  9,  1, "Control Registers", PANIC_HDR);
    unsigned int cr0, cr2, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    ps_reg(vga, 10,  2, "CR0", cr0, PANIC_BODY);
    ps_reg(vga, 10, 22, "CR2", cr2, PANIC_BODY);
    ps_reg(vga, 10, 42, "CR3", cr3, PANIC_BODY);
    ps_reg(vga, 10, 62, "CR4", cr4, PANIC_BODY);

    /* Serial dump */
    serial_print("[PANIC] "); serial_print(msg); serial_putchar('\n');
    serial_print("[PANIC] EAX="); serial_hex(r->eax);
    serial_print(" EBX=");        serial_hex(r->ebx);
    serial_print(" ECX=");        serial_hex(r->ecx);
    serial_print(" EDX=");        serial_hex(r->edx); serial_putchar('\n');
    serial_print("[PANIC] ESI="); serial_hex(r->esi);
    serial_print(" EDI=");        serial_hex(r->edi);
    serial_print(" EBP=");        serial_hex(r->ebp);
    serial_print(" ESP=");        serial_hex(r->esp); serial_putchar('\n');
    serial_print("[PANIC] EIP="); serial_hex(r->eip);
    serial_print(" EFLAGS=");     serial_hex(r->eflags);
    serial_print(" CS=");         serial_hex(r->cs);
    serial_print(" DS=");         serial_hex(r->ds);  serial_putchar('\n');
    serial_print("[PANIC] CR0="); serial_hex(cr0);
    serial_print(" CR2=");        serial_hex(cr2);
    serial_print(" CR3=");        serial_hex(cr3);
    serial_print(" CR4=");        serial_hex(cr4);    serial_putchar('\n');
}

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
#define SYS_PANIC   16   /* (msg_ptr)      → does not return           */
#define SYS_MEMINFO 17   /* (meminfo_ptr)  → 0                         */
#define SYS_SBRK    18   /* (n)            → old_break or -1           */
#define SYS_SLEEP   19   /* (ms)           → 0                         */

/* PIT tick frequency — must match divisor in pit_init() in idt.c */
#define PIT_HZ      100

static unsigned int g_ticks = 0;   /* incremented by IRQ0 (PIT) every 10 ms */

struct meminfo {
    unsigned int phys_total_kb;
    unsigned int phys_used_kb;
    unsigned int phys_free_kb;
    unsigned int virt_total_kb;
    unsigned int virt_used_kb;
    unsigned int virt_free_kb;
    int          n_procs;
};

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
    char          name[128];
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
        /* Enable interrupts so background processes can run while we wait. */
        __asm__ volatile("sti");
        while (i < len) {
            char c = 0;
            while (!c) {
                __asm__ volatile("hlt");
                c = kbd_getchar();
            }
            buf[i++] = c;
            vga_putchar(c, COLOR_DEFAULT);
            serial_putchar(c);
            if (c == '\n') break;
        }
        __asm__ volatile("cli");
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

    int plen = 0;
    while (path[plen]) plen++;
    if (plen > 127) return -1;

    int j;
    for (j = 0; j < 127 && path[j]; j++) f->name[j] = path[j];
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

extern void tss_set_ring0_stack(unsigned int esp0);

/* ============================================================
 * Forward declarations needed by syscall_dispatch
 * ============================================================ */

/* Program loader constants */
#define PROG_BASE      0x400000
#define PROG_MAX_SIZE  (256 * 1024)
#define ARGS_BASE      0x7FC000
#define ARGS_MAX       200
#define USER_STACK_TOP 0x7FF000
#define HEAP_BASE      0x440000   /* first heap page (VPN 64, right after binary) */
#define HEAP_MAX       0x7F8000   /* heap limit: VPN 1015, stays clear of stack   */
#define PAGE_SIZE      4096

extern void         exec_run(unsigned int entry, unsigned int user_stack_top,
                              unsigned int kstack_top);
extern int            fat16_listdir(void (*cb)(const char *name, unsigned int size, int is_dir));
extern int            fat16_delete(const char *name);
extern int            fat16_mkdir(const char *name);
extern int            fat16_rename(const char *src, const char *dst);
extern int            fat16_chdir(const char *name);
extern unsigned short fat16_get_cwd_cluster(void);
extern void           fat16_set_cwd_cluster(unsigned short c);

/* ============================================================
 * Paging data structures
 * ============================================================ */

static unsigned int page_dir[1024]   __attribute__((aligned(4096)));
static unsigned int pt_kernel[1024]  __attribute__((aligned(4096)));  /* 0–4 MB */

/* ============================================================
 * PMM — physical memory manager
 * ============================================================ */
#include "pmm.h"

/* ============================================================
 * Process Control Block
 * ============================================================ */

#define PROC_MAX_PROCS  32
#define PROC_MAX_FRAMES  2   /* phys_frames[0]=PD, phys_frames[1]=PT; user pages freed via PT scan */

typedef enum { PROC_UNUSED=0, PROC_RUNNING, PROC_READY, PROC_ZOMBIE,
               PROC_SLEEPING, PROC_WAITING } proc_state_t;

struct process {
    int            pid;
    proc_state_t   state;

    unsigned int   cr3;                        /* physical address of page directory */
    unsigned int   parent_cr3;                 /* physical address of parent page dir */

    unsigned int   phys_frames[PROC_MAX_FRAMES]; /* [0]=PD, [1]=user PT              */
    int            n_frames;

    unsigned int   heap_break;                 /* current heap break (sbrk)           */

    unsigned int   saved_exec_ret_esp;         /* exec_ret_esp of parent */

    unsigned int   wakeup_tick;                /* g_ticks value at which to wake up   */

    unsigned int   phys_kstack;                /* physical address of per-process ring-0 stack */
    unsigned int   saved_esp;                  /* saved kernel ESP for context switch */

    int            exit_code;

    int            is_background;             /* 1 = background, 0 = foreground      */
    unsigned int   saved_cwd_cluster;         /* FAT16 CWD at launch (BG exit restore) */
};

static struct process  g_procs[PROC_MAX_PROCS];
static struct process *g_current = 0;

static void process_destroy(struct process *p);  /* forward declaration */

/* Round-robin: find next READY or RUNNING process (never returns g_current). */
static struct process *pick_next_process(void)
{
    if (!g_current) return 0;
    int cur = (int)(g_current - g_procs);
    int i;
    for (i = 1; i < PROC_MAX_PROCS; i++) {
        int j = (cur + i) % PROC_MAX_PROCS;
        proc_state_t s = g_procs[j].state;
        if (s == PROC_READY || s == PROC_RUNNING)
            return &g_procs[j];
    }
    return 0;
}

/*
 * process_create — build a per-process page directory and load the binary.
 * Must be called while CR3 = page_dir (kernel identity map).
 *
 * Virtual layout in PDE[1] (base 0x400000):
 *   VPN   0..63    binary  (64 × 4 KB = 256 KB)
 *   VPN  64..1015  heap    (unmapped initially, mapped on demand by SYS_SBRK)
 *   VPN 1016..1022 stack   (7 × 4 KB = 28 KB)
 *   VPN 1020       ARGS_BASE = 0x7FC000  (stack_frames[4])
 *
 * Only phys_frames[0]=PD and phys_frames[1]=PT are tracked here.
 * All user pages (binary, stack, heap) are freed by scanning the PT
 * in process_destroy().
 */
static struct process *process_create(const char *name, const char *args)
{
    int i, slot = -1;
    for (i = 0; i < PROC_MAX_PROCS; i++) {
        /* Lazily reuse ZOMBIE slots */
        if (g_procs[i].state == PROC_ZOMBIE) {
            process_destroy(&g_procs[i]);
            g_procs[i].state = PROC_UNUSED;
        }
        if (g_procs[i].state == PROC_UNUSED) { slot = i; break; }
    }
    if (slot < 0) return 0;

    struct process *p = &g_procs[slot];
    p->n_frames      = 0;
    p->pid           = slot + 1;
    p->heap_break    = HEAP_BASE;
    p->phys_kstack   = 0;
    p->is_background = 0;

    /* Declare pt early so the fail: block can reference it safely */
    unsigned int *pt = (unsigned int *)0;

    /* [1] Allocate page directory */
    unsigned int pd_phys = pmm_alloc();
    if (!pd_phys) return 0;
    p->phys_frames[0] = pd_phys;
    p->n_frames       = 1;
    p->cr3            = pd_phys;

    /* [2] Allocate user page table; clear it immediately */
    unsigned int pt_phys = pmm_alloc();
    if (!pt_phys) goto fail;
    p->phys_frames[1] = pt_phys;
    p->n_frames       = 2;
    pt                = (unsigned int *)pt_phys;
    for (i = 0; i < 1024; i++) pt[i] = 0;

    /* [3] Allocate kernel stack and build initial ring-3 context frame.
     * The frame mirrors what isr_common pushes when preempting a ring-3 process,
     * allowing the scheduler to start this process via context switch on first run. */
    unsigned int kstack_phys = pmm_alloc();
    if (!kstack_phys) goto fail;
    p->phys_kstack = kstack_phys;
    {
        unsigned int *kst = (unsigned int *)(kstack_phys + PAGE_SIZE);
        *(--kst) = 0x23;            /* user SS  (ring-3 data selector)   */
        *(--kst) = USER_STACK_TOP;  /* user ESP                          */
        *(--kst) = 0x3200;          /* EFLAGS: IF=1, IOPL=3              */
        *(--kst) = 0x1B;            /* user CS  (ring-3 code selector)   */
        *(--kst) = PROG_BASE;       /* user EIP (program entry point)    */
        *(--kst) = 0;               /* err_code (dummy)                  */
        *(--kst) = 0;               /* int_no   (dummy)                  */
        /* pusha order on stack: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI */
        *(--kst) = 0;               /* EAX */
        *(--kst) = 0;               /* ECX */
        *(--kst) = 0;               /* EDX */
        *(--kst) = 0;               /* EBX */
        *(--kst) = 0;               /* ESP (ignored by popa) */
        *(--kst) = 0;               /* EBP */
        *(--kst) = 0;               /* ESI */
        *(--kst) = 0;               /* EDI */
        *(--kst) = 0x23;            /* DS */
        *(--kst) = 0x23;            /* ES */
        *(--kst) = 0x23;            /* FS */
        *(--kst) = 0x23;            /* GS  ← saved_esp points here */
        p->saved_esp = (unsigned int)kst;   /* = kstack_phys + PAGE_SIZE - 76 */
    }
    p->saved_cwd_cluster = fat16_get_cwd_cluster();

    /* [4] Allocate 64 contiguous frames for binary (VPN 0–63) */
    unsigned int bin_phys = pmm_alloc_contiguous(64);
    if (!bin_phys) goto fail;
    for (i = 0; i < 64; i++)
        pt[i] = (bin_phys + (unsigned int)i * 0x1000) | 0x07;  /* P+RW+U */

    /* [5] Allocate 7 frames for stack+args (VPN 1016–1022) */
    for (i = 0; i < 7; i++) {
        unsigned int f = pmm_alloc();
        if (!f) goto fail;
        pt[1016 + i] = f | 0x07;                                /* P+RW+U */
    }
    /* VPN 1020 = ARGS_BASE = stack frame 4 (1020 - 1016) */

    /* [6] Load binary into physical frames (identity-mapped in kernel page_dir).
     * Zero-fill bytes [n..PROG_MAX_SIZE) so the .bss section is properly zeroed
     * (physical frames may carry stale data from previous processes). */
    int n = fat16_read_from_bin(name, (unsigned char *)bin_phys, PROG_MAX_SIZE);
    if (n <= 0) goto fail;
    {
        unsigned char *base = (unsigned char *)bin_phys;
        for (i = n; i < PROG_MAX_SIZE; i++) base[i] = 0;
    }

    /* [7] Copy args into the args page (identity-mapped) */
    char *dst = (char *)(pt[1020] & ~0xFFFu);
    for (i = 0; i < ARGS_MAX - 1 && args[i]; i++) dst[i] = args[i];
    dst[i] = '\0';

    /* [8] Build page directory */
    unsigned int *pd = (unsigned int *)pd_phys;
    for (i = 0; i < 1024; i++) pd[i] = 0;
    pd[0] = (unsigned int)pt_kernel | 0x07;   /* shared kernel PT, 0–4 MB */
    pd[1] = pt_phys | 0x07;                   /* user PT                   */
    /* PDE[2]–PDE[511]: 4 MB PSE supervisor-only identity (kernel write access) */
    for (i = 2; i < 512; i++)
        pd[i] = (unsigned int)(i << 22) | 0x83;  /* P+RW+PS, U=0 */

    p->state = PROC_READY;
    return p;

fail:
    /* Free any user pages already mapped in the PT */
    if (p->n_frames >= 2) {
        for (i = 0; i < 1024; i++)
            if (pt[i] & 0x01) pmm_free(pt[i] & ~0xFFFu);
        pmm_free(p->phys_frames[1]);         /* PT itself */
    }
    if (p->n_frames >= 1) pmm_free(p->phys_frames[0]);  /* PD */
    if (p->phys_kstack)   pmm_free(p->phys_kstack);
    p->n_frames    = 0;
    p->phys_kstack = 0;
    p->state       = PROC_UNUSED;
    return 0;
}

/*
 * process_destroy — release all physical memory owned by process p.
 * Scans the user PT to find and free all mapped user pages (binary, stack,
 * heap), then frees the PT, PD, and kernel stack.
 * Must be called while CR3 = page_dir (kernel identity map).
 */
static void process_destroy(struct process *p)
{
    int vpn;
    unsigned int *pt = (unsigned int *)p->phys_frames[1];
    for (vpn = 0; vpn < 1024; vpn++)
        if (pt[vpn] & 0x01) pmm_free(pt[vpn] & ~0xFFFu);
    pmm_free(p->phys_frames[0]);   /* PD          */
    pmm_free(p->phys_frames[1]);   /* PT          */
    if (p->phys_kstack) pmm_free(p->phys_kstack);
    p->n_frames    = 0;
    p->phys_kstack = 0;
    p->state       = PROC_UNUSED;
}

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
        g_current->exit_code = (int)r->ebx;
        if (g_current->is_background) {
            /* Background process: restore VGA/CWD, mark zombie, yield via hlt.
             * IRQ0 will context-switch away on the next tick. */
            vga_check_and_restore_textmode();
            fat16_set_cwd_cluster((unsigned short)g_current->saved_cwd_cluster);
            g_current->state = PROC_ZOMBIE;
            __asm__ volatile("sti");
            for (;;) __asm__ volatile("hlt");
        } else {
            /* Foreground process: longjmp back to SYS_EXEC (or kernel_main).
             * cli ensures IRQ0 doesn't fire between the ESP swap and the ret;
             * sti is done in SYS_EXEC after g_current is updated. */
            g_exit_code = (int)r->ebx;
            __asm__ volatile(
                "cli\n"
                "mov %0, %%esp\n"
                "pop %%edi\n"
                "pop %%esi\n"
                "pop %%ebx\n"
                "pop %%ebp\n"
                "ret\n"
                :
                : "r"(exec_ret_esp)
                : "memory"
            );
        }
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
        /* Enable interrupts so the scheduler can run background processes
         * while we are waiting for keyboard input. */
        __asm__ volatile("sti");
        while (!c) {
            __asm__ volatile("hlt");
            c = kbd_getchar();
        }
        __asm__ volatile("cli");
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
    case SYS_PANIC:
        panic_screen((const char *)r->ebx, r);
        for (;;) __asm__ volatile("hlt");
        break;
    case SYS_MEMINFO: {
        struct meminfo *info = (struct meminfo *)r->ebx;
        unsigned int total_frames = pmm_total();
        unsigned int used_frames  = pmm_count_used();
        info->phys_total_kb = total_frames * 4;
        info->phys_used_kb  = used_frames  * 4;
        info->phys_free_kb  = (total_frames - used_frames) * 4;

        /* Count active processes and their mapped virtual pages */
        int n_procs = 0;
        unsigned int virt_used_pages = 0;
        for (int mi = 0; mi < PROC_MAX_PROCS; mi++) {
            if (g_procs[mi].state == PROC_UNUSED) continue;
            n_procs++;
            /* phys_frames[1] = user page table (identity-mapped in 0–4MB) */
            if (g_procs[mi].n_frames >= 2) {
                unsigned int *pt = (unsigned int *)g_procs[mi].phys_frames[1];
                for (int pj = 0; pj < 1024; pj++)
                    if (pt[pj] & 0x01) virt_used_pages++;
            }
        }
        info->n_procs       = n_procs;
        info->virt_total_kb = (unsigned int)(n_procs * 4096); /* 4 MB per proc */
        info->virt_used_kb  = virt_used_pages * 4;
        info->virt_free_kb  = info->virt_total_kb - info->virt_used_kb;
        r->eax = 0;
        break;
    }
    case SYS_SBRK: {
        /*
         * sbrk(n): map n more bytes of heap, return old break, or -1 on failure.
         * Heap lives at HEAP_BASE..HEAP_MAX-1 (VPN 64..1015 in the user PT).
         * Switch to kernel page_dir so we can safely write to the process PT
         * regardless of where the PT frame sits in physical memory.
         */
        int sbrk_n = (int)r->ebx;
        if (sbrk_n == 0) { r->eax = g_current->heap_break; break; }
        if (sbrk_n < 0 || (unsigned int)sbrk_n > HEAP_MAX - g_current->heap_break) {
            r->eax = (unsigned int)-1; break;
        }
        unsigned int old_brk = g_current->heap_break;
        unsigned int new_brk = old_brk + (unsigned int)sbrk_n;

        /* Switch to kernel page_dir for safe PT access */
        __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir) : "memory");

        unsigned int *sbrk_pt = (unsigned int *)g_current->phys_frames[1];
        unsigned int va;
        int oom = 0;
        for (va = old_brk & ~0xFFFu; va < new_brk; va += 0x1000) {
            unsigned int vpn = (va - PROG_BASE) / 0x1000;
            if (sbrk_pt[vpn] & 0x01) continue;   /* already mapped */
            unsigned int pa = pmm_alloc();
            if (!pa) { oom = 1; break; }
            sbrk_pt[vpn] = pa | 0x07;             /* P+RW+U */
        }

        /* Switch back to process page_dir (TLB flush picks up new mappings) */
        __asm__ volatile("mov %0, %%cr3" :: "r"(g_current->cr3) : "memory");

        if (oom) { r->eax = (unsigned int)-1; break; }
        g_current->heap_break = new_brk;
        r->eax = old_brk;
        break;
    }
    case SYS_EXEC: {
        char name[13], args[ARGS_MAX];
        int xi;

        /* [A] Copy name/args from parent's user space (current CR3) */
        const char *src_name = (const char *)r->ebx;
        for (xi = 0; xi < 12 && src_name[xi]; xi++) name[xi] = src_name[xi];
        name[xi] = '\0';
        const char *src_args = (const char *)r->ecx;
        for (xi = 0; xi < ARGS_MAX - 1 && src_args[xi]; xi++) args[xi] = src_args[xi];
        args[xi] = '\0';

        /* EDX bit 0: 0 = foreground, 1 = background */
        int bg = (int)(r->edx & 1);

        unsigned short saved_cwd = fat16_get_cwd_cluster();

        /* [B] Switch to kernel page_dir (identity map needed for process_create) */
        __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir) : "memory");

        /* [C] Create child process (loads binary + builds page tables) */
        struct process *child = process_create(name, args);
        if (!child) {
            unsigned int par_cr3c = g_current ? g_current->cr3 : (unsigned int)page_dir;
            __asm__ volatile("mov %0, %%cr3" :: "r"(par_cr3c) : "memory");
            fat16_set_cwd_cluster(saved_cwd);
            r->eax = (unsigned int)-1;
            break;
        }

        child->is_background = bg;

        if (bg) {
            /* [BG] Background: child is READY, return PID to shell immediately.
             * IRQ0 will schedule the child on its next turn. */
            child->state = PROC_READY;
            __asm__ volatile("mov %0, %%cr3" :: "r"(g_current->cr3) : "memory");
            fat16_set_cwd_cluster(saved_cwd);
            r->eax = (unsigned int)child->pid;
            break;
        }

        /* [D] Foreground: record parent context in child PCB */
        child->parent_cr3         = g_current ? g_current->cr3 : (unsigned int)page_dir;
        child->saved_exec_ret_esp = exec_ret_esp;
        child->state              = PROC_RUNNING;
        struct process *parent    = g_current;
        parent->state             = PROC_WAITING;   /* scheduler skips waiting parent */
        g_current                 = child;
        g_exit_code               = 0;

        /* [E] Switch to child page directory and run */
        __asm__ volatile("mov %0, %%cr3" :: "r"(child->cr3) : "memory");
        exec_run(PROG_BASE, USER_STACK_TOP, child->phys_kstack + PAGE_SIZE);

        /* [F] Child finished — SYS_EXIT did cli before longjmping here */
        exec_ret_esp         = child->saved_exec_ret_esp;
        unsigned int par_cr3 = child->parent_cr3;
        int          ecode   = g_exit_code;

        /* [G] Cleanup: switch to kernel page_dir, destroy child */
        __asm__ volatile("mov %0, %%cr3" :: "r"(page_dir) : "memory");
        process_destroy(child);

        /* Update g_current before sti so IRQ0 sees correct state */
        g_current     = parent;
        parent->state = PROC_RUNNING;
        tss_set_ring0_stack(parent->phys_kstack + PAGE_SIZE);
        __asm__ volatile("sti");   /* re-enable interrupts */

        /* [H] Restore VGA text mode, cwd, then switch to parent page_dir */
        vga_check_and_restore_textmode();
        fat16_set_cwd_cluster(saved_cwd);
        __asm__ volatile("mov %0, %%cr3" :: "r"(par_cr3) : "memory");

        r->eax = (unsigned int)ecode;
        break;
    }
    case SYS_SLEEP: {
        /* sleep(ms): block the current process for at least ms milliseconds.
         * The PIT fires at PIT_HZ; each tick is 1000/PIT_HZ ms.
         * The process spins on hlt (with interrupts enabled) until the timer
         * ISR sets its state back to PROC_RUNNING. */
        unsigned int ms = r->ebx;
        unsigned int ticks = (ms * (unsigned int)PIT_HZ + 999u) / 1000u;
        if (ticks == 0) ticks = 1;
        g_current->wakeup_tick = g_ticks + ticks;
        g_current->state = PROC_SLEEPING;
        __asm__ volatile("sti");
        while (g_current->state == PROC_SLEEPING)
            __asm__ volatile("hlt");
        r->eax = 0;
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

/* Called from isr_common; r points to the saved register frame on the stack.
 * Returns 0 for no context switch, or new process's saved_esp to switch. */
unsigned int isr_handler(struct registers *r)
{
    if (r->int_no < 32) {
        /* Page fault from user space: deliver segfault */
        if (r->int_no == 14 && (r->err_code & 0x04)) {
            if (g_current && g_current->is_background) {
                /* Background process: mark zombie, yield via hlt loop */
                vga_print("\nSegmentation fault\n", COLOR_DEFAULT);
                vga_check_and_restore_textmode();
                fat16_set_cwd_cluster((unsigned short)g_current->saved_cwd_cluster);
                g_current->exit_code = 139;
                g_current->state = PROC_ZOMBIE;
                __asm__ volatile("sti");
                for (;;) __asm__ volatile("hlt");
            } else if (exec_ret_esp != 0) {
                /* Foreground process: longjmp back to SYS_EXEC handler */
                vga_print("\nSegmentation fault\n", COLOR_DEFAULT);
                g_exit_code = 139;
                __asm__ volatile(
                    "cli\n"
                    "mov %0, %%esp\n"
                    "pop %%edi\n"
                    "pop %%esi\n"
                    "pop %%ebx\n"
                    "pop %%ebp\n"
                    "ret\n"
                    :
                    : "r"(exec_ret_esp)
                    : "memory"
                );
            }
        }

        /* CPU exception — print panic screen and halt */
        panic_screen(exception_name(r->int_no), r);
        for (;;) __asm__ volatile("hlt");

    } else if (r->int_no < 48) {
        /* Hardware IRQ */
        if (r->int_no == 32) {
            /* IRQ0 — PIT tick */
            int si;
            g_ticks++;
            /* Wake any processes whose sleep timer has expired */
            for (si = 0; si < PROC_MAX_PROCS; si++) {
                if (g_procs[si].state == PROC_SLEEPING &&
                    g_ticks >= g_procs[si].wakeup_tick)
                    g_procs[si].state = PROC_RUNNING;
            }
            /* Preemptive context switch: find next runnable process */
            if (g_current) {
                struct process *next = pick_next_process();
                if (next) {
                    if (g_current->state != PROC_ZOMBIE) {
                        /* Save current process's context */
                        g_current->saved_esp = (unsigned int)r;
                        if (g_current->state == PROC_RUNNING)
                            g_current->state = PROC_READY;
                    }
                    /* Switch to next process (works for both normal and zombie) */
                    next->state = PROC_RUNNING;
                    g_current   = next;
                    __asm__ volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
                    tss_set_ring0_stack(next->phys_kstack + PAGE_SIZE);
                    /* EOI before returning — must send before iret */
                    outb(0x20, 0x20);
                    return next->saved_esp;
                }
            }
        }
        /* EOI */
        if (r->int_no >= 40)
            outb(0xA0, 0x20);   /* slave EOI */
        outb(0x20, 0x20);       /* master EOI */

    } else if (r->int_no == 0x80) {
        syscall_dispatch(r);
    }
    return 0;
}

extern void idt_init(void);
extern void gdt_init(void);
extern void pit_init(void);
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

static void paging_init(void)
{
    int i;

    /* Kernel page table: identity map 0–4MB, supervisor only */
    for (i = 0; i < 1024; i++)
        pt_kernel[i] = (unsigned int)(i << 12) | 0x03;   /* P + RW */

    /* VGA framebuffers 0xA0000–0xBFFFF need U=1 for ring-3 demo program */
    for (i = 0xA0; i <= 0xBF; i++)
        pt_kernel[i] = (unsigned int)(i << 12) | 0x07;   /* P + RW + U */

    /* Enable PSE (4-MB pages) in CR4 */
    __asm__ volatile (
        "mov %%cr4, %%eax\n"
        "or $0x10, %%eax\n"
        "mov %%eax, %%cr4\n"
        ::: "eax"
    );

    /* Kernel page directory */
    for (i = 0; i < 1024; i++) page_dir[i] = 0;

    page_dir[0] = (unsigned int)pt_kernel | 0x07;   /* 4KB pages, 0–4MB */

    /* PDE[1]–PDE[511]: 4 MB large pages, supervisor-only identity map
     * covering 4 MB–2 GB so kernel can write to any physical frame */
    for (i = 1; i < 512; i++)
        page_dir[i] = (unsigned int)(i << 22) | 0x83;   /* P+RW+PS, U=0 */

    /* Load CR3 and enable paging + PSE in CR0 */
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_dir) : "eax"
    );
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

    pit_init();
    serial_print("[kernel] PIT ready (100 Hz)\n");

    pmm_init();
    serial_print("[kernel] PMM ready\n");

    /* Create shell process */
    struct process *shell = process_create("sh", "");
    if (!shell) {
        vga_print("FATAL: /bin/sh not found\n", COLOR_ERR);
        for (;;) __asm__ volatile("hlt");
    }
    g_current    = shell;
    shell->state = PROC_RUNNING;
    serial_print("[kernel] launching /bin/sh\n");

    /* Switch to shell page directory and exec shell at virtual 0x400000 */
    __asm__ volatile("mov %0, %%cr3" :: "r"(shell->cr3) : "memory");
    exec_run(PROG_BASE, USER_STACK_TOP, shell->phys_kstack + PAGE_SIZE);

    /* Shell called exit() — unrecoverable */
    vga_print("Shell exited. System halted.\n", COLOR_DEFAULT);
    for (;;) __asm__ volatile("hlt");
}
