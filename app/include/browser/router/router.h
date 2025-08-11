#ifndef SERVER_ROUTER_H
#define SERVER_ROUTER_H

#include "http_manager.h" /* <- pulls in the typedefs; safe here,
                               no circular dependency because this header
                               contains *no* struct definitions */

/**
 * @brief Route an HTTP request to the appropriate handler.
 *
 * Iterates through the routing table and dispatches the request to the first
 * matching handler. If no match is found, fills the response with a 404.
 *
 * @param request   Pointer to the parsed HttpRequest.
 * @param response  Pointer to the HttpResponse to populate.
 * @retval 0        Success.
 * @retval -1       No matching route (404).
 */
int router_handle_request(const HttpRequest* request, HttpResponse* response);

#endif /* SERVER_ROUTER_H */
