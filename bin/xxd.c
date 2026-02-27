/* xxd.c — minimal hexdump utility for YOLO-OS
 *
 * Usage: run xxd <file>
 *
 * Output format (16 bytes per line):
 *   00000000: 4865 6c6c 6f2c 2077 6f72 6c64 210a       Hello, world!.
 */

#include "os.h"

static const char HEX[] = "0123456789abcdef";

static void put_hex_byte(unsigned char b)
{
    char buf[2];
    buf[0] = HEX[b >> 4];
    buf[1] = HEX[b & 0x0f];
    write(STDOUT, buf, 2);
}

static void put_offset(unsigned int off)
{
    char buf[8];
    int i;
    for (i = 7; i >= 0; i--) {
        buf[i] = HEX[off & 0x0f];
        off >>= 4;
    }
    write(STDOUT, buf, 8);
}

void main(void)
{
    const char *filename = get_args();
    if (!filename || !filename[0]) {
        print("usage: run xxd <file>\n");
        exit(1);
    }

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("xxd: cannot open: ");
        print(filename);
        print("\n");
        exit(1);
    }

    unsigned char buf[16];
    unsigned int offset = 0;

    for (;;) {
        int n = read(fd, (char *)buf, 16);
        if (n <= 0) break;

        /* offset */
        put_offset(offset);
        print(": ");

        /* hex bytes — 8 groups of 2 bytes */
        int i;
        for (i = 0; i < 16; i += 2) {
            if (i > 0) write(STDOUT, " ", 1);
            if (i     < n) { put_hex_byte(buf[i]);     } else { print("  "); }
            if (i + 1 < n) { put_hex_byte(buf[i + 1]); } else { print("  "); }
        }

        print("  ");

        /* ASCII sidebar */
        for (i = 0; i < n; i++) {
            unsigned char c = buf[i];
            char ch = (c >= 0x20 && c <= 0x7e) ? (char)c : '.';
            write(STDOUT, &ch, 1);
        }

        print("\n");
        offset += (unsigned int)n;
    }

    close(fd);
    exit(0);
}
