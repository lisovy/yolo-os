/*
 * ls.c - list directory contents
 */

#include "os.h"

#define LS_MAX 64

static struct direntry entries[LS_MAX];

static int str_lt(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a < (unsigned char)*b;
}

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

void main(void)
{
    const char *arg = get_args();
    if (arg && arg[0]) {
        if (chdir(arg) < 0) {
            print("ls: not found: ");
            print(arg);
            write(STDOUT, "\n", 1);
            exit(1);
        }
    }

    int n = readdir(entries, LS_MAX);
    if (n < 0) {
        print("ls: disk error\n");
        exit(1);
    }

    /* Bubble sort: directories first, then alphabetical */
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            struct direntry *a = &entries[j];
            struct direntry *b = &entries[j + 1];
            int swap = (a->is_dir < b->is_dir) ||
                       (a->is_dir == b->is_dir && str_lt(b->name, a->name));
            if (swap) {
                struct direntry tmp = *a; *a = *b; *b = tmp;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        print(entries[i].name);
        if (entries[i].is_dir) {
            write(STDOUT, "/", 1);
        } else {
            char sizebuf[12];
            uint_to_str(entries[i].size, sizebuf);
            print("  ");
            print(sizebuf);
        }
        write(STDOUT, "\n", 1);
    }
    exit(0);
}
