/**
 * @file static_page.c
 * @brief Reads static files from disk and populates HTTP responses.
 *
 * This module provides a single public function:
 *   - handler_static_page: open a file, read its contents, and fill
 *     in an HttpResponse with the appropriate headers and body.
 *
 * Detailed error logging is performed via logger.h on failures.
 */
#include "handler_static.h"

#include <stdio.h>  /* fopen, fseek, ftell, fread, fclose */
#include <stdlib.h> /* malloc, free */

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
#define URI_HOME "/"

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static(const HttpRequest *req, HttpResponse *response)
{
    /* Initialize result as failure by default */
    int res = STATUS_FAILURE;
    
    /* Buffer to store the resolved file path to serve */
    char file_path[HTTP_MAX_PATH_LEN];

    /*
     * Map the root URI ("/") to "pages/index.html" under the current working directory (var/www).
     * For all other URIs, map them directly under the current working directory, preserving the relative path.
     */
    if(strcmp(req->path, URI_HOME) == 0)
    {
        // If the request is for the home page, serve pages/index.html
        snprintf(file_path, sizeof(file_path), "pages/index.html");
    }
    else
    {
        /*
         * Remove the leading slash from the request path if present, so that the path can be
         * used directly relative to the current working directory (var/www).
         */
        const char *rel_path = req->path[0] == '/' ? req->path + 1 : req->path;
        size_t rel_len = strlen(rel_path);

        /*
         * Check that the resulting file path will not exceed the buffer size.
         * The path is constructed as: <rel_path>\0
         */
        if(rel_len + 1 > sizeof(file_path))
        {
            log_error("static_page: requested path too long: %s", req->path);
            send_404(response);
            return res;
        }
        // Copy the relative path to the buffer
        memcpy(file_path, rel_path, rel_len);
        // Null-terminate the resulting string
        file_path[rel_len] = '\0';

#ifdef DEBUG_MODE
        // Log the resolved file path for debugging purposes
        log_info("[handler static]: file path %s; opening...", file_path);
#endif /* DEBUG_MODE */
    }

    // Attempt to open the resolved file in binary read mode
    FILE *file = fopen(file_path, "rb");
    if(!file)
    {
        log_error("[handler static]: failed to open file %s: %s", file_path, strerror(errno));
        send_404(response);
        return res;
    }

    // Move the file pointer to the end of the file to determine its size
    if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("[handler static]: fseek to end failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    // Get the current file pointer position, which represents the file size
    long file_size = ftell(file);
    if(file_size < 0)
    {
        log_error("[handler static]: ftell failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    // Check if the file is empty
    if(file_size == 0)
    {
        log_error("static_page: file is empty: %s", file_path);
        fclose(file);
        send_404(response);
        return res;
    }

    // Move the file pointer back to the beginning of the file for reading
    if(fseek(file, 0, SEEK_SET) != 0)
    {
        log_error("static_page: fseek to start failed %s: %s", file_path, strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    /*
     * Allocate a buffer to hold the entire file contents in memory.
     * The buffer size is equal to the file size determined earlier.
     */
    char *buffer = (char *)malloc((size_t)file_size);
    if(!buffer)
    {
        log_error("static_page: malloc failed: %s", strerror(errno));
        fclose(file);
        send_404(response);
        return res;
    }

    /*
     * Read the entire file contents into the allocated buffer.
     * The fread function returns the total number of bytes read.
     */
    size_t total_read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    /*
     * Verify that the number of bytes read matches the expected file size.
     * If fewer bytes were read, it indicates an error or unexpected end of file.
     */
    if(total_read != (size_t)file_size)
    {
        log_error("static_page: fread read fewer bytes than expected: %s", file_path);
        free(buffer);
        send_404(response);
        return res;
    }

    /*
     * Determine the MIME type of the file based on its extension.
     * This is used to set the Content-Type header in the HTTP response.
     */
    const char *mime = guess_mime_type(file_path);

    /*
     * Populate the HttpResponse structure with the appropriate values:
     * - status_code: HTTP status 200 (OK)
     * - status_text: "OK" message
     * - content_type: determined MIME type
     * - body: pointer to the allocated buffer containing the file contents
     * - body_length: size of the file in bytes
     */
    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = mime;
    response->body = buffer;
    response->body_length = (size_t)file_size;
    res = STATUS_SUCCESS;

    return res;
}
