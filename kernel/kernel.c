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

void kernel_main(void)
{
    serial_init();
    serial_print("[kernel] started\n");

    vga_clear();
    serial_print("[kernel] VGA cleared\n");

    vga_print("Hello, World!\n\n", COLOR_HELLO);
    serial_print("[kernel] Hello, World!\n");

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
