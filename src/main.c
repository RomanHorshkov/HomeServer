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

#include "core.h" /* server_init / run / shutdown                        */
#include "logger.h" /* loginfo / log_error / logger_init                  */

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
        /* do nothing, server not initialized */
    }
    else
    {
        log_info("Server started on port %s...", port);
        server_run();
    }

    return 0;
}
