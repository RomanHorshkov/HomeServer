#ifndef SERVER_ROUTER_ROUTE_REGISTER_H
#define SERVER_ROUTER_ROUTE_REGISTER_H

#include <stddef.h>

#include "http_manager.h" /* <- pulls in the typedefs; safe here,
                               no circular dependency because this header
                               contains *no* struct definitions */

/* ---------- 2.2  Handler signature ---------- */
typedef int (*route_handler_t)(const HttpRequest *, HttpResponse *);

/* ---------- 2.3  The metadata struct stored in the vector ---------- */
typedef struct
{
    const char *path; /* string literal, never freed      */
    size_t path_len;  /* cache of strlen(path)            */
    route_handler_t handler;
} route_t;

/* ---------- 2.4  Functions exposed to router.c ---------- */
void router_register(const char *p, route_handler_t h);

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
