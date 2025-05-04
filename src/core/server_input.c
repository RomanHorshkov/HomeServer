#include "server_input.h"

#include <fcntl.h>  // For non-blocking stdin
#include <string.h>
#include <unistd.h>  // STDIN_FILENO and ssize_t

#include "logger.h"

/****************************************************************************
 * PRIVATE VARIABLES DEFINITIONS
 ****************************************************************************
 */

/* hold for first start */
static int first_start = -1;

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int check_stdin_for_exit(void)
{
    int res = -1;
    char buf[16];

    if(first_start == -1)
    {
        /* Set read from terminal to not blocking */
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        first_start = 0;
    }

    /* read the user input if any */
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);

    if(n > 0)
    {
        buf[n] = '\0';
        if(strchr(buf, 'q') != NULL)
        {
            log_info("Received 'q' from stdin. Exiting server...\n");
            res = 0;  // signal exit
        }
    }

    return res;
}