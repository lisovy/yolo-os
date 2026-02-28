/*
 * rm.c - remove a file or empty directory
 */

#include "os.h"

void main(void)
{
    const char *name = get_args();
    if (!name || !name[0]) {
        print("rm: usage: rm <name>\n");
        exit(1);
    }

    print("rm: delete '");
    print(name);
    print("'? [y/N] ");

    int c = get_char();
    char ch = (char)c;
    write(STDOUT, &ch, 1);
    write(STDOUT, "\n", 1);

    if (c != 'y' && c != 'Y') {
        exit(0);
    }

    int r = unlink(name);
    if (r == -2)
        print("rm: directory not empty\n");
    else if (r < 0)
        print("rm: not found\n");
    exit(0);
}
