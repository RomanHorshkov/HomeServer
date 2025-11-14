
#include "epoller.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h> /* epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event */
#include <unistd.h>

#include "server_settings.h"
#include <emlog.h>

#define LOG_TAG "epoller"

/** epoll event
 * The event argument describes the object linked to the file descriptor fd.  The struct
 * epoll_event is described in epoll_event(3type). The data member of the epoll_event
 * structure specifies data that the kernel should save and then return (via
 * epoll_wait(2)) when this file descriptor becomes ready. The events member of the
 * epoll_event structure is a bit mask composed by ORing together zero or more event
 * types, returned by epoll_wait(2), and input flags, which affect its behaviour, but
 * aren't returned.
 */

/** epoll event uint32_t
 * EPOLLIN: The associated file is available for read(2) operations
 * EPOLLOUT: The associated file is available for write(2) operations
 * EPOLLRDHUP: (since Linux 2.6.17) Stream  socket  peer  closed  connection, or shut
 * down writing half of connection.
 * EPOLLERR: Error condition happened on the associated
 * file descriptor.  This event is also reported for the write end of a pipe when the
 * read end has been closed.
 * EPOLLONESHOT: (since Linux 2.6.2) Requests one-shot
 * notification for the associated file descriptor.  This means that after an event
 * notified for the file descriptor by epoll_wait(2), the file descriptor is disabled in
 * the interest list and no other events will be reported by the epoll interface.  The
 * user must call epoll_ctl() with EPOLL_CTL_MOD to rearm the file descriptor with a new
 * event mask.
 */

int epoller_new(void)
{
    int res = epoll_create1(0);
    if(res < 0) return -errno;
    return res;
}

int epoller_wait(int epoll_fd, struct epoll_event *out_events)
{
    int n = epoll_wait(epoll_fd, out_events, MAX_FAN_OUT_SOCKETS, -1);
    if(n < 0)
    {
        if(errno == EINTR)
        {
            EML_INFO(LOG_TAG, "[reactor] epoll_listen_events interrupted by signal EINTR: %s",
                     strerror(errno));
        }
        else
        {
            EML_ERROR(LOG_TAG, "[reactor] epoll_listen_events error: %s", strerror(errno));
        }
    }

    return n;
}

int epoller_manage_fd(int epoll_fd, int target_fd, int operation, uint32_t event, void *data)
{
    if(epoll_fd < 0 || target_fd < 0) return -EINVAL;

    struct epoll_event ev;

    switch(operation)
    {
        case EPOLL_CTL_ADD:
        case EPOLL_CTL_MOD:

            if(data == NULL)
            {
                EML_ERROR(LOG_TAG, 
                    "[epoller] _manage_fd with NULL data, epoll_fd %d, "
                    "target_fd %d, op %d",
                    epoll_fd, target_fd, operation);
            }
            else
            {
                EML_INFO(LOG_TAG, 
                    "[epoller] _manage_fd with data, epoll_fd %d, target_fd "
                    "%d, op %d",
                    epoll_fd, target_fd, operation);
                ev.events = event;
                ev.data.ptr = data;
            }

            return epoll_ctl(epoll_fd, operation, target_fd, &ev) ? -errno : 0;

        case EPOLL_CTL_DEL:
        default:
            /* Explicit the DEL operation */
            EML_INFO(LOG_TAG, 
                "[epoller] _manage_fd Deleting, epoll_fd %d, target_fd %d, op "
                "%d",
                epoll_fd, target_fd, operation);
            return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, target_fd, NULL) ? -errno : 0;
    }
}

int epoller_check_if_to_close(uint32_t ev_conn)
{
    return (ev_conn & (EPOLLRDHUP | EPOLLHUP | EPOLLERR));
}

int epoller_shutdown(int epoll_fd)
{
    if(epoll_fd < 0) return -EINVAL;
    if(close(epoll_fd) < 0) return -errno;
    return 0;
}
