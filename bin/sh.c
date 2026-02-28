/*
 * sh.c - YOLO-OS user-space shell
 *
 * Runs as the first user process (loaded from /bin/sh by the kernel).
 * Supports: inline editing with arrow keys, cd, __exit, and running
 * any program found in /bin by name.
 */

#include "os.h"

#define VGA_COLS  80
#define VGA_ROWS  25
#define VGA_MEM   0xB8000

#define COLOR_DEFAULT  0x07   /* light gray on black */
#define COLOR_PROMPT   0x0A   /* light green on black */

#define CMD_MAX   79
#define CWD_MAX   64

/* ── small helpers ────────────────────────────────────────────────────── */

static int sh_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}


static void sh_print(const char *s)
{
    write(STDOUT, s, sh_strlen(s));
}

/* Write a single character to stdout */
static void sh_putchar(char c)
{
    write(STDOUT, &c, 1);
}

/* ── VGA direct access (0xB8000 is user-accessible) ─────────────────── */

static void vga_putc(int row, int col, char c, unsigned char attr)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_MEM;
    vga[row * VGA_COLS + col] = ((unsigned short)attr << 8) | (unsigned char)c;
}

/* Print string via write() (so serial/tests see it), then recolor in VGA */
static void sh_print_colored(const char *s, unsigned char attr)
{
    int pos = getpos();
    int row = pos >> 8;
    int col = pos & 0xFF;
    sh_print(s);          /* write() → VGA default color + serial */
    while (*s) {          /* recolor the characters we just wrote */
        vga_putc(row, col, *s++, attr);
        col++;
        if (col >= VGA_COLS) { col = 0; row++; }
    }
    /* cursor already at correct position after sh_print() */
}

/* Redraw the command line in-place without printing to avoid scrolling. */
static void redraw_line(const char *cmd, int cmd_len, int cursor_pos,
                        int prompt_row, int prompt_col)
{
    for (int i = 0; i < cmd_len; i++)
        vga_putc(prompt_row, prompt_col + i, cmd[i], COLOR_DEFAULT);
    /* Erase the cell just past the end (handles deleted chars) */
    if (prompt_col + cmd_len < VGA_COLS)
        vga_putc(prompt_row, prompt_col + cmd_len, ' ', COLOR_DEFAULT);
    set_pos(prompt_row, prompt_col + cursor_pos);
}

/* ── cwd tracking ────────────────────────────────────────────────────── */

static char cwd_path[CWD_MAX] = "";

static void update_cwd(const char *name)
{
    if (name[0] == '/' && !name[1]) {
        cwd_path[0] = '\0';
        return;
    }
    if (name[0] == '.' && name[1] == '.' && !name[2]) {
        int len = sh_strlen(cwd_path);
        if (len > 0) {
            while (len > 0 && cwd_path[len - 1] != '/') len--;
            if (len > 0) len--;
        }
        cwd_path[len] = '\0';
        return;
    }
    /* Append /name */
    int len = sh_strlen(cwd_path);
    if (len < CWD_MAX - 2) {
        cwd_path[len++] = '/';
        int i = 0;
        while (name[i] && len < CWD_MAX - 1)
            cwd_path[len++] = name[i++];
        cwd_path[len] = '\0';
    }
}

/* ── shell main ─────────────────────────────────────────────────────── */

void main(void)
{
    char cmd[CMD_MAX + 1];
    int  cmd_len   = 0;
    int  cursor_pos = 0;

    for (;;) {
        /* Print prompt (green) */
        if (cwd_path[0]) {
            sh_print_colored(cwd_path, COLOR_PROMPT);
        }
        sh_print_colored("> ", COLOR_PROMPT);

        /* Record where the editable part starts */
        int pos = getpos();
        int prompt_row = pos >> 8;
        int prompt_col = pos & 0xFF;

        cmd_len    = 0;
        cursor_pos = 0;

        /* Read and edit command line */
        for (;;) {
            int c = get_char();
            if (!c) continue;

            if (c == KEY_LEFT) {
                if (cursor_pos > 0) {
                    cursor_pos--;
                    set_pos(prompt_row, prompt_col + cursor_pos);
                }
                continue;
            }
            if (c == KEY_RIGHT) {
                if (cursor_pos < cmd_len) {
                    cursor_pos++;
                    set_pos(prompt_row, prompt_col + cursor_pos);
                }
                continue;
            }
            if (c == KEY_UP || c == KEY_DOWN)
                continue;

            if (c == '\b') {
                if (cursor_pos > 0) {
                    for (int i = cursor_pos - 1; i < cmd_len - 1; i++)
                        cmd[i] = cmd[i + 1];
                    cmd_len--;
                    cursor_pos--;
                    redraw_line(cmd, cmd_len, cursor_pos, prompt_row, prompt_col);
                }
                continue;
            }

            if (c == '\n') {
                /* Move cursor to end of line before newline */
                set_pos(prompt_row, prompt_col + cmd_len);
                sh_putchar('\n');
                cmd[cmd_len] = '\0';
                break;
            }

            /* Printable character — insert at cursor_pos */
            if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F &&
                    cmd_len < CMD_MAX &&
                    prompt_col + cmd_len < VGA_COLS - 1) {
                for (int i = cmd_len; i > cursor_pos; i--)
                    cmd[i] = cmd[i - 1];
                cmd[cursor_pos] = (char)c;
                cmd_len++;
                cursor_pos++;
                redraw_line(cmd, cmd_len, cursor_pos, prompt_row, prompt_col);
            }
        }

        if (!cmd[0])
            continue;

        /* __ exit: signal QEMU to exit (automated tests) */
        if (cmd[0] == '_' && cmd[1] == '_' &&
            cmd[2] == 'e' && cmd[3] == 'x' && cmd[4] == 'i' &&
            cmd[5] == 't' && !cmd[6]) {
            outb(0xF4, 0x31);
            continue;
        }

        /* clear */
        if (cmd[0]=='c' && cmd[1]=='l' && cmd[2]=='e' && cmd[3]=='a' &&
            cmd[4]=='r' && !cmd[5]) {
            clrscr();
            continue;
        }

        /* exit */
        if (cmd[0]=='e' && cmd[1]=='x' && cmd[2]=='i' && cmd[3]=='t' && !cmd[4]) {
            exit(0);
        }

        /* cd [name] */
        {
            int is_cd = (cmd[0] == 'c' && cmd[1] == 'd');
            if (is_cd && (!cmd[2] || cmd[2] == ' ')) {
                const char *arg = "";
                if (cmd[2] == ' ' && cmd[3]) arg = &cmd[3];
                if (!arg[0]) arg = "/";
                if (chdir(arg) < 0)
                    sh_print("cd: not found\n");
                else
                    update_cwd(arg);
                continue;
            }
        }

        /* Any other word: parse "prog [args]" and exec */
        char prog[14];
        int  pi = 0;
        while (cmd[pi] && cmd[pi] != ' ' && pi < 13) { prog[pi] = cmd[pi]; pi++; }
        prog[pi] = '\0';
        const char *args = (cmd[pi] == ' ') ? &cmd[pi + 1] : "";

        int ret = exec(prog, args);
        if (ret < 0)
            sh_print("unknown command\n");
    }
}
