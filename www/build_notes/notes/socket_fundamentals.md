# Socket Fundamentals in a POSIX C Server
> *Everything you wanted to know about file‑descriptors that speak TCP, packed in one place.*
---

## Socket Fundamentals – What They Are and Why They Matter
A socket is a fundamental building block for network communication in POSIX systems, serving as the bridge between your application and the underlying network stack. It’s a file descriptor that you can read from and write to, just like a regular file, but with the critical difference that it handles the bidirectional flow of data over a network.

### Why Sockets Exist
At its core, a socket abstracts the complexity of network protocols, providing a simple API for sending and receiving data. Without sockets, every network application would need to manually craft packets, handle checksums, manage retransmissions, and negotiate handshakes – a daunting task that would make modern networking nearly impossible to scale.

### Types of Sockets
Sockets come in various flavors, each designed for different communication scenarios:
Stream Sockets (TCP) – Reliable, connection-oriented communication (e.g., web servers, SSH).
Datagram Sockets (UDP) – Connectionless, unreliable, but fast (e.g., DNS, VoIP).
Raw Sockets – Direct access to the network layer, used for protocols like ICMP (ping).

### How They Work
Sockets operate within the Transport Layer (Layer 4) of the OSI model, providing the mechanisms for end-to-end communication. They rely on IP addresses (Layer 3) to locate devices and ports to distinguish services on those devices.

### A typical server socket lifecycle:
Creation (socket) – Reserving a file descriptor for network I/O.
Binding (bind) – Associating the socket with a specific IP and port.
Listening (listen) – Transitioning to a passive state, ready to accept connections.
Accepting (accept) – Handshaking with clients and creating a new socket for each incoming connection.
Reading / Writing – Exchanging data through the new connection.
Closing (close / shutdown) – Releasing resources and gracefully terminating the connection.

### Why Use Sockets in C?
Using sockets directly in C gives you unparalleled control over the performance, scalability, and behavior of your network code. You’re not limited by the overhead of higher-level frameworks, making it possible to squeeze out every last bit of efficiency for latency-critical applications.

## Quick cheat‑sheet

| Stage        | Call                                | Typical purpose                         | Notes                                                  |
| ------------ | ----------------------------------- | --------------------------------------- | ------------------------------------------------------ |
| **Create**   | `int fd = socket(af, type, proto);` | Allocate a file descriptor.             | No packets on the wire yet.                            |
| **Tune**     | `setsockopt(fd, SOL_SOCKET, …)`     | Reuse, non‑blocking, linger, etc.       | Must happen before `bind` for some options.            |
| **Name**     | `bind(fd, sockaddr*, len);`         | Associate a local (IP, port) tuple.     | Wildcard `0.0.0.0` / `::` means *all* local addresses. |
| **Listen**   | `listen(fd, backlog);`              | Turn the passive endpoint into a queue. | Still no network traffic.                              |
| **Accept**   | `int cfd = accept(fd, sa*, len*);`  | Grab one queued connection.             | Now the three‑way handshake has already completed.     |
| **I/O**      | `read` / `write`                    | Exchange bytes.                         | May block unless `O_NONBLOCK`.                         |
| **Good‑bye** | `shutdown` / `close`                | Half‑close or fully close.              | `SO_LINGER` decides FIN vs RST.                        |

The calls above are pure kernel conversations; the NIC stays idle until **Accept** or later.

---

## 2  Address families

| Macro      | Meaning              | Wire‑format struct    |
| ---------- | -------------------- | --------------------- |
| `AF_INET`  | IPv4                 | `struct sockaddr_in`  |
| `AF_INET6` | IPv6                 | `struct sockaddr_in6` |
| `AF_UNIX`  | Local domain sockets | `struct sockaddr_un`  |

`getaddrinfo` lets you stay generic by passing `AF_UNSPEC`; it returns a linked list of `addrinfo` items, one per viable protocol/transport combo.

---

## 3  Dual‑stack: serving IPv4 *and* IPv6

### 3.1  The two classic approaches

| Approach                  | What you do                                                           | Pros                                     | Cons                                                                                                        |
| ------------------------- | --------------------------------------------------------------------- | ---------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| **Two sockets**           | Call `getaddrinfo`, create one fd per `AF_INET` *and* per `AF_INET6`. | Predictable behaviour, explicit control. | Twice the listeners, twice the epoll cost.                                                                  |
| **One dual‑stack socket** | Create an IPv6 fd, *leave* `IPV6_V6ONLY` **disabled** (0).            | Only one fd in epoll.                    | Some Linux dists ship with `net.ipv6.bindv6only=1`, breaking dual‑stack unless you clear the flag yourself. |

