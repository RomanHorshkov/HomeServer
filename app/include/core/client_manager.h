/**
 * @file connection_manager.h
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025-05-11
 */

#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
 */

typedef struct client_manager client_manager_t;

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */

int client_manager_init(client_manager_t **client_manager_ptr_ptr);

int client_manager_add_connection(client_manager_t *client_manager_ptr, int fd);

int client_manager_remove_connection(client_manager_t *client_manager_ptr, int fd);

int client_manager_manage_client(client_manager_t *client_manager_ptr, int fd);

int client_manager_get_active_connections(client_manager_t *client_manager_ptr);

int client_manager_shutdown(client_manager_t *client_manager_ptr);

#endif /* CONNECTION_MANAGER_H */
