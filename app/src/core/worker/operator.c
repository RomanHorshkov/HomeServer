#define _GNU_SOURCE

#include "worker/operator.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <emlog.h>

#include "worker/client.h"
#include "reactor.h"
#include "spsc_ring.h"
#include "socket_helper.h"
#include "time_helper.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

#define LOG_TAG "srv_wrkr_operator"

#define OPERATOR_RING_CAPACITY 8U

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */

/****************************************************************************
 * PRIVATE FUNCTION DECLARATIONS
 ****************************************************************************
 */
static int _operator_handle_wakeup_event(int fd, fd_ctx_t *ctx);
static int _operator_handle_client_event(int fd, fd_ctx_t *ctx);
static int _operator_handle_timer_event(int fd, fd_ctx_t *ctx);
static int _operator_add_client(worker_operator_t *op, int client_fd);
static int _operator_remove_client(worker_operator_t *op, int client_fd);
static int _operator_timer_init(worker_operator_t *op);
static void _operator_timer_update(worker_operator_t *op);
static void _operator_check_timeouts(worker_operator_t *op);
static void _operator_set_timer(worker_operator_t *op, uint32_t freq);
static void _operator_check_timeouts(worker_operator_t *op);

/****************************************************************************
 * PUBLIC FUNCTION DEFINITIONS
 ****************************************************************************
 */

