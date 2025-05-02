#ifndef SERVER_CORE_H
#define SERVER_CORE_H


/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int server_init(const char *port);

void server_run(void);

void server_shutdown(void);

#endif /* SERVER_CORE_H */