/**
 * @file static_page.c
 * @brief Reads static files from disk and populates HTTP responses.
 *
 * This module provides a single public function:
 *   - static_page_serve_file: open a file, read its contents, and fill
 *     in an HttpResponse with the appropriate headers and body.
 *
 * Detailed error logging is performed via logger.h on failures.
 */

#include "static_page.h"

#include <errno.h>  /* errno */
#include <stdio.h>  /* fopen, fseek, ftell, fread, fclose */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* strerror */

#include "logger.h"

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int static_page_serve_file(const char* filepath, const char* content_type, HttpResponse* response)
{
    /* Validate arguments */
    if(!filepath || !response)
    {
        log_error("static_page: invalid arguments (filepath or response is NULL)", "");
        return -1;
    }

    /* Open file in binary mode to preserve raw bytes (esp. for images) */
    FILE* file = fopen(filepath, "rb");
    if(!file)
    {
        log_error("static_page: failed to open file", strerror(errno));
        return -1;
    }

    /* Determine file size: seek to end, then tell */
    if(fseek(file, 0, SEEK_END) != 0)
    {
        log_error("static_page: fseek to end failed", strerror(errno));
        fclose(file);
        return -1;
    }

    long file_size = ftell(file);
    if(file_size < 0)
    {
        log_error("static_page: ftell failed", strerror(errno));
        fclose(file);
        return -1;
    }

    /* Reset file position to beginning */
    if(fseek(file, 0, SEEK_SET) != 0)
    {
        log_error("static_page: fseek to start failed", strerror(errno));
        fclose(file);
        return -1;
    }

    /* Guard against empty files */
    if(file_size == 0)
    {
        log_error("static_page: file is empty", filepath);
        fclose(file);
        return -1;
    }

    /* Allocate buffer for file contents */
    char* buffer = (char*)malloc((size_t)file_size);
    if(!buffer)
    {
        log_error("static_page: malloc failed", strerror(errno));
        fclose(file);
        return -1;
    }

    /* Read all bytes */
    size_t total_read = fread(buffer, 1, (size_t)file_size, file);
    if(total_read != (size_t)file_size)
    {
        log_error("static_page: fread read fewer bytes than expected", filepath);
        free(buffer);
        fclose(file);
        return -1;
    }

    /* Close file descriptor now that data is in memory */
    fclose(file);

    /* Populate HttpResponse */
    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = content_type;
    response->body = buffer;
    response->body_length = (size_t)file_size;

    return 0;
}
