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

char* hs_build_http_request_exact_len(size_t target_len, size_t *out_len)
{
    const size_t base_len = sizeof(cs_base_request) - 1;
    if (target_len < base_len + 2)
    {
        return NULL;
    }

    char *buffer = malloc(target_len + 1);
    if (!buffer)
    {
        return NULL;
    }

    size_t len = 0;
    memcpy(buffer + len, cs_base_request, base_len);
    len += base_len;

    size_t header_idx = 0;
    char *last_value_start = NULL;
    size_t last_value_len = 0;

    while (header_idx < HTTP_MAX_HEADERS_IN)
    {
        char prefix[32];
        int prefix_len = snprintf(prefix, sizeof(prefix), "X-FILL-%02zu: ", header_idx);
        if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix))
        {
            break;
        }

        ssize_t remaining = (ssize_t)target_len - (ssize_t)len - 2;
        if (remaining <= (ssize_t)prefix_len + 2)
        {
            break;
        }

        ssize_t available = remaining - prefix_len - 2;
        size_t value_len = (size_t)available;
        if (value_len > HTTP_MAX_HEADER_VALUE_LEN)
        {
            value_len = HTTP_MAX_HEADER_VALUE_LEN;
        }

        bool will_continue = (available > (ssize_t)HTTP_MAX_HEADER_VALUE_LEN) &&
                             (header_idx + 1 < HTTP_MAX_HEADERS_IN);

        memcpy(buffer + len, prefix, (size_t)prefix_len);
        len += (size_t)prefix_len;
        memset(buffer + len, 'a', value_len);
        if (!will_continue)
        {
            last_value_start = buffer + len;
            last_value_len = value_len;
        }
        len += value_len;

        if (will_continue)
        {
            buffer[len++] = '\r';
            buffer[len++] = '\n';
            header_idx++;
            continue;
        }

        header_idx++;
        break;
    }

    ssize_t remaining_after_headers = (ssize_t)target_len - (ssize_t)(len + 2);
    if (remaining_after_headers > 0)
    {
        if (!last_value_start)
        {
            free(buffer);
            return NULL;
        }
        size_t extra = (size_t)remaining_after_headers;
        if (last_value_len + extra > HTTP_MAX_HEADER_VALUE_LEN)
        {
            extra = HTTP_MAX_HEADER_VALUE_LEN - last_value_len;
        }
        if (extra == 0)
        {
            free(buffer);
            return NULL;
        }
        memset(last_value_start + last_value_len, 'a', extra);
        last_value_len += extra;
        len += extra;
    }

    if (last_value_start)
    {
        buffer[len++] = '\r';
        buffer[len++] = '\n';
    }

    buffer[len++] = '\r';
    buffer[len++] = '\n';

    if (len != target_len)
    {
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    if (out_len)
    {
        *out_len = len;
    }
    return buffer;
}
