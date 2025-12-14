/**
 * @file static_page.c
 * @brief Reads static files from disk and populates HTTP responses.
 *
 * This module provides a single public function:
 *   - handler_static_page: open a file, read its contents, and fill
 *     in an HttpResponse with the appropriate headers and body.
 *
 * Detailed error logging is performed via EMlog on failures.
 *
 *   @author  Roman Horshkov <roman.horshkov@gmail.com>
 *   @date    2025‑05‑11
 *   (c) 2025
 */
#include "handlers_int.h"
#include "emlog.h"
#include <string.h>

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "handler_static"

/****************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int handler_static(const http_request_t* req, http_response_t* res)
{
//     /* return value */
//     int res = STATUS_FAILURE;

//     /* filesystem path buffer */
//     char requested_file_path[HTTP_MAX_PATH_LEN] = {0};

//     /* relative path to serve */
//     const char *rel_path = NULL;

//     /* file handle */
//     FILE *f = NULL;

//     /* buffer for file contents */
//     char *body = NULL;

//     /* Validate input pointers */
//     if(request != NULL && response != NULL)
//     {
//         sv_t rel_sv = request->path;
//         /* Map "/" (home) to the SPA shell at views/index.html */
//         if(rel_sv.p && rel_sv.n == 1 && rel_sv.p[0] == '/')
//         {
//             rel_path = HOME_PAGE;
//         }

//         /* Strip leading '/' from other absolute paths */
//         else if(rel_sv.p && rel_sv.n >= 2 && rel_sv.p[0] == '/')
//         {
//             rel_sv.p += 1;
//             rel_sv.n -= 1;
//         }

//         /* Strip leading "./" if present */
//         else if(rel_sv.p && rel_sv.n >= 2 && rel_sv.p[0] == '.' && rel_sv.p[1] == '/')
//         {
//             rel_sv.p += 2;
//             rel_sv.n -= 2;
//         }

//         /* Use the path as-is otherwise */
//         else if(!rel_sv.p)
//         {
//             rel_sv.p = "";
//             rel_sv.n = 0;
//         }

//         /* Ensure the relative path fits into our buffer */
//         size_t rel_len = rel_path ? strlen(rel_path) : rel_sv.n;
//         if(rel_len + 1 <= sizeof(requested_file_path))
//         {
//             /* Copy the relative path into requested_file_path */
//             if(rel_path)
//             {
//                 memcpy(requested_file_path, rel_path, rel_len);
//             }
//             else
//             {
//                 memcpy(requested_file_path, rel_sv.p, rel_len);
//             }
//             requested_file_path[rel_len] = '\0';
//             rel_path = requested_file_path;

//             /* Open the file in binary mode */
//             f = fopen(requested_file_path, "rb");
//             if(f)
//             {
//                 /* size of the file */
//                 long size = 0;

//                 /* Seek to end to determine file size */
//                 if(fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0)
//                 {
//                     /* Rewind to beginning before reading */
//                     rewind(f);

//                     /* Allocate buffer to hold file contents */
//                     body = malloc((size_t)size);

//                     /* Read the entire file into the buffer */
//                     if(body && fread(body, 1, (size_t)size, f) == (size_t)size)
//                     {
//                         /* Populate the HttpResponse structure */
//                         response->status_code = 200;
//                         response->status_text = "OK";
//                         response->content_type = guess_mime_type(requested_file_path);
//                         response->body = body;
//                         response->body_length = (size_t)size;
//                         response->body_owned = 1;
//                         res = STATUS_SUCCESS;
//                     }
//                 }
//                 /* Close file handle */
//                 fclose(f);
//             }
//             else
//             {
// #ifdef MODE_DEBUG
//                 EML_ERROR(LOG_TAG, "Failed to open file %s", requested_file_path);
// #endif /* MODE_DEBUG */
//             }
//         }
//         else
//         {
// #ifdef MODE_DEBUG
//             EML_ERROR(LOG_TAG,
//                       "Requested path length does not fit into buffer (len=%zu)",
//                       strlen(rel_path));
// #endif /* MODE_DEBUG */
//         }
//     }

//     /* On failure, free any allocated buffer and send 404 */
//     if(res != STATUS_SUCCESS)
//     {
//         if(body)
//         {
//             free(body);
//         }
//         send_404(response);
//     }

    return res;
}

/****************************************************************************
 * PRIVATE FUNCTION DEFINITIONS
 ***************************************************************************
 */
/* None */
