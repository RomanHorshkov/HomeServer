#include "browser.h"

#include <errno.h>      /* errno */
#include <stdio.h>      /* snprintf */
#include <stdlib.h>     /* free */
#include <string.h>     /* strerror */
#include <sys/socket.h> /* send(), recv() */

#include "handler_static.h"  /* handler_static_page */
#include "logger.h"          /* log_info, log_error */
#include "router.h"          /* router_handle_request */
#include "server_settings.h" /* HTTP constants */

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Send a complete HTTP response (headers and body) to a client socket.
 *
 * This function builds the HTTP/1.1 response headers into a stack buffer,
 * sends them in a single call, and then streams the response body (if any)
 * using @ref send_all(). Handles both text and binary-safe transfers.
 *
 * @param fd                      Client socket descriptor.
 * @param resp                    Pointer to a populated HttpResponse structure.
 * @retval  0  Success.
 * @retval -1 Failure (logged).
 */
static int send_response(int fd, const HttpResponse *resp);

/**
 * @brief Send exactly 'len' bytes from 'buf' over the socket, handling partial sends.
 *
 * This function loops on @c send() until all bytes are written, retrying on
 * partial sends or EINTR. It is binary-safe and suitable for large files.
 *
 * @param fd   Client socket descriptor.
 * @param buf  Pointer to data buffer.
 * @param len  Number of bytes to send.
 * @return     Total bytes sent (=len) or -1 on unrecoverable error.
 */
static ssize_t send_all(int fd, const void *buf, size_t len);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */
int browser_manage_client_req(int fd, const char *recv_buf, size_t n)
{
    /* return variable */
    int res = STATUS_FAILURE;

    /* create the request and response variables */
    HttpRequest request;
    HttpResponse response;

    memset(&request, 0, sizeof(HttpRequest));
    memset(&response, 0, sizeof(HttpResponse));

    /* manage http request */
    if(http_manage_request(recv_buf, n, &request) != STATUS_SUCCESS)
    {
        log_error("[browser] http_manage_request failed", strerror(errno));
    }

    /* Route request to generate HttpResponse (status, headers, body) */
    else if(router_handle_request(&request, &response) != STATUS_SUCCESS)
    {
        log_error("[browser] router_handle_request failed for fd %d", fd);
    }

    /* Send HTTP response over TCP (headers + binary body) */
    else if(send_response(fd, &response) < 0)
    {
        log_error("[browser] send_response failed", strerror(errno));

        /* Free any heap buffer allocated by handler_static_page */
        free((void *)response.body);
    }

    else
    {
        /* Set return variable to success */
        res = STATUS_SUCCESS;

        /* Free any heap buffer allocated by handler_static_page */
        free((void *)response.body);
    }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static int send_response(int fd, const HttpResponse *resp)
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
                 "keep-alive"); /* keep alive here because with close send_response is not called */

    /* Validate header length */
    if(hdr_len < 0 || hdr_len >= (int)sizeof hdr_buf)
    {
        log_error("[browser]: response headers too large", "");
        return -1;
    }

    /* Send all header bytes */
    if(send_all(fd, hdr_buf, (size_t)hdr_len) < 0)
    {
        log_error("[browser]: response header send failed", strerror(errno));
        return -1;
    }

    /* Send response body if present */
    if(resp->body_length > 0 && resp->body)
    {
        if(send_all(fd, resp->body, resp->body_length) < 0)
        {
            log_error("[browser]: response body send failed", strerror(errno));
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
