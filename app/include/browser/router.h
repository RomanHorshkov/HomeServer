#ifndef SERVER_ROUTER_H
#define SERVER_ROUTER_H

#include "http_manager.h"  // for HttpRequest and HttpResponse structs

/**
 * @brief Main router: finds a matching route and calls its handler.
 * @param request   The HTTP request.
 * @param response  The HTTP response to populate.
 * @return          0 on success, -1 on error.
 */
int router_handle_request(const HttpRequest* req, HttpResponse* res);

#endif /* SERVER_ROUTER_H */
