/* vi.c — vi-like text editor for YOLO-OS
 *
 * Usage:    run vi <file>
 * Modes:    Normal (default)  |  Insert (i)  |  Command (:)
 * Movement: arrow keys
 * Commands: :w   save
 *           :q   quit (refuses if unsaved)
 *           :q!  force quit
 *           :wq  save and quit
 *
 * Screen layout:
 *   rows  0-23  text content   (EDIT_ROWS = 24 visible lines)
 *   row   24    status / cmd bar
 *
 * Scroll guard: row 24 is written with at most 79 chars so the VGA
 * cursor never wraps to row 25 and triggers vga_scroll().
 *
 * Entry point: user.ld places .text.startup before .text so GCC's
 * main() always lands at 0x400000 regardless of its position here.
 */

#include "os.h"

#define EDIT_ROWS  24
#define LNUM_W      6       /* 4-digit number + 2 spaces */
#define EDIT_COLS  (80 - LNUM_W)

#define MAX_BUF    16384
#define MAX_LINES  512

#define MODE_NORMAL  0
#define MODE_INSERT  1
#define MODE_COMMAND 2

#define ESC  0x1B

/* ------------------------------------------------------------------ */

static char  buf[MAX_BUF];
static int   buf_len;
static int   lines[MAX_LINES]; /* lines[i] = buf offset of start of line i */
static int   nlines;

static int   cy, cx;           /* cursor: line (0-based), col (0-based) */
static int   top;              /* first visible line */

static int   mode;
static char  cmd[32];
static int   cmd_len;
static char  msg[64];          /* one-shot status message, cleared on next key */

static char  filename[64];
static int   modified;

/* ------------------------------------------------------------------ */
/* Utilities                                                           */

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void scopy(char *d, const char *s, int max)
{
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int seq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

/* Write integer n right-aligned in w chars to dst; returns w. */
static int fmt_num(char *dst, int n, int w)
{
    char tmp[12];
    int  i = 0;
    if (n == 0) { tmp[i++] = '0'; }
    else { int x = n; while (x) { tmp[i++] = (char)('0' + x % 10); x /= 10; } }
    for (int pad = w - i; pad > 0; pad--) *dst++ = ' ';
    while (i > 0) *dst++ = tmp[--i];
    return w;
}

/* ------------------------------------------------------------------ */
/* Line index                                                          */

static void rebuild(void)
{
    nlines = 1;
    lines[0] = 0;
    for (int i = 0; i < buf_len && nlines < MAX_LINES; i++)
        if (buf[i] == '\n')
            lines[nlines++] = i + 1;
}

/* Length of line li, not counting the trailing '\n'. */
static int llen(int li)
{
    if (li >= nlines) return 0;
    int end = (li + 1 < nlines) ? lines[li + 1] - 1 : buf_len;
    return end - lines[li];
}

/* ------------------------------------------------------------------ */
/* Buffer edits                                                        */

static void binsert(int pos, char c)
{
    if (buf_len >= MAX_BUF - 1) return;
    for (int i = buf_len; i > pos; i--) buf[i] = buf[i - 1];
    buf[pos] = c;
    buf_len++;
    modified = 1;
    rebuild();
}

static void bdelete(int pos)
{
    if (pos < 0 || pos >= buf_len) return;
    for (int i = pos; i < buf_len - 1; i++) buf[i] = buf[i + 1];
    buf_len--;
    modified = 1;
    rebuild();
}

/* ------------------------------------------------------------------ */
/* Display                                                             */

static void redraw(void)
{
    clrscr();

    char rowbuf[80];

    for (int row = 0; row < EDIT_ROWS; row++) {
        int li  = top + row;
        int pos = 0;

        if (li < nlines) {
            /* line number: 4-digit right-aligned + 2 spaces */
            pos += fmt_num(rowbuf, li + 1, 4);
            rowbuf[pos++] = ' ';
            rowbuf[pos++] = ' ';

            /* line content, clamped to available columns */
            int n = llen(li);
            if (n > EDIT_COLS) n = EDIT_COLS;
            for (int j = 0; j < n; j++)
                rowbuf[pos++] = buf[lines[li] + j];
        } else {
            rowbuf[pos++] = '~';
        }

        set_pos(row, 0);
        write(STDOUT, rowbuf, pos);
    }

    /* Status bar — capped at 79 chars to prevent cursor wrapping to
     * row 24, which would trigger vga_scroll() and ruin the display. */
    char st[79];
    int  sp = 0;

    if (msg[0]) {
        int n = slen(msg);
        if (n > 79) n = 79;
        for (int i = 0; i < n; i++) st[sp++] = msg[i];
    } else if (mode == MODE_COMMAND) {
        st[sp++] = ':';
        for (int i = 0; i < cmd_len && sp < 79; i++) st[sp++] = cmd[i];
    } else if (mode == MODE_INSERT) {
        const char *ins = "-- INSERT --";
        for (; *ins && sp < 79; ins++) st[sp++] = *ins;
    } else {
        int n = slen(filename);
        if (n > 30) n = 30;
        for (int i = 0; i < n; i++) st[sp++] = filename[i];
        if (modified) {
            const char *m = " [+]";
            for (; *m && sp < 79; m++) st[sp++] = *m;
        }
    }

    set_pos(24, 0);
    write(STDOUT, st, sp);

    /* place hardware cursor at edit position */
    int scol = LNUM_W + cx;
    if (scol > 79) scol = 79;
    set_pos(cy - top, scol);
}

/* ------------------------------------------------------------------ */
/* Cursor helpers                                                      */

static void clamp_cx(void)
{
    int m = llen(cy);
    if (cx > m) cx = m;
    if (cx < 0) cx = 0;
}

static void scroll_to_cursor(void)
{
    if (cy < top)              top = cy;
    if (cy >= top + EDIT_ROWS) top = cy - EDIT_ROWS + 1;
}

/* ------------------------------------------------------------------ */
/* File I/O                                                            */

static void load(void)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { buf_len = 0; rebuild(); return; }
    int n = read(fd, buf, MAX_BUF - 1);
    buf_len = (n > 0) ? n : 0;
    close(fd);
    rebuild();
}

