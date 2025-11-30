#define _GNU_SOURCE

#include "listener/listener.h"

#include <unistd.h>

#include <emlog.h>

#include "server_settings.h"

#define LOG_TAG "srv_listener"

int listener_init(const char *port, pipeline_t *pipeline_ptr)
{
    if(port == NULL || port[0] == '\0' || pipeline_ptr == NULL)
    {
        EML_ERROR(LOG_TAG, "listener_init: invalid input");
        return STATUS_FAILURE;
    }

    EML_INFO(LOG_TAG, "listener init stub on port %s", port);
    return STATUS_SUCCESS;
}

void *listener_run(void *arg)
{
    (void)arg;
    EML_INFO(LOG_TAG, "listener thread stub running");

    for(;;)
    {
        sleep(1);
    }

    return NULL;
}
