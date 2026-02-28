/*
 * pmm.c - Physical Memory Manager
 *
 * Bitmap allocator over physical frames 0x100000..0x7FFFFFF (127 MB).
 * Frame size: 4096 bytes.
 * Total frames: (0x8000000 - 0x100000) / 0x1000 = 0x7F00 = 32512
 * Bitmap: 32512 / 32 = 1016 unsigned ints = 4064 bytes in BSS.
 *
 * Bit = 0 → frame is free; bit = 1 → frame is used.
 * Frame index 0 corresponds to physical address PMM_BASE (0x100000).
 */

#include "pmm.h"

#define PMM_BASE        0x100000u       /* first managed physical address */
#define PMM_END         0x8000000u      /* one past last managed address   */
#define PMM_FRAME_SIZE  0x1000u         /* 4 KB per frame                  */
#define PMM_TOTAL_FRAMES ((PMM_END - PMM_BASE) / PMM_FRAME_SIZE)  /* 32512 */
#define PMM_BITMAP_WORDS ((PMM_TOTAL_FRAMES + 31) / 32)           /* 1016  */

static unsigned int pmm_bitmap[PMM_BITMAP_WORDS];

/* Mark a frame as used */
static void pmm_set(unsigned int frame)
{
    pmm_bitmap[frame / 32] |= (1u << (frame % 32));
}

/* Mark a frame as free */
static void pmm_clear(unsigned int frame)
{
    pmm_bitmap[frame / 32] &= ~(1u << (frame % 32));
}

/* Test whether a frame is used */
static int pmm_test(unsigned int frame)
{
    return (pmm_bitmap[frame / 32] >> (frame % 32)) & 1;
}

void pmm_init(void)
{
    unsigned int i;
    /* All frames start as free (bitmap zeroed by BSS initialisation) */
    for (i = 0; i < PMM_BITMAP_WORDS; i++)
        pmm_bitmap[i] = 0;
}

/*
 * Allocate one physical frame.
 * Returns the physical address of the frame, or 0 on failure.
 */
unsigned int pmm_alloc(void)
{
    unsigned int i, bit;
    for (i = 0; i < PMM_BITMAP_WORDS; i++) {
        if (pmm_bitmap[i] == 0xFFFFFFFFu)
            continue;
        /* Find first free bit in this word */
        for (bit = 0; bit < 32; bit++) {
            unsigned int frame = i * 32 + bit;
            if (frame >= PMM_TOTAL_FRAMES)
                return 0;
            if (!pmm_test(frame)) {
                pmm_set(frame);
                return PMM_BASE + frame * PMM_FRAME_SIZE;
            }
        }
    }
    return 0;
}

/*
 * Allocate n contiguous physical frames.
 * Returns the physical address of the first frame, or 0 on failure.
 */
unsigned int pmm_alloc_contiguous(int n)
{
    unsigned int start, count, frame;

    if (n <= 0) return 0;

    start = 0;
    count = 0;
    for (frame = 0; frame < PMM_TOTAL_FRAMES; frame++) {
        if (!pmm_test(frame)) {
            if (count == 0) start = frame;
            count++;
            if ((int)count == n) {
                /* Found a run of n free frames — mark them all used */
                unsigned int f;
                for (f = start; f < start + (unsigned int)n; f++)
                    pmm_set(f);
                return PMM_BASE + start * PMM_FRAME_SIZE;
            }
        } else {
            count = 0;
        }
    }
    return 0;
}

/*
 * Free a single physical frame previously returned by pmm_alloc /
 * pmm_alloc_contiguous.
 */
void pmm_free(unsigned int pa)
{
    if (pa < PMM_BASE || pa >= PMM_END) return;
    unsigned int frame = (pa - PMM_BASE) / PMM_FRAME_SIZE;
    pmm_clear(frame);
}
