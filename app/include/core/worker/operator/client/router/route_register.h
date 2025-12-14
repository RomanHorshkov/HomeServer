/**
 * @file    route_register.h
 * 
 * @brief   HTTP route registration API for operator client path.
 * Provides macros and functions to register HTTP routes and their handlers.
 * 
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    dec 2025
 * (c) 2025
 */
#ifndef SERVER_ROUTER_ROUTE_REGISTER_H
#define SERVER_ROUTER_ROUTE_REGISTER_H

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */

#include <stddef.h>
#include "http_common.h"

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * TYPEDEFS
 ****************************************************************************
 */

/* ---------- Handler signature ---------- */
typedef int (*route_handler_t)(const http_request_t *, http_response_t *);

/****************************************************************************
 * ENUMERATED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPEDEFS
 ****************************************************************************
 */

typedef struct
{
    const char *path; /* string literal, never freed      */
    size_t path_len;  /* cache of strlen(path)            */
    route_handler_t handler;
} route_t;

/****************************************************************************
 * STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

void router_register(const char *path, route_handler_t h);

const route_t *router_get_table(size_t *out_count);

#define REGISTER_ROUTE(PATH, HANDLER)                              \
    /* 1.  Each translation unit gets its own constructor. */      \
    static void _reg_##HANDLER(void) __attribute__((constructor)); \
                                                                   \
    /* 2.  The constructor actually calls router_register(). */    \
    static void _reg_##HANDLER(void)                               \
    {                                                              \
        router_register(PATH, HANDLER);                            \
    }

#endif /* SERVER_ROUTER_ROUTE_REGISTER_H */
