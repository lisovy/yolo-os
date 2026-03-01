/*
 * t_sleep — test sleep() syscall: sleeps 1 second and prints confirmation.
 */

#include "os.h"

void main(void)
{
    print("sleeping 1 s ...\n");
    int r = sleep(1000);
    if (r != 0) {
        print("sleep: FAIL (non-zero return)\n");
        exit(1);
    }
    print("sleep: OK\n");
    exit(0);
}
