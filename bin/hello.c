/* hello.c — first user-space program for YOLO-OS */

#define SYS_EXIT  0
#define SYS_WRITE 1
#define FD_STDOUT 1

/* Forward declarations so main() comes first in .text. */
static inline void print(const char *s);
static inline void sys_exit(void);

void main(void)
{
    print("Hello from ring 0!\n");
    sys_exit();
}

static inline void print(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_WRITE), "b"(FD_STDOUT), "c"(s), "d"(len)
        : "memory"
    );
}

static inline void sys_exit(void)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_EXIT)
        : "memory"
    );
}
