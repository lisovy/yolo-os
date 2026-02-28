/*
 * os.h - YOLO-OS interface for programs
 *
 * Syscall ABI: int 0x80, EAX=number, EBX/ECX/EDX=args, return value in EAX.
 * All functions are static inline — include this header, no linking needed.
 */

#ifndef OS_H
#define OS_H

/* Syscall numbers */
#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_READ  2
#define SYS_OPEN  3
#define SYS_CLOSE 4

/* Standard file descriptors */
#define STDIN  0
#define STDOUT 1

/* open() flags */
#define O_RDONLY 0
#define O_WRONLY 1

/* Syscall numbers — screen control */
#define SYS_GETCHAR          5
#define SYS_SETPOS           6
#define SYS_CLRSCR           7
#define SYS_GETCHAR_NONBLOCK 8   /* non-blocking; returns 0 if no key ready */

/* Arrow key codes returned by get_char() */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

/* Program argument string — written by kernel before exec_run() */
#define ARGS_BASE 0x3FF800
static inline const char *get_args(void) { return (const char *)ARGS_BASE; }

/* Raw syscall — up to 3 arguments */
static inline int syscall(int num, int a, int b, int c)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static inline void exit(int code)
{
    syscall(SYS_EXIT, code, 0, 0);
}

static inline int write(int fd, const char *buf, int len)
{
    return syscall(SYS_WRITE, fd, (int)buf, len);
}

static inline int read(int fd, char *buf, int len)
{
    return syscall(SYS_READ, fd, (int)buf, len);
}

static inline int open(const char *path, int flags)
{
    return syscall(SYS_OPEN, (int)path, flags, 0);
}

static inline int close(int fd)
{
    return syscall(SYS_CLOSE, fd, 0, 0);
}

/* Utility: string length */
static inline int strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Utility: write a null-terminated string to stdout */
static inline int print(const char *s)
{
    return write(STDOUT, s, strlen(s));
}

/* Blocking raw keyread — no echo, no line buffering */
static inline int get_char(void) { return syscall(SYS_GETCHAR, 0, 0, 0); }
/* Non-blocking raw keyread — returns 0 immediately if no key is ready */
static inline int get_char_nonblock(void) { return syscall(SYS_GETCHAR_NONBLOCK, 0, 0, 0); }
/* Move VGA cursor to (row, col) */
static inline void set_pos(int row, int col) { syscall(SYS_SETPOS, row, col, 0); }
/* Clear text area and home cursor */
static inline void clrscr(void) { syscall(SYS_CLRSCR, 0, 0, 0); }

/* Direct hardware port I/O (ring 0 only) */
static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#endif /* OS_H */