int worker_operator_init(worker_operator_t *op, int id)
{
    if(op == NULL)
    {
        EML_ERROR(LOG_TAG, "init: invalid input");
        return STATUS_FAILURE;
    }

    memset(op, 0, sizeof(*op));
    op->id = id;
    op->status = WORKER_STATUS_INACTIVE;
    op->wakeup_fd = -1;
    op->timer_fd = -1;

    /* Allocate mailbox ring */
    op->ring = spsc_ring_init(OPERATOR_RING_CAPACITY);
    if(!op->ring)
    {
        EML_ERROR(LOG_TAG, "init: ring alloc failed");
        return STATUS_FAILURE;
    }

    /* Create wakeup eventfd */
    op->wakeup_fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
    if(op->wakeup_fd == -1)
    {
        EML_PERR(LOG_TAG, "init: eventfd creation failed");
        spsc_ring_destroy(&op->ring);
        return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}

int worker_operator_get_wakeup_fd(const worker_operator_t *op)
{
    return op ? op->wakeup_fd : -1;
}

spsc_ring_t *worker_operator_get_ring(worker_operator_t *op)
{
    return op ? op->ring : NULL;
}

int worker_operator_start(worker_operator_t *op)
{
    if(op == NULL)
    {
        EML_ERROR(LOG_TAG, "start: invalid input");
        return STATUS_FAILURE;
    }

    if(pthread_create(&op->thread, NULL, worker_operator_thread, op) != 0)
    {
        EML_PERR(LOG_TAG, "start: pthread_create failed");
        return STATUS_FAILURE;
    }

    return STATUS_SUCCESS;
}

void worker_operator_shutdown(worker_operator_t *op)
{
    if(op == NULL)
    {
        return;
    }

    /* Stop thread if running */
    if(op->thread)
    {
        pthread_cancel(op->thread);
        pthread_join(op->thread, NULL);
        op->thread = 0;
    }

    spsc_ring_destroy(&op->ring);
    if(op->wakeup_fd != -1)
    {
        close(op->wakeup_fd);
    }
    op->wakeup_fd = -1;

    if(op->timer_fd != -1)
    {
        close(op->timer_fd);
    }
    op->timer_fd = -1;
}

void *worker_operator_thread(void *arg)
{
    worker_operator_t *op = (worker_operator_t *)arg;
    if(!op)
    {
        EML_ERROR(LOG_TAG, "thread: invalid operator");
        return NULL;
    }

    EML_INFO(LOG_TAG, "[op %d] thread starting", op->id);

    if(reactor_init(&op->reactor, (size_t)MAX_FAN_OUT_SOCKETS) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[op %d] reactor_init failed", op->id);
        return NULL;
    }

    if(_operator_timer_init(op) != STATUS_SUCCESS)
    {
        EML_ERROR(LOG_TAG, "[op %d] timer init failed", op->id);
        reactor_shutdown(&op->reactor);
        return NULL;
    }

    /* Register wakeup fd */
    fd_ctx_t *wakeup_ctx = calloc(1, sizeof(*wakeup_ctx));
    if(!wakeup_ctx)
    {
        EML_PERR(LOG_TAG, "[op %d] calloc wakeup_ctx failed", op->id);
        reactor_shutdown(&op->reactor);
        close(op->timer_fd);
        return NULL;
    }
    wakeup_ctx->fd = op->wakeup_fd;
    wakeup_ctx->owner = op;
    wakeup_ctx->handler = _operator_handle_wakeup_event;
    if(reactor_add_in(&op->reactor, op->wakeup_fd, wakeup_ctx) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[op %d] reactor_add_in wakeup failed", op->id);
        free(wakeup_ctx);
        reactor_shutdown(&op->reactor);
        close(op->timer_fd);
        return NULL;
    }
    op->wakeup_ctx = wakeup_ctx;
    op->status = WORKER_STATUS_ACTIVE;

    /* Main loop */
    while(op->status == WORKER_STATUS_ACTIVE || op->status == WORKER_STATUS_FULL)
    {
        int out_fd = -1;
        if(reactor_run(&op->reactor, &out_fd) != STATUS_SUCCESS)
        {
            if(out_fd != -1)
            {
                EML_INFO(LOG_TAG, "[op %d] reactor requested close for fd %d", op->id, out_fd);
                _operator_remove_client(op, out_fd);
                continue;
            }
            EML_ERROR(LOG_TAG, "[op %d] reactor_run failed", op->id);
        }
    }

    /* Cleanup */
    for(size_t i = 0; i < op->active_clients; i++)
    {
        close(op->clients[i].fd);
        free(op->clients[i].ctx);
    }

    reactor_shutdown(&op->reactor);
    if(op->timer_fd != -1)
    {
        close(op->timer_fd);
    }
    if(op->wakeup_ctx)
    {
        free(op->wakeup_ctx);
    }

    EML_INFO(LOG_TAG, "[op %d] thread exiting", op->id);
    return NULL;
}

/****************************************************************************
 * PRIVATE FUNCTION DEFINITIONS
 ****************************************************************************
 */

static int _operator_handle_wakeup_event(int fd, fd_ctx_t *ctx)
{
    worker_operator_t *op = (worker_operator_t *)ctx->owner;

    /* Drain wakeup fd */
    socket_drain(fd);

    int client_fd = -1;
    while(spsc_ring_pop(op->ring, &client_fd) == 0)
    {
#ifdef DEBUG_MODE
        EML_DBG(LOG_TAG, "[op %d tid %lu] wakeup: dispatching fd %d",
                op->id, (unsigned long)pthread_self(), client_fd);
#endif /* DEBUG_MODE */
        if(_operator_add_client(op, client_fd) != STATUS_SUCCESS)
        {
            EML_ERROR(LOG_TAG, "[op %d] failed to add client %d, closing", op->id, client_fd);
            socket_shutdown_and_close(client_fd);
            op->status = WORKER_STATUS_FULL;
        }
    }

    if(op->active_clients < WORKER_MAX_CLIENTS && op->status == WORKER_STATUS_FULL)
    {
        op->status = WORKER_STATUS_ACTIVE;
    }

    return STATUS_SUCCESS;
}

static int _operator_handle_client_event(int fd, fd_ctx_t *ctx)
{
    worker_operator_t *op = NULL;
#ifdef DEBUG_MODE
    op = ctx ? (worker_operator_t *)ctx->owner : NULL;
    EML_DBG(LOG_TAG, "[op %d tid %lu] client event fd %d, calling client_handle",
            op ? op->id : -1, (unsigned long)pthread_self(), fd);
#endif /* DEBUG_MODE */
    if(!ctx || !ctx->owner)
    {
        return STATUS_FAILURE;
    }
    op = (worker_operator_t *)ctx->owner;

    /* Find the client slot */
    worker_client_slot_t *slot = NULL;
    for(size_t i = 0; i < op->active_clients; ++i)
    {
        if(op->clients[i].fd == fd)
        {
            slot = &op->clients[i];
            break;
        }
    }
    if(!slot)
    {
        return STATUS_FAILURE;
    }

    int res = client_handle(op, slot);
    if(res != STATUS_SUCCESS)
    {
        _operator_remove_client(op, fd);
    }
    return STATUS_SUCCESS;
}

static int _operator_handle_timer_event(int fd, fd_ctx_t *ctx)
{
    worker_operator_t *op = (worker_operator_t *)ctx->owner;

    if(socket_drain(fd) == -1)
    {
        EML_PERR(LOG_TAG, "[op %d] timer read failed", op->id);
        return STATUS_FAILURE;
    }

    _operator_timer_update(op);
    _operator_check_timeouts(op);
    return STATUS_SUCCESS;
}

static int _operator_add_client(worker_operator_t *op, int client_fd)
{
    if(op->active_clients >= WORKER_MAX_CLIENTS)
    {
        EML_WARN(LOG_TAG, "[op %d] max clients reached (%d), rejecting fd %d",
                 op->id, WORKER_MAX_CLIENTS, client_fd);
        op->status = WORKER_STATUS_FULL;
        return STATUS_FAILURE;
    }

    client_socket_init(&client_fd);

    fd_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if(!ctx)
    {
        EML_PERR(LOG_TAG, "[op %d] calloc ctx failed for fd %d", op->id, client_fd);
        return STATUS_FAILURE;
    }

    ctx->fd = client_fd;
    ctx->owner = op;
    ctx->handler = _operator_handle_client_event;

    if(reactor_add_in_client(&op->reactor, client_fd, ctx) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[op %d] reactor_add_in_client failed for fd %d", op->id, client_fd);
        free(ctx);
        return STATUS_FAILURE;
    }

    op->clients[op->active_clients++] = (worker_client_slot_t){
        .fd = client_fd,
        .ctx = ctx,
        .last_activity = (uint32_t)time_helper_get_now(),
        .request_count = 0,
    };

#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "[op %d tid %lu] added client fd %d (active=%zu)",
            op->id, (unsigned long)pthread_self(), client_fd, op->active_clients);
#endif /* DEBUG_MODE */

    if(op->active_clients == 1)
    {
        _operator_set_timer(op, OPERATOR_TIMER_ACTIVE_SEC);
    }

    if(op->active_clients >= WORKER_MAX_CLIENTS)
    {
        op->status = WORKER_STATUS_FULL;
    }

    return STATUS_SUCCESS;
}

