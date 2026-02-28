/*
 * malloc_oob — accesses the heap area (HEAP_BASE = 0x440000) directly,
 * without calling malloc() first.  The page is not mapped, so this
 * triggers a page fault → "Segmentation fault" → returns to shell.
 */

#include "os.h"

void main(void)
{
    volatile char *p = (volatile char *)HEAP_BASE;
    *p = 1;   /* page fault: heap not mapped without sbrk */
    /* should never reach here */
    print("ERROR: expected segfault did not occur\n");
    exit(1);
}
