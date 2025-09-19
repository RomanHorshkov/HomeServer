/* backend/route_registry.c */
#include "route_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 3.1  The dynamic vector ---------- */
static route_t *vec   = NULL;
static size_t   used  = 0; /* items filled */
static size_t   alloc = 0; /* items allocated */

void router_register(const char *path, route_handler_t handler)
{
    if(used == alloc)
    { /* grow vector (8, 16, 32 …) */
        alloc = alloc ? alloc * 2 : 8;
        vec   = realloc(vec, alloc * sizeof *vec);
        if(!vec)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
    }

    vec[used++] =
        (route_t){.path = path, .path_len = strlen(path), .handler = handler};
}

const route_t *router_get_table(size_t *out_count)
{
    if(out_count)
        *out_count = used;
    return vec; /* read‑only outside this translation unit */
}
