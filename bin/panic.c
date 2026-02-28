/*
 * panic.c - trigger a kernel panic with an optional message
 *
 * Usage: panic [message]
 * If no message is given, uses "user-requested panic".
 */

#include "os.h"

void main(void)
{
    const char *msg = get_args();
    if (!msg || !msg[0])
        msg = "user-requested panic";
    kernel_panic(msg);
}
