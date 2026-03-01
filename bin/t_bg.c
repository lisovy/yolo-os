/*
 * t_bg.c - Background execution test
 *
 * Sleeps for 300 ms, then prints "bg: OK".
 * Run as: t_bg &
 * Expected: shell prompt returns immediately, "bg: OK" appears ~300 ms later.
 */

#include "os.h"

void main(void)
{
    sleep(300);
    print("bg: OK\n");
    exit(0);
}
