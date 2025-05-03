#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

/****************************************************************************
 * PUBLIC INCLUDES
 ****************************************************************************
 */
#include <errno.h>   // errno, EADDRINUSE, etc.
#include <string.h>  // memset(), strcpy(), strlen(), strerror(), etc.

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

struct addrinfo;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

void logger_init(const char *filename);
void logger_close();

void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_addrinfo_list(const struct addrinfo *ai);

#endif /* SERVER_LOGGER_H */