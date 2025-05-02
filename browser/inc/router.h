#ifndef SERVER_ROUTER_H
#define SERVER_ROUTER_H


#include "http_manager.h"           // for HttpRequest and HttpResponse structs

/* Handles the incoming request based on the method and path.
 * Builds the response to send back into the out_response structure.
 */
int router_handle_request(const HttpRequest* request, HttpResponse* response);



#endif /* SERVER_ROUTER_H */