/*
 * kernel.c - Simple bare-metal kernel for IBM PC x86
 *
 * Video:     VGA text mode — direct writes to memory at 0xB8000.
 *            BIOS interrupts are unavailable in 32-bit protected mode.
 * Keyboard:  PS/2 polling via I/O ports 0x60 / 0x64.
 *            Scan code set 1, US QWERTY layout.
 */

/* VGA text mode */
#define VGA_MEMORY  0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25

/* Attribute byte: high nibble = background, low nibble = foreground color */
#define COLOR_DEFAULT  0x07   /* light gray on black */
#define COLOR_HELLO    0x0F   /* bright white on black */
#define COLOR_PROMPT   0x0A   /* light green on black */

/* PS/2 keyboard I/O ports */
#define KBD_DATA    0x60
#define KBD_STATUS  0x64

/* ============================================================
 * I/O port helpers
 * ============================================================ */

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ============================================================
 * VGA text mode driver
 * ============================================================ */

static int cursor_col = 0;
static int cursor_row = 0;

static void vga_clear(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;
    unsigned short blank = (COLOR_DEFAULT << 8) | ' ';

    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = blank;

    cursor_col = 0;
    cursor_row = 0;
}

static void vga_scroll(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEMORY;

    /* Shift every row one line up */
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

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        /* Backspace: erase the character to the left of the cursor */
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
}

static void vga_print(const char *s, unsigned char color)
{
    while (*s)
        vga_putchar(*s++, color);
}

/* ============================================================
 * PS/2 keyboard driver — scan code set 1, US QWERTY
 * ============================================================ */

/*
 * Index = scan code (make), value = ASCII character.
 * Value 0 means no ASCII representation (Shift, Ctrl, F-keys, ...).
 */
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

#define SCANCODE_MAP_SIZE  ((int)(sizeof(scancode_map) / sizeof(scancode_map[0])))

/*
 * Wait for a keypress and return its ASCII character.
 * Returns 0 for keys with no ASCII representation.
 */
static char kbd_getchar(void)
{
    /* Wait until the keyboard output buffer is full */
    while (!(inb(KBD_STATUS) & 0x01))
        ;

    unsigned char sc = inb(KBD_DATA);

    /* Bit 7 set = key-release event; ignore it */
    if (sc & 0x80)
        return 0;

    if (sc < SCANCODE_MAP_SIZE)
        return scancode_map[sc];

    return 0;
}

/* ============================================================
 * Kernel entry point
 * ============================================================ */

void kernel_main(void)
{
    vga_clear();

    vga_print("Hello, World!\n\n", COLOR_HELLO);
    vga_print("IBM PC x86 bare-metal kernel\n", COLOR_DEFAULT);
    vga_print("Type on the keyboard -- characters appear below:\n\n", COLOR_DEFAULT);
    vga_print("> ", COLOR_PROMPT);

    for (;;) {
        char c = kbd_getchar();
        if (c == 0)
            continue;

        vga_putchar(c, COLOR_DEFAULT);

        /* Print a new prompt after Enter */
        if (c == '\n' || c == '\r')
            vga_print("> ", COLOR_PROMPT);
    }
}
