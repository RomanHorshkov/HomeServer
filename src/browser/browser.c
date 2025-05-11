/*
 * browser.c
 *
 * Handles incoming client HTTP requests: parses raw data, dispatches to router,
 * and sends back HTTP responses over a TCP socket in two phases (headers then body).
 * Supports binary-safe transfers for static files (images, CSS, HTML, etc.).
 */

#include "browser.h"

#include <errno.h>      /* errno */
#include <stdio.h>      /* snprintf */
#include <stdlib.h>     /* free */
#include <string.h>     /* strerror */
#include <sys/socket.h> /* send(), recv() */
#include <unistd.h>     /* ssize_t */

#include "logger.h"          /* log_info, log_error */
#include "router.h"          /* router_handle_request */
#include "server_settings.h" /* HTTP constants */
#include "static_page.h"     /* static_page_serve_file */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/*
 * send_response:
 *   Build HTTP/1.1 response headers then stream the response body.
 * Parameters:
 *   fd - Client socket descriptor
 *   resp - Pointer to populated HttpResponse structure
 *   client_connection_policy - HTTP_CONNECTION_CLOSE or KEEP_ALIVE
 * Returns:
 *   0 on success, -1 on any failure (logged).
 */
static int send_response(int fd, const HttpResponse *resp, int client_connection_policy);

/*
 * send_all:
 *   Ensures that exactly 'len' bytes are sent over the socket,
 *   retrying on partial sends or EINTR.
 * Parameters:
 *   fd - Client socket descriptor
 *   buf - Pointer to data buffer
 *   len - Number of bytes to send
 * Returns:
 *   Total bytes sent (=len) or -1 on unrecoverable error.
 */
static ssize_t send_all(int fd, const void *buf, size_t len);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/*
 * Public: browser_manage_client_req
 * ---------------------------------
 * Parses an incoming HTTP request, routes it, and sends the response.
 * Frees any heap-allocated body after sending.
 * Parameters:
 *   fd - Client socket descriptor
 *   recv_buf - Buffer containing raw HTTP request data
 *   n - Number of bytes in recv_buf
 *   client_connection_policy - OUT param set to CONNECTION_CLOSE or KEEP_ALIVE
 * Returns:
 *   Number of bytes of body sent on success, -1 on error.
 */
ssize_t browser_manage_client_req(int fd, const char *recv_buf, size_t n,
                                  int *client_connection_policy)
{
    HttpRequest request;
    HttpResponse response;
    ssize_t result = -1;

    /* 1) Parse raw HTTP request into HttpRequest struct */
    if(http_parse_request(recv_buf, n, &request, client_connection_policy) < 0)
    {
        log_error("browser_manage_client_req: parse failed", strerror(errno));
        return -1;
    }

    /* 2) Route request to generate HttpResponse (status, headers, body) */
    if(router_handle_request(&request, &response) < 0)
    {
        log_error("browser_manage_client_req: routing failed", strerror(errno));
        return -1;
    }

    /* 3) Send HTTP response over TCP (headers + binary body) */
    if(send_response(fd, &response, *client_connection_policy) < 0)
    {
        log_error("browser_manage_client_req: send_response failed", strerror(errno));
        result = -1;
    }
    else
    {
        /* Return the number of body bytes sent (may be 0 for no-body) */
        result = (ssize_t)response.body_length;
    }

    /* 4) Free any heap buffer allocated by static_page_serve_file */
    free((void *)response.body);

    return result;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/*
 * send_response:
 *   Phase 1: Build and send the HTTP response headers (status, content-type,
 *   content-length, connection).
 *   Phase 2: Send the response body bytes (if any) using send_all().
 */
static int send_response(int fd, const HttpResponse *resp, int client_connection_policy)
{
    char hdr_buf[1024]; /* stack buffer for headers */
    int hdr_len;

    /* Build the status line and headers into hdr_buf */
    hdr_len =
        snprintf(hdr_buf, sizeof hdr_buf,
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: %s\r\n"
                 "\r\n",
                 resp->status_code, resp->status_text, resp->content_type, resp->body_length,
                 (client_connection_policy == HTTP_CONNECTION_CLOSE) ? "close" : "keep-alive");

    /* Validate header length */
    if(hdr_len < 0 || hdr_len >= (int)sizeof hdr_buf)
    {
        log_error("send_response: headers too large", "");
        return -1;
    }

    /* Send all header bytes */
    if(send_all(fd, hdr_buf, (size_t)hdr_len) < 0)
    {
        log_error("send_response: header send failed", strerror(errno));
        return -1;
    }

    /* Send response body if present */
    if(resp->body_length > 0 && resp->body)
    {
        if(send_all(fd, resp->body, resp->body_length) < 0)
        {
            log_error("send_response: body send failed", strerror(errno));
            return -1;
        }
    }

    return 0;
}

/*
 * send_all:
 *   Loops on send() until all 'len' bytes from 'buf' are written,
 *   handling EINTR and partial writes.
 */
static ssize_t send_all(int fd, const void *buf, size_t len)
{
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;

    while(total_sent < len)
    {
        ssize_t sent;
        sent = send(fd, ptr + total_sent, len - total_sent, 0);
        if(sent < 0)
        {
            if(errno == EINTR)
            {
                continue; /* retry if interrupted */
            }
            return -1; /* unrecoverable error */
        }
        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}
