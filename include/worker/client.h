#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <netdb.h>  // NI_MAXHOST, NI_MAXSERV
#include <sys/types.h>


/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/
/* Server's worker structures */
typedef struct 
{
    int fd;                         // file descriptor
    char port[NI_MAXSERV];          // port name
    // future: thread id, last activity timestamp, etc.
} socket_t;

typedef struct
{
    socket_t* sockets;              // dynamic array of sockets for this worker
    size_t active_sockets_no;       // active_sockets
    size_t sockets_capacity;        // total sockets capacity of the array
} socket_info_t;

typedef struct
{
    pid_t pid;                      // process id for this worker
    socket_info_t sockets_info;     // sockets info struct
    char host[NI_MAXHOST];          // client IP (for grouping)
    // future: user/session info, etc.
} worker_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

int clients_init(worker_t *clients_ptr);
int client_add_socket(worker_t *client, const int file_descriptor, const char *host_name, const char *port_name);
int client_add_client(worker_t *client, const int file_descriptor, const char *host_name, const char *port_name);
int client_get_host(const worker_t *client, char* host_name, const size_t host_name_len);
void client_handle(const int client_fd);

#endif  // SERVER_CLIENT_H
