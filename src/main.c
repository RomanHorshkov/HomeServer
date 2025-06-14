/**
 * @file main.c
 * @brief Stand‑alone entry point for the micro‑HTTP server.
 *
 * The translation unit contains **only** the `main()` function; every other
 * concern (logging, socket management, request handling, …) is tucked away
 * behind @ref core.h so the program’s high‑level flow stays crystal clear:
 *
 * ```text
 *  ┌─────────────┐   init   ┌───────────┐
 *  │   main()    │ ───────▶ │  server   │
 *  │  (this TU)  │          │   core    │
 *  └─────────────┘ ◀─────── └───────────┘
 *        ▲                    ▲   ▲
 *        │  run / shutdown    │   └─ listener / client‑manager / …
 *        └────────────────────┘
 * ```
 *
 * ### Exit status
 * | Code | Meaning                                    |
 * |------|--------------------------------------------|
 * | 0    | Clean shutdown (user typed `'q'`, SIGINT…) |
 * | 1    | Core initialisation failed                 |
 *
 * ### Command‑line arguments
 * For simplicity the listening port is currently hard‑wired to **3490**.
 */

#define _GNU_SOURCE /* for completeness                                    */

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

    log_info("Starting server on port %s...", port);

    if(server_init(port) == -1)
    {
        /* do nothing, server not initialized */
    }
    else
    {
        log_info("Server initialized successfully. Starting...");
        server_run();
    }

    return 0;
}