static int _operator_remove_client(worker_operator_t *op, int client_fd)
{
    size_t idx = SIZE_MAX;
    for(size_t i = 0; i < op->active_clients; ++i)
    {
        if(op->clients[i].fd == client_fd)
        {
            idx = i;
            break;
        }
    }

    if(idx == SIZE_MAX)
    {
        EML_ERROR(LOG_TAG, "[op %d] remove_client: fd %d not found", op->id, client_fd);
        return STATUS_FAILURE;
    }

    if(reactor_del(&op->reactor, op->clients[idx].fd) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[op %d] reactor_del failed fd %d", op->id, op->clients[idx].fd);
    }

    close(op->clients[idx].fd);
    free(op->clients[idx].ctx);

    /* Compact array */
    for(size_t j = idx + 1; j < op->active_clients; ++j)
    {
        op->clients[j - 1] = op->clients[j];
    }
    op->active_clients--;

#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "[op %d] removed client fd %d (active=%zu)", op->id, client_fd, op->active_clients);
#endif /* DEBUG_MODE */

    if(op->active_clients == 0)
    {
        _operator_set_timer(op, OPERATOR_TIMER_IDLE_SEC);
    }

    if(op->status == WORKER_STATUS_FULL && op->active_clients < WORKER_MAX_CLIENTS)
    {
        op->status = WORKER_STATUS_ACTIVE;
    }

    return STATUS_SUCCESS;
}

