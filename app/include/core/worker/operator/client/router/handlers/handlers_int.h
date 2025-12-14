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
#include <stdint.h>
#include <stdio.h>    /* snprintf, sscanf, fopen, fseek, ftell, fread, fclose */
#include <stdlib.h>   /* malloc, free, strdup, strtol, qsort */
#include <string.h>   /* strcmp, strcpy, strerror, strstr, strdup */
#include <sys/stat.h> /* stat, struct stat */

#include "contract_version.h" /* CONTRACT_VERSION */
#include "handlers_interface.h"
#include "http_manager.h" /* Http_request_t, HttpResponse */
#include "route_register.h"

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

/**
 * @brief Guess the MIME type for a file based on its extension.
 *
 * @param path Path to the file.
 * @return MIME type string (e.g., "text/html").
 */
const char *guess_mime_type(const char *path);

#endif /* SERVER_BROWSER_HANDLER_WHOAMI_H */
