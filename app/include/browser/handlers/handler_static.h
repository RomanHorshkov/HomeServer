#ifndef SERVER_STATIC_PAGE_H
#define SERVER_STATIC_PAGE_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

#include "http_manager.h" /* HttpRequest, HttpResponse */

/****************************************************************************
 * PUBLIC FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Serve a static file in response to an HTTP request.
 *
 * This handler constructs a file path from the request path, attempts to open and read
 * the file from disk, and fills the provided HttpResponse structure with the file's
 * contents and appropriate headers. The MIME type is automatically determined based
 * on the file extension.
 *
 * On error (file not found, read error, etc.), logs the error and returns non-zero.
 * The caller is responsible for freeing the response body buffer if the call succeeds.
 *
 * @param req      Pointer to the parsed HttpRequest structure.
 * @param response Pointer to the HttpResponse structure to populate.
 * @retval  0      Success; response is fully populated.
 * @retval !=0     Failure; response is not valid.
 */
int handler_static(const HttpRequest *req, HttpResponse *response);

#endif /* SERVER_STATIC_PAGE_H */
