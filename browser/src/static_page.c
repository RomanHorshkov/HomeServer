#include "static_page.h"

#include <errno.h>  // errno, EADDRINUSE, etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

/**
 * Serves a static file by reading it from disk and populating the HttpResponse.
 */
int static_page_serve_file(const char* filepath, const char* content_type, HttpResponse* response)
{
    /* return value */
    int res = -1;

    /* check input validity */
    if(filepath != NULL && response != NULL)
    {
        FILE* f = fopen(filepath, "rb");
        if(f != NULL)
        {
            /* Seek to end to find file size */
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);

            if(fsize > 0)
            {
                /* Allocate memory for the file content */
                char* file_buffer = (char*)malloc(fsize + 1);

                if(file_buffer != NULL)
                {
                    /* Read file content into buffer */
                    size_t read_bytes = fread(file_buffer, 1, fsize, f);
                    if(read_bytes == (size_t)fsize)
                    {
                        /* Null-terminate just in case */
                        file_buffer[fsize] = '\0';

                        /* Fill HttpResponse */
                        response->status_code = 200;
                        response->status_text = "OK";
                        response->content_type = content_type;
                        response->body = file_buffer;
                        response->body_length = (size_t)fsize;

                        /* close the file */
                        fclose(f);  // <-- THIS WAS MISSING

                        res = 0;
                    }
                    else
                    {
                        free(file_buffer);
                        fclose(f);
                        log_error("static_page: serve file: read_bytes != file_size",
                                  strerror(errno));
                    }
                }
                else
                {
                    fclose(f);
                    log_error("static_page: serve file: buffer malloc failed", strerror(errno));
                }
            }
            else
            {
                fclose(f);
                log_error("static_page: serve file: empty file", strerror(errno));
            }
        }
        else
        {
            log_error("static_page: serve file: open file failed", strerror(errno));
        }
    }
    else
    {
        log_error("static_page: serve file: invalid input", strerror(errno));
    }
    return res;
}
