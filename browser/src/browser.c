#include "browser.h"

/****************************************************************************
 * PRIVATE INCLUDES
 ****************************************************************************
 */

#include <errno.h>  // errno, EADDRINUSE, etc.

#include "logger.h"
#include "router.h"
#include "server_settings.h"
#include "static_page.h"

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

ssize_t browser_manage_client_req(const char* recv_buf, size_t n, char* send_buf,
                                  int* client_connection_policy)
{
    /* return value, set to failure */
    int res = -1;

    /* http client request struct */
    HttpRequest request;

    /* http server response struct */
    HttpResponse response;

    /* suppose http requests are coming from the client */
    /* fill the request structure properly */
    if(http_parse_request(recv_buf, n, &request, client_connection_policy) == -1)
    {
        log_error("Browser: http parse request gone wrong", strerror(errno));
    }

    /* Handle the routing */
    else if(router_handle_request(&request, &response) == -1)
    {
        /* Handle bad routing, 404 or 500 */
        log_error("Browser: router handle request failed", strerror(errno));
    }

    /* build the http response */
    else
    {
        res = http_build_response(&response, client_connection_policy, send_buf,
                                  HTTP_SEND_BUFFER_LEN);
    }

    return res;
}