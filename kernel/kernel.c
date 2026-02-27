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

/* RTC I/O ports and registers */
#define RTC_INDEX   0x70
#define RTC_DATA    0x71
#define RTC_REG_SEC   0x00
#define RTC_REG_MIN   0x02
#define RTC_REG_HOUR  0x04
#define RTC_REG_DAY   0x07
#define RTC_REG_MON   0x08
#define RTC_REG_YEAR  0x09
#define RTC_REG_STA   0x0A   /* Status A: bit 7 = Update In Progress */
#define RTC_REG_STB   0x0B   /* Status B: bit 2 = binary mode, bit 1 = 24h mode */

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

    /* Clear only the text area (rows 0 to TEXT_ROWS-1) */
    for (int i = 0; i < VGA_COLS * TEXT_ROWS; i++)
        vga[i] = blank;

    cursor_col = 0;
    cursor_row = 0;
    vga_update_hw_cursor();
}

static void vga_scroll(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

    /* Shift every text row one line up, staying within TEXT_ROWS */
    for (int row = 0; row < TEXT_ROWS - 1; row++)
        for (int col = 0; col < VGA_COLS; col++)
            vga[row * VGA_COLS + col] = vga[(row + 1) * VGA_COLS + col];

    /* Clear the last text row */
    unsigned short blank = (COLOR_DEFAULT << 8) | ' ';
    for (int col = 0; col < VGA_COLS; col++)
        vga[(TEXT_ROWS - 1) * VGA_COLS + col] = blank;

    cursor_row = TEXT_ROWS - 1;
}

static void vga_putchar(char c, unsigned char color)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

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

    if (cursor_row >= TEXT_ROWS)
        vga_scroll();
    vga_update_hw_cursor();
}

static void vga_print(const char *s, unsigned char color)
{
    while (*s)
        vga_putchar(*s++, color);
}

/* ============================================================
 * PS/2 keyboard driver — scan code set 1, US QWERTY
 * ============================================================ */

#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36

