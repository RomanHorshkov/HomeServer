#ifndef SERVER_BROWSER_HANDLER_WHOAMI_H
#define SERVER_BROWSER_HANDLER_WHOAMI_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */

/* libc / POSIX */
#include <ctype.h>  /* isxdigit() */
#include <dirent.h> /* opendir(), readdir(), closedir() */
#include <stddef.h>
#include <stdio.h>    /* snprintf, sscanf, fopen, fseek, ftell, fread, fclose */
#include <stdlib.h>   /* malloc, free, strdup, strtol, qsort */
#include <string.h>   /* strcmp, strcpy, strerror, strstr, strdup */
#include <sys/stat.h> /* stat, struct stat */

#include "http_manager.h" /* HttpRequest, HttpResponse */
#include "contract_version.h" /* CONTRACT_VERSION */
#include "handlers_interface.h"
#include "logger.h"          /* log_info, log_error */
#include "route_register.h"

#include "cJSON.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */

/* stringify PATH_MAX to build “%<PATH_MAX>s” format strings with sscanf()   */
#define _STR(x) #x
#define _XSTR(x) _STR(x)
#define PATH_MAX_STR _XSTR(PATH_MAX)

/* Allow room for “www” prefix + path + NUL                                   */
#define FULLPATH_MAX (PATH_MAX + 4)
/* Allow room for fullpath + “/” + NAME_MAX + NUL                             */
#define CHILDFULL_MAX (FULLPATH_MAX + NAME_MAX + 1)

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 *//**
 * url_decode()
 * ------------
 * Percent‑decodes @p src and writes the result to @p dst.
 *
 *  • UTF‑8 is *not* interpreted: the function operates on raw bytes.
 *  • The caller must supply a destination buffer of at least @p dst_sz bytes.
 *  • The output is always NUL‑terminated on success.
 *
 * Decoding rules (matching classic URL encoding):
 *   %xx   → single byte with hexadecimal value xx
 *   '+'   → ASCII space ' '
 *   other → byte copied unchanged
 *
 * @param src     NUL‑terminated percent‑encoded input string.
 * @param dst     Output buffer (may alias neither @p src nor overlap).
 * @param dst_sz  Size of @p dst in bytes, including the final NUL.
 *
 * @retval  0 Success – the string was fully decoded and fits in @p dst.
 * @retval -1 Failure – the decoded string would overflow @p dst.
 */
int url_decode(const char *src, char *dst, size_t dst_sz);

/**
 * @brief Populate an HttpResponse with a static 400 Bad Request HTML page.
 *
 * Sets the following fields in the response:
 *   - status_code   = 400
 *   - status_text   = "Bad Request"
 *   - content_type  = "text/html"
 *   - body          = "<html><body><h1>400 Bad Request</h1></body></html>"
 *   - body_length   = strlen(body)
 *
 * @param[out] response  Pointer to the HttpResponse structure to fill.
 */
void send_400(HttpResponse *response);

/**
 * @brief Populate an HttpResponse with a static 404 Not Found HTML page.
 *
 * Sets the following fields in the response:
 *   - status_code   = 404
 *   - status_text   = "Not Found"
 *   - content_type  = "text/html"
 *   - body          = "<html><body><h1>404 Not Found</h1></body></html>"
 *   - body_length   = strlen(body)
 *
 * @param[out] response  Pointer to the HttpResponse structure to fill.
 */
void send_404(HttpResponse *response);

/**
 * @brief Populate an HttpResponse with a static 405 Method Not Allowed HTML page.
 *
 * Sets the following fields in the response:
 *   - status_code   = 405
 *   - status_text   = "Method Not Allowed"
 *   - content_type  = "text/html"
 *   - body          = "<html><body><h1>405 Method Not Allowed</h1></body></html>"
 *   - body_length   = strlen(body)
 *
 * @param[out] response  Pointer to the HttpResponse structure to fill.
 */
void send_405(HttpResponse *response);

void send_500(HttpResponse *response);

/**
 * @brief Guess the MIME type for a file based on its extension.
 *
 * @param path Path to the file.
 * @return MIME type string (e.g., "text/html").
 */
const char *guess_mime_type(const char *path);

int validate_whoami_shape(const cJSON *root);

#endif /* SERVER_BROWSER_HANDLER_WHOAMI_H */
