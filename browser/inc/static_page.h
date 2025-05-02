#ifndef SERVER_STATIC_MANAGER_H
#define SERVER_STATIC_MANAGER_H

#include "http_manager.h"

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/* Loads a static file from disk and fills the HttpResponse. 
 * Returns 0 on success, -1 on failure.
 */
int static_page_serve_file(const char* filepath, const char* content_type, HttpResponse* response);


#endif /* SERVER_STATIC_MANAGER_H */