/**
 * @file sd_activation.h
 * @brief systemd socket-activation adapter — adopt pre-opened listening fds by name.
 *
 * In production the two backend transports are NOT bound by this process. systemd creates
 * /run/home_server/{api,upload}.sock (right owner/group/mode) BEFORE the service starts, owns their
 * cleanup (RemoveOnStop=yes), and passes the already-listening fds to us via the LISTEN_FDS protocol
 * (sd_listen_fds(3)). This removes bind()/chmod()/unlink() from the backend entirely, shrinking both the
 * privilege and the AppArmor surface.
 *
 * We parse the protocol directly (LISTEN_PID / LISTEN_FDS / LISTEN_FDNAMES) rather than linking
 * libsystemd: it is a small, stable ABI and this platform keeps its dependency and attack surface
 * minimal. The adapter performs the SAME validations sd_listen_fds_with_names() would (exact expected
 * names, AF_UNIX + SOCK_STREAM + listening, CLOEXEC) and unsets the activation environment once consumed.
 *
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 */

#ifndef SERVER_SD_ACTIVATION_H
#define SERVER_SD_ACTIVATION_H

/**
 * @brief The named listening fds handed over by systemd socket activation.
 *
 * Each field is a ready-to-use, non-blocking, close-on-exec, LISTENING AF_UNIX stream fd, or -1 when
 * that named socket was not passed. @ref api_fd is mandatory for an activated run; @ref upload_fd is
 * optional (its absence simply keeps uploads on the operator path).
 */
typedef struct
{
    int api_fd;    /**< fd named "api", or -1 */
    int upload_fd; /**< fd named "upload", or -1 */
} sd_listen_set_t;

/**
 * @brief Consume the systemd socket-activation environment, validating and adopting the named fds.
 *
 * Reads LISTEN_PID/LISTEN_FDS/LISTEN_FDNAMES. When this process was socket-activated it validates every
 * passed fd (LISTEN_PID == getpid(); AF_UNIX; SOCK_STREAM; SO_ACCEPTCONN; a recognised name), marks it
 * O_NONBLOCK | FD_CLOEXEC, records it into @p out, and ALWAYS unsets the activation environment so no
 * child inherits it.
 *
 * @param[out] out  REQUIRED (must be non-NULL). Filled with the adopted fds (fields default to -1);
 *                  fields stay -1 on the not-activated path. A NULL @p out is a caller bug → -1.
 * @retval  1  Socket-activated: @p out->api_fd is a valid listening fd (upload_fd may be -1).
 * @retval  0  Not socket-activated (no/foreign LISTEN_FDS) — the caller should bind its own sockets.
 * @retval -1  NULL @p out, OR activated but malformed (missing "api", bad name/type, count mismatch) — fatal.
 */
int sd_take_listen_fds(sd_listen_set_t* out);

#endif /* SERVER_SD_ACTIVATION_H */
