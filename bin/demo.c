/* demo.c — VGA Mode 13h snow effect + PC speaker beeps for YOLO-OS
 *
 * Switches to 320x200 256-colour graphics mode and fills the
 * framebuffer with random black/white pixels ("TV snow").
 * PC speaker beeps twice per second (80 ms on, 170 ms off, repeat).
 * Press 'q' to quit.
 *
 * The kernel automatically restores text mode after exit
 * (program_exec → vga_restore_textmode), so this program does
 * not need to reset the VGA hardware itself.
 */

#include "os.h"

#define FB_BASE   0xA0000
#define FB_WIDTH  320
#define FB_HEIGHT 200
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT)   /* 64 000 bytes */

/* VGA register ports */
#define VGA_MISC_W  0x3C2
#define VGA_SEQ_I   0x3C4
#define VGA_SEQ_D   0x3C5
#define VGA_CRTC_I  0x3D4
#define VGA_CRTC_D  0x3D5
#define VGA_GC_I    0x3CE
#define VGA_GC_D    0x3CF
#define VGA_AC      0x3C0
#define VGA_INSTAT  0x3DA

/* PC speaker / PIT */
#define PIT_CH0     0x40   /* channel 0 data (read for timing)  */
#define PIT_CH2     0x42   /* channel 2 data (write for tone)   */
#define PIT_CMD     0x43   /* PIT command register              */
#define SPEAKER_CTL 0x61   /* PC speaker / NMI control port     */

static void set_mode13h(void);

/* XOR-shift PRNG */
static unsigned int rng_state = 0xDEADBEEF;

static unsigned int rand_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Read PIT channel 0 current count (counts down at 1 193 180 Hz). */
static unsigned short pit_count(void)
{
    outb(PIT_CMD, 0x00);              /* latch channel 0 */
    unsigned char lo = inb(PIT_CH0);
    unsigned char hi = inb(PIT_CH0);
    return (unsigned short)(lo | (hi << 8));
}

/* Wait approximately ms milliseconds using PIT channel 0.
 * Processes in <=27 ms chunks to stay within the ~55 ms wrap window. */
static void msleep(unsigned int ms)
{
    unsigned int ticks = ms * 1193u;
    while (ticks > 0) {
        unsigned int chunk = (ticks > 27000u) ? 27000u : ticks;
        unsigned short start = pit_count();
        while ((unsigned short)(start - pit_count()) < (unsigned short)chunk)
            ;
        ticks -= chunk;
    }
}

/* Turn PC speaker on at freq Hz (programs PIT channel 2, mode 3). */
static void speaker_on(unsigned int freq)
{
    unsigned int div = 1193180u / freq;
    outb(PIT_CMD, 0xB6);                        /* ch2, lo/hi, mode 3 */
    outb(PIT_CH2, (unsigned char)(div & 0xFF));
    outb(PIT_CH2, (unsigned char)(div >> 8));
    outb(SPEAKER_CTL, inb(SPEAKER_CTL) | 0x03); /* gate + enable      */
}

/* Turn PC speaker off. */
static void speaker_off(void)
{
    outb(SPEAKER_CTL, inb(SPEAKER_CTL) & ~0x03);
}

void main(void)
{
    set_mode13h();

    volatile unsigned char *fb = (volatile unsigned char *)FB_BASE;

    for (;;) {
        /* Randomise the entire framebuffer each pass */
        for (int i = 0; i < FB_SIZE; i++)
            fb[i] = (rand_next() & 1) ? 15 : 0; /* 0 = black, 15 = bright white */

        /* Two short beeps: on 80 ms, off 170 ms, on 80 ms, off 170 ms = 500 ms */
        speaker_on(1000);  msleep(80);
        speaker_off();     msleep(170);
        speaker_on(1000);  msleep(80);
        speaker_off();     msleep(170);

        int c = get_char_nonblock();
        if (c == 'q' || c == 'Q') { speaker_off(); exit(0); }
    }
}

static void set_mode13h(void)
{
    /* Miscellaneous output */
    outb(VGA_MISC_W, 0x63);

    /* Sequencer */
    outb(VGA_SEQ_I, 0x00); outb(VGA_SEQ_D, 0x03);
    outb(VGA_SEQ_I, 0x01); outb(VGA_SEQ_D, 0x01);
    outb(VGA_SEQ_I, 0x02); outb(VGA_SEQ_D, 0x0F);
    outb(VGA_SEQ_I, 0x03); outb(VGA_SEQ_D, 0x00);
    outb(VGA_SEQ_I, 0x04); outb(VGA_SEQ_D, 0x0E);

    /* CRTC: unlock write-protected registers, then write all 25 */
    outb(VGA_CRTC_I, 0x11); outb(VGA_CRTC_D, 0x0E);
    static const unsigned char crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(VGA_CRTC_I, (unsigned char)i);
        outb(VGA_CRTC_D, crtc[i]);
    }

    /* Graphics Controller */
    static const unsigned char gc[9] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
    };
    for (int i = 0; i < 9; i++) {
        outb(VGA_GC_I, (unsigned char)i);
        outb(VGA_GC_D, gc[i]);
    }

    /* Attribute Controller */
    static const unsigned char ac[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };
    inb(VGA_INSTAT);  /* reset flip-flop */
    for (int i = 0; i < 21; i++) {
        outb(VGA_AC, (unsigned char)i);
        outb(VGA_AC, ac[i]);
    }
    outb(VGA_AC, 0x20);  /* re-enable display */

    /* Clear framebuffer to black */
    volatile unsigned char *fb = (volatile unsigned char *)FB_BASE;
    for (int i = 0; i < FB_SIZE; i++)
        fb[i] = 0;
}