static const char scancode_map[] = {
    /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
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

/*
 * Non-blocking: returns 0 immediately if no key is ready.
 * Tracks Shift state; returns 0 for non-ASCII keys.
 */
static char kbd_getchar(void)
{
    if (!(inb(KBD_STATUS) & 0x01))
        return 0;

    unsigned char sc = inb(KBD_DATA);

    if (sc & 0x80) {
        unsigned char make = sc & 0x7F;
        if (make == SC_LSHIFT || make == SC_RSHIFT)
            shift_pressed = 0;
        return 0;
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
 * RTC — IBM PC Real Time Clock
 * ============================================================ */

static unsigned char rtc_read(unsigned char reg)
{
    outb(RTC_INDEX, reg);
    return inb(RTC_DATA);
}

static unsigned char bcd_to_bin(unsigned char bcd)
{
    return (unsigned char)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

struct rtc_time {
    int sec, min, hour, day, mon, year;
};

static void rtc_get_time(struct rtc_time *t)
{
    /* Wait until RTC is not in the middle of an update */
    while (rtc_read(RTC_REG_STA) & 0x80)
        ;

    unsigned char sec  = rtc_read(RTC_REG_SEC);
    unsigned char min  = rtc_read(RTC_REG_MIN);
    unsigned char hour = rtc_read(RTC_REG_HOUR);
    unsigned char day  = rtc_read(RTC_REG_DAY);
    unsigned char mon  = rtc_read(RTC_REG_MON);
    unsigned char yr   = rtc_read(RTC_REG_YEAR);
    unsigned char stb  = rtc_read(RTC_REG_STB);

    int binary = (stb & 0x04) != 0;
    int h24    = (stb & 0x02) != 0;

    /* In 12h mode bit 7 of the hour byte is the PM flag */
    int pm = (!h24) && (hour & 0x80);
    if (!h24) hour &= 0x7F;

    if (!binary) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        yr   = bcd_to_bin(yr);
    }

    /* Convert 12h -> 24h if needed */
    if (!h24) {
        if (pm && hour != 12) hour = (unsigned char)(hour + 12);
        else if (!pm && hour == 12) hour = 0;
    }

    t->sec  = sec;
    t->min  = min;
    t->hour = hour;
    t->day  = day;
    t->mon  = mon;
    t->year = 2000 + yr;
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

static int last_sec = -1;
static int colon_on = 1;

static void status_bar_update(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

    /* Fast path: only redraw when the second changes */
    if (rtc_read(RTC_REG_STA) & 0x80)
        return;  /* update in progress, skip */

    unsigned char stb = rtc_read(RTC_REG_STB);
    int binary = (stb & 0x04) != 0;
    unsigned char raw_sec = rtc_read(RTC_REG_SEC);
    int sec = binary ? raw_sec : bcd_to_bin(raw_sec);

    if (sec == last_sec)
        return;

    colon_on = !colon_on;
    last_sec = sec;

    /* Read full time */
    struct rtc_time t;
    rtc_get_time(&t);

    /* Fill entire status bar row with blue background */
    unsigned short bg = (COLOR_STATUS_BG << 8) | ' ';
    for (int col = 0; col < VGA_COLS; col++)
        vga[STATUS_ROW * VGA_COLS + col] = bg;

    /*
     * Build date+time string: "DD.MM.YYYY HH:MM" (16 chars)
     * The colon blinks every second.
     */
    char str[16];
    str[0]  = (char)('0' + t.day  / 10);
    str[1]  = (char)('0' + t.day  % 10);
    str[2]  = '.';
    str[3]  = (char)('0' + t.mon  / 10);
    str[4]  = (char)('0' + t.mon  % 10);
    str[5]  = '.';
    str[6]  = (char)('0' + t.year / 1000);
    str[7]  = (char)('0' + (t.year / 100) % 10);
    str[8]  = (char)('0' + (t.year / 10)  % 10);
    str[9]  = (char)('0' + t.year % 10);
    str[10] = ' ';
    str[11] = (char)('0' + t.hour / 10);
    str[12] = (char)('0' + t.hour % 10);
    str[13] = colon_on ? ':' : ' ';
    str[14] = (char)('0' + t.min / 10);
    str[15] = (char)('0' + t.min % 10);

    /* Write right-aligned at columns 64..79 */
    int start = VGA_COLS - 16;
    for (int i = 0; i < 16; i++)
        vga[STATUS_ROW * VGA_COLS + start + i] =
            (COLOR_STATUS_TIME << 8) | (unsigned char)str[i];
}

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

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_OPEN   3
#define SYS_CLOSE  4

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

static int sys_write(unsigned int fd, const char *buf, unsigned int len)
{
    unsigned int i;
    if (fd == FD_STDOUT) {
        for (i = 0; i < len; i++) {
            vga_putchar(buf[i], COLOR_DEFAULT);
            serial_putchar(buf[i]);
        }
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

static void syscall_dispatch(struct registers *r)
{
    switch (r->eax) {
    case SYS_EXIT:
        serial_print("[kernel] program exited\n");
        /* halt until program loader is implemented */
        for (;;) __asm__ volatile ("hlt");
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

void kernel_main(void)
{
    serial_init();
    serial_print("[kernel] started\n");

    idt_init();
    __asm__ volatile ("sti");
    serial_print("[kernel] IDT ready\n");

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

    serial_print("[kernel] entering keyboard loop\n");
    vga_print("> ", COLOR_PROMPT);

    for (;;) {
        status_bar_update();

        char c = kbd_getchar();
        if (c == 0)
            continue;

        vga_putchar(c, COLOR_DEFAULT);
        serial_putchar(c);

        if (c == '\n' || c == '\r') {
            vga_print("> ", COLOR_PROMPT);
            serial_print("> ");
        }
    }
}
