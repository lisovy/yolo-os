/*
 * malloc_oob — allocates a 4 KB buffer with malloc(), writes within bounds,
 * then deliberately overflows past the allocation boundary until a page fault
 * occurs → "Segmentation fault" → returns to shell.
 */

#include "os.h"
#include "malloc.h"

void main(void)
{
    char *buf = (char *)malloc(4096);
    if (!buf) {
        print("ERROR: malloc returned NULL\n");
        exit(1);
    }

    /* In-bounds writes: fill the entire 4 KB allocation */
    unsigned int i;
    for (i = 0; i < 4096; i++)
        buf[i] = (char)i;

    /* Overflow: write past the allocated 4 KB until a page fault fires */
    for (;; i++)
        buf[i] = (char)i;

    /* should never reach here */
    print("ERROR: expected segfault did not occur\n");
    exit(1);
}
