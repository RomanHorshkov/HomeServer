#define _GNU_SOURCE

#include "worker/client.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <emlog.h>

#include "worker/operator.h"
#include "time_helper.h"

#define LOG_TAG "srv_client"

int client_handle(worker_operator_t *op, worker_client_slot_t *slot)
{
    (void)op;
    if(!slot) return STATUS_FAILURE;

    char buf[HTTP_RECEIVE_BUFFER_LEN];

    for(;;)
    {
        ssize_t n = recv(slot->fd, buf, sizeof(buf), 0);
        if(n > 0)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "fd %d received %zd bytes", slot->fd, n);
#endif
            slot->last_activity = (uint32_t)time_helper_get_now();
            slot->request_count++;
            /* For now just log and keep reading until EAGAIN/EOF */
            continue;
        }
        else if(n == 0)
        {
            /* peer closed */
            return STATUS_FAILURE;
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                return STATUS_SUCCESS;
            }
            EML_PERR(LOG_TAG, "recv failed on fd %d", slot->fd);
            return STATUS_FAILURE;
        }
    }
}
