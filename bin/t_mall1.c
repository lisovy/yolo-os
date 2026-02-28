/*
 * malloc_test — exercises malloc() and free():
 *   1. Basic allocation, write, read-back
 *   2. Multiple allocations (int array)
 *   3. free() + reallocation (first-fit reuse)
 *   4. Large allocation spanning multiple pages
 *   5. Over-limit request returns NULL (no crash)
 */

#include "os.h"
#include "malloc.h"

static void fail(const char *msg)
{
    print("FAIL: ");
    print(msg);
    print("\n");
    exit(1);
}

void main(void)
{
    int i;

    /* 1. Basic alloc + write + verify */
    char *p = malloc(64);
    if (!p) fail("malloc(64) returned NULL");
    for (i = 0; i < 64; i++) p[i] = (char)(i & 0xFF);
    for (i = 0; i < 64; i++)
        if (p[i] != (char)(i & 0xFF)) fail("data corruption in basic alloc");
    print("alloc+write: ok\n");

    /* 2. Multiple concurrent allocations */
    int *q = (int *)malloc(16 * sizeof(int));
    if (!q) fail("malloc int array");
    for (i = 0; i < 16; i++) q[i] = i * i;
    for (i = 0; i < 16; i++)
        if (q[i] != i * i) fail("int array corruption");
    print("multi-alloc: ok\n");

    /* 3. free + reuse: free p, then malloc same size — must not crash */
    free(p);
    char *p2 = malloc(64);
    if (!p2) fail("malloc after free returned NULL");
    for (i = 0; i < 64; i++) p2[i] = (char)(i ^ 0xAA);
    for (i = 0; i < 64; i++)
        if (p2[i] != (char)(i ^ 0xAA)) fail("data corruption after reuse");
    print("free+reuse: ok\n");

    /* 4. Large allocation spanning several pages */
    char *big = (char *)malloc(12288);   /* 3 pages worth */
    if (!big) fail("malloc(12288) returned NULL");
    for (i = 0; i < 12288; i++) big[i] = (char)(i & 0x7F);
    for (i = 0; i < 12288; i++)
        if (big[i] != (char)(i & 0x7F)) fail("large alloc corruption");
    print("large-alloc: ok\n");

    /* 5. Request more than entire heap can hold — must return NULL, not crash */
    void *huge = malloc(0x400000);   /* 4 MB > heap capacity */
    if (huge != (void *)0) fail("over-limit malloc did not return NULL");
    print("exhaustion: ok\n");

    print("malloc: OK\n");
    exit(0);
}
