/**
 * @file db_request_local.h
 * @brief Lightweight, zero‑copy DB request views used by operator.
 */
#ifndef SERVER_WORKER_DB_REQUEST_LOCAL_H
#define SERVER_WORKER_DB_REQUEST_LOCAL_H

#include <stddef.h>
#include <stdint.h>

#include "config_core.h"
#include "utils/string_view.h"

#define HDR_KEY key
#define HDR_VAL value

/**
 * @brief Header key/value view (non‑owning slices).
 */
typedef struct
{
    sv_t key;   /**< header name slice */
    sv_t value; /**< header value slice */
} db_hdr_kv_t;

/**
 * @brief DB request assembled from an HTTP request (non‑owning views).
 */
typedef struct
{
    uint64_t now_epoch;       /**< current epoch seconds */
    uint32_t remote_ip_be;    /**< client IPv4 in big‑endian (if known) */
    uint16_t remote_port_be;  /**< client port in big‑endian (if known) */

    int method;               /**< HTTP method as enum/int */
    sv_t path;                /**< path slice */

    db_hdr_kv_t *headers;     /**< header views array */
    int header_count;         /**< number of header pairs */

    const char *body;         /**< pointer to body buffer (not owned) */
    size_t body_len;          /**< body length in bytes */
} DB_request_t;

#endif /* SERVER_WORKER_DB_REQUEST_LOCAL_H */
