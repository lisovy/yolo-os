/*
 * mkdir.c - create a directory
 */

#include "os.h"

void main(void)
{
    const char *name = get_args();
    if (!name || !name[0]) {
        print("mkdir: usage: mkdir <name>\n");
        exit(1);
    }
    if (os_mkdir(name) < 0)
        print("mkdir: failed\n");
    exit(0);
}