Your code picks the first path: it iterates the `addrinfo` list and runs `socket/bind/listen` for each entry. When the current item is IPv6 it explicitly flips `IPV6_V6ONLY` to **on** (`1`). That means each fd is *single‑stack* by design, avoiding surprises.

> **Tip –** If you ever want the single dual‑stack listener, just skip the `setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, …)` call.

### 3.2  IPv4‑mapped IPv6 addresses

With `IPV6_V6ONLY = 0`, an IPv4 peer `203.0.113.10:5000` appears as `::ffff:203.0.113.10 port 5000`. Use `INET6_ADDRSTRLEN` buffer size when printing.

---

## 4  Key structs at a glance

```c
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET
    in_port_t      sin_port;     // network‑byte‑order
    struct in_addr sin_addr;     // 32‑bit address
    char           sin_zero[8];  // padding (unused)
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;   // AF_INET6
    in_port_t       sin6_port;     // network‑byte‑order
    uint32_t        sin6_flowinfo; // usually 0
    struct in6_addr sin6_addr;     // 128‑bit address
    uint32_t        sin6_scope_id; // link‑local scope
};
```

For a generic bucket, cast both of the above to `struct sockaddr` or, even safer, use `struct sockaddr_storage` which is guaranteed to be big enough for any family.

---

## 5  Essential socket options

| Option         | Proto / Level  | Why you care                                                    | Code snippet                                                                |
| -------------- | -------------- | --------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `SO_REUSEADDR` | `SOL_SOCKET`   | Restart the server quickly after a crash.                       | `int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);`  |
| `SO_REUSEPORT` | `SOL_SOCKET`   | Multiple accept loops on the same port (perfect for multicore). | Linux 3.9+ only.                                                            |
| `SO_LINGER`    | `SOL_SOCKET`   | Decide between graceful FIN vs immediate RST at close.          | See `struct linger`.                                                        |
| `IPV6_V6ONLY`  | `IPPROTO_IPV6` | Toggle dual‑stack.                                              | `int one = 1; setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);` |
| `TCP_NODELAY`  | `IPPROTO_TCP`  | Disable Nagle, send small frames instantly.                     | Latency‑critical workloads.                                                 |
| `O_NONBLOCK`   | `fcntl` flag   | Never block in accept/read/write.                               | Combine with `epoll`/`poll`.                                                |
| `FD_CLOEXEC`   | `fcntl` flag   | Don’t leak sockets into `execve()`.                             | `fcntl(fd, F_SETFD, FD_CLOEXEC);`                                           |

---

## 6  Non‑blocking accept loop

```c
for (int i = 0; i < listener_count; ++i) {
    int cfd = accept4(listeners[i], NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            continue; // nothing queued right now
        }
        perror("accept");
        continue;
    }
    // hand cfd to your worker pool / epoll instance
}
```

Using `accept4` lets you set `O_NONBLOCK` and `FD_CLOEXEC` in one go, avoiding a race.

---

## 7  When does the NIC matter?

All the housekeeping (`socket`, `bind`, `listen`) lives inside the kernel. No Ethernet frame leaves the machine until **after**:

1. The peer sends a SYN.
2. The kernel completes the handshake.
3. Your process performs `accept` and starts I/O.

That’s why the sequence diagram places *NIC / LAN* only below the **Accept** message.

---

## 8  Sample full listener (IPv4 + IPv6, one per family)

```c
static int start_listeners(const char *port, int fds[], size_t *out_n) {
    struct addrinfo hints = {0}, *ai, *it;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &ai) != 0)
        return -1;

    size_t n = 0;
    for (it = ai; it && n < MAX_LSN; it = it->ai_next) {
        int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == -1) continue;

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &(struct linger){1,0}, sizeof(struct linger));
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

        if (bind(fd, it->ai_addr, it->ai_addrlen) == -1) { close(fd); continue; }
        if (listen(fd, 128) == -1) { close(fd); continue; }

        fds[n++] = fd;
    }
    freeaddrinfo(ai);
    *out_n = n;
    return n ? 0 : -1;
}
```

---

## 9  Further reading

* \[man 7 socket]
* \[man 2 bind]
* \[RFC 3493 – Basic Socket Interface Extensions for IPv6]
* Stevens & Fenner – *UNIX Network Programming, v1* (Sockets)
* Love – *Linux System Programming*, ch. 16

---

> *Last updated : 10 May 2025*
