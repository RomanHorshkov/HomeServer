# Deep‑Dive on POSIX Sockets

*A distilled field‑manual for writing robust server code in C.*

> **Scope** – this guide is **only** about the socket layer.  No HTTP, no business logic, no TLS.  Just file‑descriptors that talk IP.

---

## 1  What *is* a socket?

A socket is a kernel object referenced by a file‑descriptor.  You `read()` and `write()` it like any other FD, but the bytes you push travel across a transport protocol (TCP, UDP, …) instead of a disk.

* **Local endpoint** ⇒ *(IP, port, protocol)*
* **Remote endpoint** ⇒ *(IP, port, protocol)*

The kernel owns sequence numbers, retransmissions, congestion control; you own when to block, when to close, and which options to flip.

---

## 2  Server‑side lifecycle

| Stage      | Syscall                      | What really happens                                                             | When NIC traffic starts |
| ---------- | ---------------------------- | ------------------------------------------------------------------------------- | ----------------------- |
| **Create** | `socket(af,type,proto)`      | Kernel allocates an unbound inode.                                              | None                    |
| **Tune**   | `setsockopt()` / `fcntl()`   | Flags such as `SO_REUSEADDR`, `O_NONBLOCK`, `IPV6_V6ONLY`.                      | None                    |
| **Bind**   | `bind(fd, sockaddr*, len)`   | Associate a local IP/port; reserves the tuple.                                  | None                    |
| **Listen** | `listen(fd, backlog)`        | Turn the FD into a passive queue.                                               | None                    |
| **Accept** | `accept(fd, sa*, len*)`      | Three‑way handshake is *already* completed; kernel returns a new FD per client. | **Now**                 |
| **I/O**    | `read` / `write` / `sendmsg` | Exchange data.                                                                  | Continues               |
| **Close**  | `shutdown` / `close`         | FIN or RST depending on `SO_LINGER`.                                            | Final packets           |

`accept()` duplicates work: it **both** pulls the first pending connection *and* fills in the peer address, so you can log or apply ACLs.

---

## 3  Address families & structs

| Family | Macro      | Struct on the wire    | Bytes     |
| ------ | ---------- | --------------------- | --------- |
| IPv4   | `AF_INET`  | `struct sockaddr_in`  | 16        |
| IPv6   | `AF_INET6` | `struct sockaddr_in6` | 28        |
| UNIX   | `AF_UNIX`  | `struct sockaddr_un`  | up to 110 |

### 3.1  Generic buckets

`struct sockaddr` is the *common header* (2 bytes `sa_family`, 14 bytes data).  Too small for IPv6. `struct sockaddr_storage` is **big enough for any family** and properly aligned – always safe as a stack variable.

### 3.2  The cast rule

You may freely cast between specific ↑ structs and the generic ↓ type **as long as you pass the correct byte length** alongside it.

```c
struct sockaddr_storage ss;
socklen_t len = sizeof ss;
int cfd = accept(lsn_fd, (struct sockaddr *)&ss, &len);
```

After the call:

* `ss.ss_family` says which flavour you got.
* `len` holds 16 for IPv4, 28 for IPv6 – propagate it to `getnameinfo()`.

---

## 4  Dual‑stack strategies

| Strategy                | How                                              | Pros                            | Cons                                                                                   |
| ----------------------- | ------------------------------------------------ | ------------------------------- | -------------------------------------------------------------------------------------- |
| **2 sockets**           | One `AF_INET`, one `AF_INET6 + IPV6_V6ONLY=1`    | Clear separation, no surprises. | Slightly more FDs; epoll list ×2.                                                      |
| **1 dual‑stack socket** | Make only an IPv6 socket, leave `IPV6_V6ONLY=0`. | Single FD.                      | Breaks on distros with `net.ipv6.bindv6only=1`. IPv4 peers appear as `::ffff:a.b.c.d`. |

Your code path #1 (two sockets) is robust and portable.

---

## 5  Passing pointers & sizes correctly

### 5.1  `accept()` pattern

