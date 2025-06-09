#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <netdb.h>  // NI_MAXHOST, NI_MAXSERV
#include <sys/types.h>


/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/
/* Server's clients structures */
typedef struct 
{
    int fd;                         // file descriptor
    // pid_t pid;                   // process id
    char host[NI_MAXHOST];          // host name
    char port[NI_MAXSERV];          // port name
    // future: thread id, last activity timestamp, etc.
} socket_info_t;

typedef struct
{
    pid_t pid;                      // process id for this client
    socket_info_t *sockets;    // dynamic array of sockets for this client
    size_t sockets_count;
    size_t sockets_capacity;
    char host[NI_MAXHOST];          // client IP (for grouping)
    // future: user/session info, etc.
} client_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

int clients_init(client_t *clients_ptr);
int client_add_socket(client_t *client, const int file_descriptor, const char *host_name, const char *port_name);
int client_add_client(client_t *client, const int file_descriptor, const char *host_name, const char *port_name);
int client_get_host(const client_t *client, char* host_name, const size_t host_name_len);
void client_handle(const int client_fd);

#endif  // SERVER_CLIENT_H
