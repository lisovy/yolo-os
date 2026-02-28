/*
 * malloc.h — simple first-fit free-list heap allocator for YOLO-OS user programs.
 *
 * Uses the SYS_SBRK syscall to map pages on demand from the heap region
 * (HEAP_BASE = 0x440000 up to 0x7F8000, ~3.7 MB).
 *
 * Each allocation is preceded by a 12-byte struct _blk header.
 * Include after os.h:
 *   #include "os.h"
 *   #include "malloc.h"
 */
#ifndef MALLOC_H
#define MALLOC_H

#include "os.h"

/* Block header: immediately precedes every allocation */
struct _blk {
    unsigned int  size;   /* payload size in bytes (not including header) */
    int           free;   /* 1 = available, 0 = in use                    */
    struct _blk  *next;   /* next block in list, NULL if last              */
};

#define _BLKHDR  ((unsigned int)sizeof(struct _blk))   /* 12 bytes */

/* Head of the free/used block list; NULL until first malloc() */
static struct _blk *_heap_head = (struct _blk *)0;

/*
 * malloc(size) — allocate size bytes and return a pointer to the payload.
 * Returns NULL on failure (out of heap space).
 */
static inline void *malloc(unsigned int size)
{
    if (size == 0) return (void *)0;

    /* Round up to 4-byte alignment */
    size = (size + 3u) & ~3u;

    /* Walk free list: first-fit search */
    struct _blk *b    = _heap_head;
    struct _blk *prev = (struct _blk *)0;
    while (b) {
        if (b->free && b->size >= size) {
            /* Split block if the leftover is large enough for a new header + 4 B */
            if (b->size >= size + _BLKHDR + 4u) {
                struct _blk *tail = (struct _blk *)((char *)b + _BLKHDR + size);
                tail->size = b->size - size - _BLKHDR;
                tail->free = 1;
                tail->next = b->next;
                b->next    = tail;
                b->size    = size;
            }
            b->free = 0;
            return (char *)b + _BLKHDR;
        }
        prev = b;
        b    = b->next;
    }

    /* No suitable free block — ask kernel for more heap pages */
    void *p = sbrk(_BLKHDR + size);
    if ((int)p == -1) return (void *)0;

    struct _blk *nb = (struct _blk *)p;
    nb->size = size;
    nb->free = 0;
    nb->next = (struct _blk *)0;

    if (!_heap_head)
        _heap_head = nb;
    else if (prev)
        prev->next = nb;

    return (char *)nb + _BLKHDR;
}

/*
 * free(ptr) — return the block at ptr to the free pool.
 * Coalesces forward with adjacent free blocks to reduce fragmentation.
 */
static inline void free(void *ptr)
{
    if (!ptr) return;
    struct _blk *b = (struct _blk *)((char *)ptr - _BLKHDR);
    b->free = 1;
    /* Forward coalesce */
    while (b->next && b->next->free) {
        b->size += _BLKHDR + b->next->size;
        b->next  = b->next->next;
    }
}

#endif /* MALLOC_H */
