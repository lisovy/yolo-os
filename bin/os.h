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

/* Syscall numbers — filesystem and process */
#define SYS_READDIR  9
#define SYS_UNLINK  10
#define SYS_MKDIR   11
#define SYS_RENAME  12
#define SYS_EXEC    13
#define SYS_CHDIR   14
#define SYS_GETPOS  15
#define SYS_PANIC   16
#define SYS_MEMINFO 17
#define SYS_SBRK    18
#define SYS_SLEEP   19

struct direntry { char name[13]; unsigned int size; int is_dir; };

struct meminfo {
    unsigned int phys_total_kb;
    unsigned int phys_used_kb;
    unsigned int phys_free_kb;
    unsigned int virt_total_kb;
    unsigned int virt_used_kb;
    unsigned int virt_free_kb;
    int          n_procs;
};

/* Arrow key codes returned by get_char() */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

/* Program argument string — written by kernel before exec_run() */
#define ARGS_BASE  0x7FC000
#define HEAP_BASE  0x440000   /* first heap virtual address (right after binary) */
static inline const char *get_args(void) { return (const char *)ARGS_BASE; }

/* Memory utility */
static inline void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

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

/* Extend heap by n bytes; returns old break address, or (void*)-1 on failure */
static inline void *sbrk(unsigned int n)
{
    return (void *)syscall(SYS_SBRK, (int)n, 0, 0);
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
/* Read directory entries into buf; returns count */
static inline int readdir(struct direntry *buf, int max)
    { return syscall(SYS_READDIR, (int)buf, max, 0); }
/* Delete file or empty directory */
static inline int unlink(const char *n)
    { return syscall(SYS_UNLINK, (int)n, 0, 0); }
/* Create directory */
static inline int mkdir(const char *n)
    { return syscall(SYS_MKDIR, (int)n, 0, 0); }
/* Rename file or directory */
static inline int rename(const char *s, const char *d)
    { return syscall(SYS_RENAME, (int)s, (int)d, 0); }
/* exec() flags for EDX (SYS_EXEC third argument) */
#define EXEC_FG  0   /* foreground: shell waits for child to finish */
#define EXEC_BG  1   /* background: shell continues immediately      */

/* Execute a program from /bin; blocks until child exits; returns exit code or -1 */
static inline int exec(const char *name, const char *args)
    { return syscall(SYS_EXEC, (int)name, (int)args, EXEC_FG); }
/* Execute a program in the background; returns child PID or -1 */
static inline int exec_bg(const char *name, const char *args)
    { return syscall(SYS_EXEC, (int)name, (int)args, EXEC_BG); }
/* Change current working directory */
static inline int chdir(const char *name)
    { return syscall(SYS_CHDIR, (int)name, 0, 0); }
/* Get cursor position: high byte = row, low byte = col */
static inline int getpos(void)
    { return syscall(SYS_GETPOS, 0, 0, 0); }
static inline void kernel_panic(const char *msg)
    { syscall(SYS_PANIC, (int)msg, 0, 0); }
static inline int meminfo(struct meminfo *info)
    { return syscall(SYS_MEMINFO, (int)info, 0, 0); }
/* Sleep for at least ms milliseconds (granularity: 10 ms at 100 Hz) */
static inline int sleep(unsigned int ms)
    { return syscall(SYS_SLEEP, (int)ms, 0, 0); }

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
