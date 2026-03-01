/*
 * t_exec — stress-test exec() by spawning 300 child processes sequentially.
 *
 * Verifies that process slot recycling works correctly across many
 * create/destroy cycles.  Prints "exec: OK" on success.
 */

#include "os.h"

void main(void)
{
    int i;
    for (i = 0; i < 300; i++) {
        if (exec("hello", "") < 0) {
            print("exec: FAIL\n");
            exit(1);
        }
    }
    print("exec: OK\n");
    exit(0);
}
