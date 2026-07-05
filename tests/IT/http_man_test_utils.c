/**
 * @file http_man_test_utils.c
 * @brief HTTP parser integration-test request generation helpers.
 */

#include "http_man_test_utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_common.h"

static const char cs_base_request[] =
    "POST /health HTTP/1.1\r\n"
    "Host: integration.tests\r\n"
    "Connection: close\r\n";

char* hs_build_http_request_exact_len(size_t target_len, size_t* out_len)
{
    const size_t base_len         = sizeof(cs_base_request) - 1;
    const size_t endline_len      = 4;  /* \r\n\r\n */
    const size_t min_line_len     = 13; /* "X-FILL-00: " + "\r\n" */
    const size_t header_value_cap = KiB(4) - 1;

    if(target_len < base_len + endline_len + min_line_len)
    {
        return NULL;
    }

    char* buffer = malloc(target_len + 1);
    if(!buffer)
    {
        return NULL;
    }

    memcpy(buffer, cs_base_request, base_len);
    size_t len = base_len;

    size_t filler_needed = target_len - base_len - endline_len;
    size_t header_idx    = 0;

    while(filler_needed > 0 && header_idx < HTTP_MAX_HEADERS_IN)
    {
        char prefix[32];
        int  prefix_len = snprintf(prefix, sizeof(prefix), "X-FILL-%02zu: ", header_idx);
        if(prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix))
        {
            break;
        }

        size_t min_line = (size_t)prefix_len + 2;
        if(filler_needed < min_line)
        {
            break;
        }

        size_t max_line  = min_line + header_value_cap;
        size_t line_len  = filler_needed < max_line ? filler_needed : max_line;
        size_t value_len = line_len - min_line;

        memcpy(buffer + len, prefix, (size_t)prefix_len);
        len += (size_t)prefix_len;

        if(value_len > 0)
        {
            memset(buffer + len, 'a', value_len);
            len += value_len;
        }

        buffer[len++] = '\r';
        buffer[len++] = '\n';

        filler_needed -= line_len;
        header_idx++;
    }

    if(filler_needed != 0)
    {
        free(buffer);
        return NULL;
    }

    buffer[len++] = '\r';
    buffer[len++] = '\n';
    buffer[len++] = '\r';
    buffer[len++] = '\n';

    if(len != target_len)
    {
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    if(out_len)
    {
        *out_len = len;
    }
    return buffer;
}
