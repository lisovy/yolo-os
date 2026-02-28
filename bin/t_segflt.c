/* segfault.c — deliberately accesses a kernel-only address to trigger #PF */

#include "os.h"

void main(void)
{
    volatile int *p = (volatile int *)0x1000;   /* kernel address, U=0 */
    *p = 0x42;                                  /* write → page fault   */
    /* unreachable */
    exit(0);
}
