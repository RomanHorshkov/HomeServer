/**
 * Server TCP implementation
 */

#include "core.h"                   // server's core, main brain

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int main()
{
    /* server's listening port */
    const char *port = "3490";

    if(server_init(port) == -1)
    {
        /* do nothing */
    }
    else
    {
        server_run();
        server_shutdown();
    }

    return 0;
}