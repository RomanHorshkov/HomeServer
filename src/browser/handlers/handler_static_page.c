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

#include "handler_static_page.h"

#include <stdio.h>  /* fopen, fseek, ftell, fread, fclose */
#include <stdlib.h> /* malloc, free */

#include "handler_utils.h"
#include "server_settings.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static_page(const char* filepath, const char* content_type, HttpResponse* response)
{
    /* Return variable */
    int res = STATUS_FAILURE;

    /* Validate arguments */
    if(!filepath || !response)
    {
        log_error("static_page: invalid arguments (filepath or response is NULL)", "");
    }

    else
    {
        /* Open file in binary mode */
        FILE* file = fopen(filepath, "rb");

        /* prepare filesize variable */
        static long file_size = 0;

        if(!file)
        {
            log_error("static_page: failed to open file", strerror(errno));
        }

        /* Determine file size: seek to end */
        else if(fseek(file, 0, SEEK_END) != 0)
        {
            log_error("static_page: fseek to end failed", strerror(errno));
            fclose(file);
        }

        /* Check the file size */
        else if((file_size = ftell(file)) < 0)
        {
            log_error("static_page: ftell failed", strerror(errno));
            fclose(file);
        }

        /* Guard against empty files */
        else if(file_size == 0)
        {
            log_error("static_page: file is empty", filepath);
            fclose(file);
        }

        /* Reset file position to beginning */
        else if(fseek(file, 0, SEEK_SET) != 0)
        {
            log_error("static_page: fseek to start failed", strerror(errno));
            fclose(file);
        }

        /* Read file contents into response */
        else
        {
            /* Allocate buffer for file contents */
            char* buffer = (char*)malloc((size_t)file_size);
            if(!buffer)
            {
                log_error("static_page: malloc failed", strerror(errno));
                fclose(file);
            }

            /* Read all bytes */
            else
            {
                /* Read the file */
                size_t total_read = fread(buffer, 1, (size_t)file_size, file);

                /* Close file descriptor now that data is in memory */
                fclose(file);

                if(total_read != (size_t)file_size)
                {
                    log_error("static_page: fread read fewer bytes than expected", filepath);
                    free(buffer);
                }

                else
                {
                    /* Populate HttpResponse */
                    response->status_code = 200;
                    response->status_text = "OK";
                    response->content_type = content_type;
                    response->body = buffer;
                    response->body_length = (size_t)file_size;

                    /* set return value */
                    res = STATUS_SUCCESS;
                }
            }
        }
    }

    return res;
}