static int _operator_timer_init(worker_operator_t *op)
{
    op->timer_fd = time_helper_init();
    if(op->timer_fd == -1)
    {
        EML_PERR(LOG_TAG, "[op %d] timerfd_create failed", op->id);
        return STATUS_FAILURE;
    }

    fd_ctx_t *timer_ctx = calloc(1, sizeof(*timer_ctx));
    if(!timer_ctx)
    {
        EML_PERR(LOG_TAG, "[op %d] calloc timer_ctx failed", op->id);
        close(op->timer_fd);
        op->timer_fd = -1;
        return STATUS_FAILURE;
    }

    timer_ctx->fd = op->timer_fd;
    timer_ctx->owner = op;
    timer_ctx->handler = _operator_handle_timer_event;

    if(time_helper_set(op->timer_fd, SERVER_KEEPALIVE_TIMEOUT_NOT_ALONE, 0) == -1)
    {
        EML_PERR(LOG_TAG, "[op %d] time_helper_set failed", op->id);
        free(timer_ctx);
        close(op->timer_fd);
        op->timer_fd = -1;
        return STATUS_FAILURE;
    }

    if(reactor_add_in(&op->reactor, op->timer_fd, timer_ctx) != STATUS_SUCCESS)
    {
        EML_PERR(LOG_TAG, "[op %d] reactor_add_in failed for timer", op->id);
        free(timer_ctx);
        close(op->timer_fd);
        op->timer_fd = -1;
        return STATUS_FAILURE;
    }

    op->timer_frequency = OPERATOR_TIMER_IDLE_SEC;
    return STATUS_SUCCESS;
}

static void _operator_timer_update(worker_operator_t *op)
{
    if(op->active_clients > 0)
    {
        _operator_set_timer(op, OPERATOR_TIMER_ACTIVE_SEC);
    }
    else
    {
        _operator_set_timer(op, OPERATOR_TIMER_IDLE_SEC);
    }

#ifdef DEBUG_MODE
    EML_DBG(LOG_TAG, "[op %d] timer update (freq=%u, active=%zu)",
            op->id, op->timer_frequency, op->active_clients);
#endif /* DEBUG_MODE */
}

static void _operator_check_timeouts(worker_operator_t *op)
{
    uint32_t now = (uint32_t)time_helper_get_now();
    for(size_t i = 0; i < op->active_clients;)
    {
        worker_client_slot_t *c = &op->clients[i];
        uint32_t timeout = (c->request_count == 0) ? WORKER_CLIENT_TIMEOUT_SHORT
                                                   : WORKER_CLIENT_TIMEOUT_LONG;
        if((now - c->last_activity) > timeout)
        {
#ifdef DEBUG_MODE
            EML_DBG(LOG_TAG, "[op %d] closing idle fd %d (last=%u, now=%u, to=%u)",
                    op->id, c->fd, c->last_activity, now, timeout);
#endif
            int fd = c->fd;
            _operator_remove_client(op, fd);
            /* removal compacts array; do not increment i */
            continue;
        }
        i++;
    }
}

static void _operator_set_timer(worker_operator_t *op, uint32_t freq)
{
    if(op->timer_fd < 0) return;
    if(op->timer_frequency == freq) return;
    time_helper_set(op->timer_fd, freq, 0);
    op->timer_frequency = freq;
}
