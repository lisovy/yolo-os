/*
 * mv.c - rename a file or directory
 */

#include "os.h"

void main(void)
{
    const char *args = get_args();
    if (!args || !args[0]) {
        print("mv: usage: mv <src> <dst>\n");
        exit(1);
    }

    char src[13], dst[13];
    int i = 0, j = 0;

    while (args[i] && args[i] != ' ' && i < 12) { src[i] = args[i]; i++; }
    src[i] = '\0';
    if (args[i] != ' ' || !src[0]) {
        print("mv: usage: mv <src> <dst>\n");
        exit(1);
    }
    i++;
    while (args[i] && j < 12) { dst[j++] = args[i++]; }
    dst[j] = '\0';
    if (!dst[0]) {
        print("mv: usage: mv <src> <dst>\n");
        exit(1);
    }

    if (os_rename(src, dst) < 0)
        print("mv: failed\n");
    exit(0);
}
