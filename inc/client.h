#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H


/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES DECLARATIONS
 ****************************************************************************
 */

typedef struct clients_pot clients_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */


int clients_init(clients_t **clients);

int clients_add_new_client(clients_t **clients, struct sockaddr_storage *client_addr, int *client_fd);

void clients_handle_client(int *client_fd);

void clients_erase_client(clients_t **clients, pid_t *client_pid);

// void clients_close_client(clients_t **clients, pid_t *client_pid);

// void clients_close_all(clients_t **clients);

void clients_set_client_pid(clients_t **clients, pid_t *pid, int *client_fd);

void clients_shutdown(clients_t **clients);

#endif /* SERVER_CLIENT_H */
