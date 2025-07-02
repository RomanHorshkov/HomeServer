/**
 * @file main.c
 * @brief Stand-alone entry point for the micro-HTTP server.
 *
 * This translation unit contains **only** the `main()` function; all other
 * concerns (logging, socket management, request handling, etc.) are encapsulated
 * behind @ref core.h, ensuring the program’s high-level flow remains
 * crystal clear and maintainable.
 *
 * ```
 *  ┌─────────────┐   init   ┌───────────┐
 *  │   main()    │ ───────▶ │  server   │
 *  │  (this TU)  │          │   core    │
 *  └─────────────┘ ◀─────── └───────────┘
 *        ▲                    ▲   ▲
 *        │  run / shutdown    │   └─ listener / worker / …
 *        └────────────────────┘
 * ```
 *
 * ### Exit Status
 * | Code | Meaning                                    |
 * |------|--------------------------------------------|
 * | 0    | Clean shutdown (user typed `'q'`, SIGINT…) |
 * | 1    | Core initialization failed                 |
 *
 * ### Command-Line Arguments
 * For simplicity, the listening port is currently hard-wired to **3490**.
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025‑05‑11
 * (c) 2025
 */

#include <stdio.h>  /* printf / fprintf / perror                          */
#include <unistd.h> /* chdir                                              */

#include "core.h"   /* server_init / run / shutdown                       */
#include "logger.h" /* loginfo / log_error / logger_init                  */



/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define CHDIR_PATH "/srv/HomeServer/www"

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int main(int argc, char *argv[])
{
    /* Return variable */
    int res = -1;

    /* Check input */
    if(argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
    }

    /* Set working directory to var/www */
    else if(chdir(CHDIR_PATH) != 0)
    {
        printf("chdir to %s failed %s", CHDIR_PATH, strerror(errno));
    }

    /* Initialize the server */
    else if(server_init(argv[1]) == -1)
    {
        /* do nothing, server not initialized */
    }

    /* Run the server */
    else
    {
        log_info("Server started on port %s...", argv[1]);
        server_run();

        /* Set return variable to success */
        res = 0;
    }

    return res;
}
