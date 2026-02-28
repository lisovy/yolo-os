/*
 * free.c - display physical and virtual memory usage
 *
 * Output (all values in kB):
 *
 *          total       used       free
 * Phys:   130048     1200    128848
 * Virt:     8192      568      7624   (2 procs)
 */

#include "os.h"

/* Write a right-justified decimal number in a field of `width` chars. */
static void print_num(unsigned int n, int width)
{
    char buf[12];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else { while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; } }

    /* pad with spaces on the left */
    int digits = i;
    for (int sp = digits; sp < width; sp++) write(STDOUT, " ", 1);

    /* print digits in correct order */
    for (int d = digits - 1; d >= 0; d--)
        write(STDOUT, &buf[d], 1);
}

void main(void)
{
    struct meminfo m;
    if (meminfo(&m) < 0) {
        print("free: meminfo failed\n");
        exit(1);
    }

    print("         total       used       free\n");

    print("Phys:  ");
    print_num(m.phys_total_kb, 8);
    print(" kB");
    print_num(m.phys_used_kb,  8);
    print(" kB");
    print_num(m.phys_free_kb,  8);
    print(" kB\n");

    print("Virt:  ");
    print_num(m.virt_total_kb, 8);
    print(" kB");
    print_num(m.virt_used_kb,  8);
    print(" kB");
    print_num(m.virt_free_kb,  8);
    print(" kB");

    /* append process count */
    print("   (");
    print_num((unsigned int)m.n_procs, 1);
    print(m.n_procs == 1 ? " proc)\n" : " procs)\n");

    exit(0);
}
