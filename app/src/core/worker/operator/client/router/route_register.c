/**
 * @file route_register.c
 * @brief HTTP route registration implementation.
 * 
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    dec 2025
 * 
 * (c) 2025
 */

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */

#include "route_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_route_register"

/****************************************************************************
 * ENUMERATED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * VARIABLES
 ****************************************************************************
 */


static route_t *vec = NULL;
static uint8_t used = 0;  /* items filled */
static uint8_t alloc = 0; /* items allocated */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

void router_register(const char *path, route_handler_t handler)
{
    /* TODO: this is quite dangerous multiplying indefinitely on uint8_t */
    if(used == alloc)
    {
        /* grow vector (8, 16, 32 …) */
        alloc = alloc ? alloc * 2 : 2;
        vec = realloc(vec, alloc * sizeof *vec);
        if(!vec)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
    }

    /* Register new route */
    vec[used++] = (route_t) {
        .path = path,
        .path_len = strlen(path),
        .handler = handler
    };

#ifdef DEBUG
    EML_DEBUG(LOG_TAG, "Registered route: %s, N routes = %u", path, used);
#endif
}

const route_t *router_get_table(size_t *out_count)
{
    if(out_count) *out_count = used;
    return vec; /* read‑only outside this translation unit */
}