static void save(void)
{
    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        scopy(msg, "ERROR: cannot open for writing", sizeof(msg));
        return;
    }
    write(fd, buf, buf_len);
    close(fd);
    modified = 0;
    scopy(msg, "saved", sizeof(msg));
}

/* ------------------------------------------------------------------ */
/* main                                                                */

void main(void)
{
    const char *arg = get_args();
    if (!arg || !arg[0]) { print("usage: run vi <file>\n"); exit(1); }
    scopy(filename, arg, sizeof(filename));

    load();
    cy = 0; cx = 0; top = 0;
    mode = MODE_NORMAL;
    msg[0] = '\0';
    modified = 0;
    redraw();

    for (;;) {
        int c = get_char();
        msg[0] = '\0';

        /* Arrow keys work in normal and insert mode (not command) */
        if (mode != MODE_COMMAND &&
            (c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT)) {
            if      (c == KEY_UP)   { if (cy > 0)          { cy--; clamp_cx(); } }
            else if (c == KEY_DOWN) { if (cy < nlines - 1) { cy++; clamp_cx(); } }
            else if (c == KEY_LEFT) { if (cx > 0) cx--; }
            else                    { int m = llen(cy); if (cx < m) cx++; }

        /* ---- NORMAL ---- */
        } else if (mode == MODE_NORMAL) {
            switch ((char)c) {
            case 'i': mode = MODE_INSERT; break;
            case 'o': {
                /* open new line below current */
                binsert(lines[cy] + llen(cy), '\n');
                cy++; cx = 0;
                mode = MODE_INSERT;
                break;
            }
            case 'x': {
                /* delete char under cursor (not the newline) */
                int pos = lines[cy] + cx;
                if (pos < buf_len && buf[pos] != '\n') {
                    bdelete(pos);
                    clamp_cx();
                }
                break;
            }
            case ':':
                mode = MODE_COMMAND;
                cmd_len = 0; cmd[0] = '\0';
                break;
            }

        /* ---- INSERT ---- */
        } else if (mode == MODE_INSERT) {
            if (c == ESC) {
                mode = MODE_NORMAL;
                if (cx > 0) cx--;   /* vi moves cursor back one on ESC */
                clamp_cx();
            } else if (c == '\b') {
                int pos = lines[cy] + cx;
                if (pos > 0) {
                    int prev_len = (cx == 0 && cy > 0) ? llen(cy - 1) : -1;
                    bdelete(pos - 1);
                    if (cx > 0) {
                        cx--;
                    } else if (cy > 0) {
                        cy--;
                        cx = prev_len;
                    }
                }
            } else if (c == '\r' || c == '\n') {
                binsert(lines[cy] + cx, '\n');
                cy++; cx = 0;
            } else if (c >= 0x20 && c < 0x7F) {
                binsert(lines[cy] + cx, (char)c);
                cx++;
            }

        /* ---- COMMAND ---- */
        } else {
            if (c == ESC) {
                mode = MODE_NORMAL;
            } else if (c == '\r' || c == '\n') {
                if (seq(cmd, "w")) {
                    save();
                } else if (seq(cmd, "q")) {
                    if (modified)
                        scopy(msg, "unsaved changes -- use :q! to force", sizeof(msg));
                    else
                        { clrscr(); exit(0); }
                } else if (seq(cmd, "q!")) {
                    clrscr(); exit(0);
                } else if (seq(cmd, "wq") || seq(cmd, "x")) {
                    save(); clrscr(); exit(0);
                } else {
                    scopy(msg, "unknown command", sizeof(msg));
                }
                mode = MODE_NORMAL;
            } else if (c == '\b') {
                if (cmd_len > 0) cmd[--cmd_len] = '\0';
            } else if (c >= 0x20 && c < 0x7F && cmd_len < 30) {
                cmd[cmd_len++] = (char)c;
                cmd[cmd_len]   = '\0';
            }
        }

        scroll_to_cursor();
        redraw();
    }
}
