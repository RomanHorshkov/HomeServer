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
#define URI_HOME "/"

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
} mime_map[] = {{".html", "text/html"},
                {".css", "text/css"},
                {".js", "application/javascript"},
                {".jpg", "image/jpeg"},
                {".jpeg", "image/jpeg"},
                {".png", "image/png"},
                {".gif", "image/gif"},
                {".svg", "image/svg+xml"},
                {".json", "application/json"},
                {".md", "text/markdown"},
                {".puml", "text/plain"}};

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */
static int json_has_key_of_type(const cJSON *obj, const char *k, int type);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int url_decode(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0; /* index in dst[]                       */

    /*------------------------------------------------------------------------*
     * Iterate over every byte of the source string until we either run out   *
     * of input (src[si] == '\\0') *or* the destination buffer is one byte    *
     * away from being full (we keep space for the terminating NUL).          *
     *------------------------------------------------------------------------*/
    for(size_t si = 0; /* si = source index                    */
        src[si] != '\0' &&
        di < dst_sz - 1; /* stop if input ends or dst almost full*/
        ++si)
    {
        /*---------- pattern “%hh” where both h are hex digits --------------*/
        if(src[si] == '%' /*   looking at a '%'                   */
           &&
           isxdigit((unsigned char)
                        src[si + 1]) /*   next char is hex                   */
           &&
           isxdigit((unsigned char)
                        src[si + 2])) /*   char after that is hex             */
        {
            const char hex[3] = {src[si + 1], src[si + 2],
                                 '\0'}; /* build \"hh\\0\" for strtol */
            dst[di++]         = (char)strtol(
                hex, NULL, 16); /* convert to byte, store, advance dst  */
            si += 2; /* skip the two hex digits              */
        }
        /*---------- pattern “+” becomes space -----------------------------*/
        else if(src[si] == '+')
        {
            dst[di++] = ' '; /* convert '+' to space                 */
        }
        /*---------- any other byte is copied verbatim ---------------------*/
        else
        {
            dst[di++] = src[si]; /* normal character                     */
        }
    }

    /*------------------------------------------------------------------------*
     * exit the loop either because src was fully consumed or because         *
     * dst is full.  If run out of space, di == dst_sz - 1 and cannot         *
     * NUL‑terminate safely → return error.                                   *
     *------------------------------------------------------------------------*/
    if(di >= dst_sz - 1) /* need room for '\\0'                  */
        return -1;

    dst[di] = '\0'; /* final NUL                            */
    return 0;       /* success                              */
}

void send_400(HttpResponse *response)
{
    response->status_code  = 400;
    response->status_text  = "Bad Request";
    response->content_type = "text/html";
    response->body = "<html><body><h1>400 Bad Request</h1></body></html>";
    response->body_length = strlen(response->body);
}

void send_404(HttpResponse *response)
{
    response->status_code  = 404;
    response->status_text  = "Not Found";
    response->content_type = "text/html";
    response->body         = "<html><body><h1>404 Not Found</h1></body></html>";
    response->body_length  = strlen(response->body);
}

void send_405(HttpResponse *response)
{
    response->status_code  = 405;
    response->status_text  = "Method Not Allowed";
    response->content_type = "text/html";
    response->body =
        "<html><body><h1>405 Method Not Allowed</h1></body></html>";
    response->body_length = strlen(response->body);
}

void send_500(HttpResponse *response)
{
    response->status_code  = 500;
    response->status_text  = "Internal Server Error";
    response->content_type = "text/html";
    response->body =
        "<html><body><h1>500 Internal Server Error</h1></body></html>";
    response->body_length = strlen(response->body);
}

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
int validate_whoami_shape(const cJSON *root)
{
    if(!cJSON_IsObject(root))
        return -1;
    if(!json_has_key_of_type(root, "server_time", cJSON_String))
        return -1;
    if(!json_has_key_of_type(root, "method", cJSON_String))
        return -1;
    if(!json_has_key_of_type(root, "path", cJSON_String))
        return -1;
    if(!json_has_key_of_type(root, "headers", cJSON_Object))
        return -1;
    if(!json_has_key_of_type(root, "contract_version", cJSON_String))
        return -1;

    const cJSON *server_time =
        cJSON_GetObjectItemCaseSensitive(root, "server_time");
    const cJSON *method  = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *path    = cJSON_GetObjectItemCaseSensitive(root, "path");
    const cJSON *headers = cJSON_GetObjectItemCaseSensitive(root, "headers");
    const cJSON *version =
        cJSON_GetObjectItemCaseSensitive(root, "contract_version");

    if(!cJSON_IsString(server_time) || !server_time->valuestring)
        return -1;
    if(!cJSON_IsString(method) || !method->valuestring)
        return -1;
    if(!cJSON_IsString(path) || !path->valuestring)
        return -1;
    if(!cJSON_IsObject(headers))
        return -1;
    if(!cJSON_IsString(version) || !version->valuestring)
        return -1;
    if(strcmp(path->valuestring, "/api/whoami") != 0)
        return -1;

    return 0;
}

/****************************************************************************
 * PRIVATE FUNCTION DEFINITIONS
 ***************************************************************************
 */

static int json_has_key_of_type(const cJSON *obj, const char *k, int type)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, k);
    return (v && (v->type & 0xFF) == type);
}
