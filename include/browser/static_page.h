#ifndef SERVER_STATIC_MANAGER_H
#define SERVER_STATIC_MANAGER_H

#include "http_manager.h"

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Serve a static file.
 *
 * Opens the given file in binary mode, reads its entire contents into
 * memory, and sets up the HttpResponse fields accordingly. If any error
 * occurs, an error is logged and a non-zero result is returned.
 *
 * @param filepath     Path to the file on the local filesystem
 * @param content_type MIME type to set in the response (e.g. "text/html")
 * @param response     Pointer to an HttpResponse struct to populate
 * @return 0 on success, non-zero on error
 */
int static_page_serve_file(const char* filepath, const char* content_type, HttpResponse* response);

#endif /* SERVER_STATIC_MANAGER_H */