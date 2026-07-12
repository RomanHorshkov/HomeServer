/**
 * @file sd_activation.c
 * @brief systemd socket-activation adapter (see sd_activation.h).
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 */

#define _GNU_SOURCE

#include <db_server/core/listener/sd_activation.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <emlog.h>
#include <db_server/core/config_core.h>

#define LOG_TAG "srv_sdact"

/* systemd passes activated fds starting at fd 3 (SD_LISTEN_FDS_START). */
#define SD_FD_START 3

/* Drop every activation variable so no child (none today, but be strict) inherits it. */
static void _clear_activation_env(void)
{
    unsetenv("LISTEN_PID");
    unsetenv("LISTEN_FDS");
    unsetenv("LISTEN_FDNAMES");
    unsetenv("LISTEN_FDS_START");
}

/* One getsockopt(int-valued) probe. Returns the value, or -1 on error. */
static int _sockopt_int(int fd, int optname)
{
    int       v   = 0;
    socklen_t len = sizeof v;
    if(getsockopt(fd, SOL_SOCKET, optname, &v, &len) == -1) return -1;
    return v;
}

/* Validate one inherited fd is a LISTENING AF_UNIX stream socket, then make it non-blocking + CLOEXEC. */
static int _adopt_fd(int fd)
{
    if(_sockopt_int(fd, SO_ACCEPTCONN) != 1)
    {
        EML_ERROR(LOG_TAG, "activation fd %d is not a listening socket", fd);
        return -1;
    }
    if(_sockopt_int(fd, SO_DOMAIN) != AF_UNIX)
    {
        EML_ERROR(LOG_TAG, "activation fd %d is not AF_UNIX", fd);
        return -1;
    }
    if(_sockopt_int(fd, SO_TYPE) != SOCK_STREAM)
    {
        EML_ERROR(LOG_TAG, "activation fd %d is not SOCK_STREAM", fd);
        return -1;
    }

    int fl = fcntl(fd, F_GETFD);
    if(fl == -1 || fcntl(fd, F_SETFD, fl | FD_CLOEXEC) == -1)
    {
        EML_PERR(LOG_TAG, "activation fd %d: FD_CLOEXEC failed", fd);
        return -1;
    }
    int st = fcntl(fd, F_GETFL);
    if(st == -1 || fcntl(fd, F_SETFL, st | O_NONBLOCK) == -1)
    {
        EML_PERR(LOG_TAG, "activation fd %d: O_NONBLOCK failed", fd);
        return -1;
    }
    return 0;
}

int sd_take_listen_fds(sd_listen_set_t* out)
{
    if(out)
    {
        out->api_fd    = -1;
        out->upload_fd = -1;
    }

    const char* s_pid   = getenv("LISTEN_PID");
    const char* s_fds   = getenv("LISTEN_FDS");
    const char* s_names = getenv("LISTEN_FDNAMES");

    /* No activation at all → the caller binds its own sockets (dev/direct run). */
    if(!s_pid || !s_fds)
    {
        return 0;
    }

    /* The fds are only ours if LISTEN_PID names THIS process. Anything else → not for us. */
    long pid = strtol(s_pid, NULL, 10);
    if(pid != (long)getpid())
    {
        EML_WARN(LOG_TAG, "LISTEN_PID=%ld != getpid()=%ld — ignoring foreign activation env", pid, (long)getpid());
        _clear_activation_env();
        return 0;
    }

    long n = strtol(s_fds, NULL, 10);
    if(n < 1)
    {
        _clear_activation_env();
        return 0;
    }
    if(n > (long)(SERVER_CORE_MAX_LISTENING_SOCKETS))
    {
        EML_ERROR(LOG_TAG, "LISTEN_FDS=%ld exceeds the expected maximum (%d)", n, SERVER_CORE_MAX_LISTENING_SOCKETS);
        _clear_activation_env();
        return -1;
    }

    /* Names are mandatory (the .socket units set FileDescriptorName=). We route by NAME, never by fd
     * position (review §9): fd 3 is not assumed to be "api". Copy out of the environment before tokenising. */
    if(!s_names || s_names[0] == '\0')
    {
        EML_ERROR(LOG_TAG, "activation without LISTEN_FDNAMES — set FileDescriptorName= on the .socket units");
        _clear_activation_env();
        return -1;
    }
    char names[256];
    if(strlen(s_names) >= sizeof names)
    {
        EML_ERROR(LOG_TAG, "LISTEN_FDNAMES too long");
        _clear_activation_env();
        return -1;
    }
    strncpy(names, s_names, sizeof names - 1);
    names[sizeof names - 1] = '\0';

    int  idx = 0;
    char* save = NULL;
    for(char* tok = strtok_r(names, ":", &save); tok; tok = strtok_r(NULL, ":", &save), ++idx)
    {
        if(idx >= n)
        {
            EML_ERROR(LOG_TAG, "more names than LISTEN_FDS=%ld", n);
            _clear_activation_env();
            return -1;
        }
        int fd = SD_FD_START + idx;
        if(_adopt_fd(fd) != 0)
        {
            _clear_activation_env();
            return -1;
        }
        if(strcmp(tok, "api") == 0)
        {
            out->api_fd = fd;
        }
        else if(strcmp(tok, "upload") == 0)
        {
            out->upload_fd = fd;
        }
        else
        {
            /* Forward-compatible: an unrecognised named listener is closed, not fatal. */
            EML_WARN(LOG_TAG, "ignoring unexpected activation socket name '%s' (fd %d)", tok, fd);
            close(fd);
        }
    }
    if(idx != n)
    {
        EML_ERROR(LOG_TAG, "LISTEN_FDNAMES has %d names but LISTEN_FDS=%ld", idx, n);
        _clear_activation_env();
        return -1;
    }

    _clear_activation_env();

    if(out->api_fd < 0)
    {
        EML_ERROR(LOG_TAG, "socket activation is missing the required 'api' socket");
        return -1;
    }

    EML_INFO(LOG_TAG, "socket-activated: api fd=%d, upload fd=%d", out->api_fd, out->upload_fd);
    return 1;
}