```c
struct sockaddr_storage addr;
for (;;) {
    socklen_t len = sizeof addr;          /* reset every loop */
    int cfd = accept4(lsn_fd,
                      (struct sockaddr *)&addr,
                      &len,
                      SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd == -1) {
        if (errno == EAGAIN)  break;      /* no pending */
        perror("accept");                /* real error */
        continue;
    }
    handle_client(cfd, &addr, len);
}
```

* **Always** pass the *pointer* to `len`; kernel writes back real size on success, leaves it untouched on error.
* Re‑initialise `len` before each call; otherwise it still contains the previous client’s size.

### 5.2  `getnameinfo()`

```c
char host[NI_MAXHOST], serv[NI_MAXSERV];
getnameinfo((struct sockaddr *)&addr, len,
            host, sizeof host,
            serv, sizeof serv,
            NI_NUMERICHOST | NI_NUMERICSERV);
```

Use the same `len` returned by `accept()` so IPv6 is not truncated.

---

## 6  Essential socket‑level options

| Option         | Level          | Typical value              | Effect                                                                       |
| -------------- | -------------- | -------------------------- | ---------------------------------------------------------------------------- |
| `SO_REUSEADDR` | `SOL_SOCKET`   | `int yes = 1`              | Rebind after crash without 2‑minute TIME‑WAIT wait.                          |
| `SO_REUSEPORT` | `SOL_SOCKET`   | `yes`                      | Multiple processes can bind the same (IP,port); kernel round‑robins accepts. |
| `SO_LINGER`    | `SOL_SOCKET`   | `{.l_onoff=1,.l_linger=0}` | Force RST on close – handy for listeners.                                    |
| `IPV6_V6ONLY`  | `IPPROTO_IPV6` | `int one=1` (or 0)         | Toggle dual‑stack vs IPv6‑only.                                              |
| `TCP_NODELAY`  | `IPPROTO_TCP`  | `one`                      | Disable Nagle; send tiny frames immediately.                                 |
| `O_NONBLOCK`   | `fcntl` flag   | n/a                        | Make accept/read/write return `EAGAIN` instead of hanging.                   |
| `FD_CLOEXEC`   | `fcntl` flag   | n/a                        | Do not leak sockets across `execve`.                                         |

---

## 7  Non‑blocking accept loops & epoll hints

* Use `accept4()` with `SOCK_NONBLOCK | SOCK_CLOEXEC` to avoid a race window.
* Never spin on `accept()` in a tight loop – integrate the listener FDs into `epoll_wait()`.
* Beware the “thundering herd”: with many worker processes, either set `SO_REUSEPORT` **or** designate one accept‑thread and share clients via a queue.

---

## 8  Printing addresses (IPv4 & IPv6)

```c
char buf[INET6_ADDRSTRLEN];
void *addr_ptr;
if (sa->sa_family == AF_INET) {
    addr_ptr = &((struct sockaddr_in *)sa)->sin_addr;
} else {
    addr_ptr = &((struct sockaddr_in6 *)sa)->sin6_addr;
}
inet_ntop(sa->sa_family, addr_ptr, buf, sizeof buf);
```

IPv6 buffer constant (`INET6_ADDRSTRLEN`) is big enough for IPv4 too – use it by default.

---

## 9  Checklist before shipping

* [x] `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR)` before **bind**.
* [x] Open with `FD_CLOEXEC` (or use `accept4`).
* [x] Decide dual‑stack strategy and set/clear `IPV6_V6ONLY` accordingly.
* [x] Handle `EINTR`, `EAGAIN`, `ECONNRESET` on accept/read/write.
* [x] Propagate real `socklen_t` sizes, never hard‑code `sizeof(struct sockaddr)` for IPv6.
* [x] Log peer addresses with `getnameinfo()+NI_NUMERICHOST`, not `inet_ntoa()`.
* [x] Sanity‑check your backlog (`/proc/sys/net/core/somaxconn`).

---

### Useful references

* **man 7 socket** – the grand overview.
* **RFC 3493** – Basic socket interface extensions for IPv6.
* W. Richard Stevens – *UNIX Network Programming Vol 1*.

*Last updated 11 May 2025*
