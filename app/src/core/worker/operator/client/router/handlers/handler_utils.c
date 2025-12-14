/**
 * @file handler_utils.c
 * @brief Utility functions used by various handlers.
 *
 * Contains helper routines (e.g. guess_mime_type, send_404) that
 * assist other handler modules in building and sending HTTP responses.
 *
 *   @author  Roman Horshkov <roman.horshkov@gmail.com>
 *   @date    2025‑05‑11
 *   (c) 2025
 */

#include "handlers_int.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */
/* None */

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
 * MIME type mapping
 ****************************************************************************
 * Table-driven mapping of file extensions to MIME types.
 * Used by guess_mime_type() to set the Content-Type header.
 */
const struct
{
    const char *extension;
    const char *mime_type;
} mime_map[] = {
    {".html", "text/html"},   {".css", "text/css"},      {".js", "application/javascript"},
    {".jpg", "image/jpeg"},   {".jpeg", "image/jpeg"},   {".png", "image/png"},
    {".gif", "image/gif"},    {".svg", "image/svg+xml"}, {".json", "application/json"},
    {".md", "text/markdown"}, {".puml", "text/plain"}};

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

const char *guess_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if(!ext)
    {
        return "application/octet-stream";
    }
    for(size_t i = 0; i < sizeof(mime_map) / sizeof(mime_map[0]); ++i)
    {
        if(strcmp(ext, mime_map[i].extension) == 0)
        {
            return mime_map[i].mime_type;
        }
    }
    return "application/octet-stream";
}

/* Call this right before cJSON_PrintUnformatted(root); */
