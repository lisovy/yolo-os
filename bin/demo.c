/* demo.c — VGA Mode 13h snow effect for YOLO-OS
 *
 * Switches to 320x200 256-colour graphics mode and fills the
 * framebuffer with random black/white pixels ("TV snow").
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
#define VGA_DAC_W   0x3C8
#define VGA_DAC_D   0x3C9

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

void main(void)
{
    set_mode13h();

    volatile unsigned char *fb = (volatile unsigned char *)FB_BASE;

    for (;;) {
        /* Randomise the entire framebuffer each pass */
        for (int i = 0; i < FB_SIZE; i++)
            fb[i] = (unsigned char)(rand_next() & 1); /* 0 = black, 1 = white */

        /* Check for 'q' without blocking the animation */
        int c = get_char_nonblock();
        if (c == 'q' || c == 'Q')
            exit(0);
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

    /* DAC palette: index 0 = black, index 1 = white */
    outb(VGA_DAC_W, 0);
    outb(VGA_DAC_D, 0);  outb(VGA_DAC_D, 0);  outb(VGA_DAC_D, 0);
    outb(VGA_DAC_W, 1);
    outb(VGA_DAC_D, 63); outb(VGA_DAC_D, 63); outb(VGA_DAC_D, 63);

    /* Clear framebuffer to black */
    volatile unsigned char *fb = (volatile unsigned char *)FB_BASE;
    for (int i = 0; i < FB_SIZE; i++)
        fb[i] = 0;
}
