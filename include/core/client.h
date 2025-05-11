#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <netdb.h>  // NI_MAXHOST, NI_MAXSERV
#include <sys/types.h>

void client_handle(const int client_fd);

#endif  // SERVER_CLIENT_H
