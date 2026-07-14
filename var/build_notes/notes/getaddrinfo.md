# `getaddrinfo(3)` – practical guide

> Line‑wrapped to \~72 columns for easy diffing / email pasting.

---

## 1  Why it exists

`getaddrinfo()` unifies the old IPv4‑only `gethostbyname()` and `getservbyname()` into **one re‑entrant, protocol‑agnostic API**. It handles IPv4 *and* IPv6, honours `/etc/hosts`, the resolver, and `/etc/services`, and hides nasty alignment issues of raw `sockaddr` structures.

---

## 2  Function signature

```c
int getaddrinfo(const char          *node,      /* host or NULL      */
                const char          *service,   /* port/service name */
                const struct addrinfo *hints,   /* filtering hints   */
                struct addrinfo     **res);     /* out: linked list  */
```

`freeaddrinfo(res)` must be called when done.  On failure the routine returns a non‑zero **gai‑error code**; *never* use `errno`.

---

## 3  `struct addrinfo`

```c
struct addrinfo {
    int              ai_flags;      /* AI_* bitmask          */
    int              ai_family;     /* AF_*                  */
    int              ai_socktype;   /* SOCK_STREAM/DGRAM/... */
    int              ai_protocol;   /* IPPROTO_* or 0        */
    socklen_t        ai_addrlen;    /* length of *ai_addr    */
    struct sockaddr *ai_addr;       /* ready‑to‑bind/connect */
    char            *ai_canonname;  /* optional FQDN         */
    struct addrinfo *ai_next;       /* next node in list     */
};
```

All fields are filled by libc except `hints` (the *input* structure).

---

## 4  Common `ai_flags`

| Flag             | Meaning                                          |
| ---------------- | ------------------------------------------------ |
| `AI_PASSIVE`     | If `node==NULL`, return wildcard (0.0.0.0 / ::). |
| `AI_CANONNAME`   | Fill `ai_canonname` with primary FQDN.           |
| `AI_NUMERICHOST` | Skip DNS; `node` is a literal address.           |
| `AI_V4MAPPED`    | Map IPv4 to v6 `::ffff:a.b.c.d` when asking v6.  |
| `AI_ADDRCONFIG`  | Only families that have at least one local addr. |

---

## 5  Typical **server** pattern

```c
struct addrinfo hints = {0};
hints.ai_family   = AF_UNSPEC;   /* v4 or v6        */
hints.ai_socktype = SOCK_STREAM; /* TCP             */
hints.ai_flags    = AI_PASSIVE;  /* wildcard bind   */

struct addrinfo *res;
int rc = getaddrinfo(NULL, "8080", &hints, &res);
// iterate → socket() → setsockopt() → bind() → listen() ...
```

---

## 6  Typical **client** pattern

```c
struct addrinfo hints = {0};
hints.ai_family   = AF_UNSPEC;
hints.ai_socktype = SOCK_STREAM;

struct addrinfo *res;
int rc = getaddrinfo("example.com", "https", &hints, &res);
// iterate → socket() → connect() until success
```

---

## 7  Error handling (`gai_strerror`)

| Code         | Reason                              |
| ------------ | ----------------------------------- |
| `EAI_NONAME` | Host/service not found              |
| `EAI_AGAIN`  | Temporary DNS failure (retry)       |
| `EAI_FAIL`   | Permanent DNS failure               |
| `EAI_FAMILY` | Unsupported family requested        |
| `EAI_SYSTEM` | Check `errno` for underlying reason |

---

## 8  Pitfalls & best‑practice tips

* **Iterate the list** – never assume the first node works.
* Use `SO_REUSEADDR` before `bind()` to recycle ports after crash.
* For IPv6‑only servers set `IPV6_V6ONLY` to avoid dual‑stack issues.
* Call `freeaddrinfo()` even on failure paths → no leaks.
* Do **not** hard‑code address lengths; use `ai_addrlen`.

---

## 9 Resolver deep‑dive – GNU libc `getaddrinfo()`

> builds the `addrinfo` linked list — no socket syscalls yet.

| #  | Step                             | Routine / file        | Notes                     |
| -- | -------------------------------- | --------------------- | ------------------------- |
| 1  | Fast path – numeric literal?     | `inet_pton()`         | If `AI_NUMERICHOST` *or*  |
|    |                                  |                       | purely numeric input, the |
|    |                                  |                       | code skips all name‑      |
|    |                                  |                       | service lookups.          |
| 2  | Apply `hints` filtering          | `gaih_inet()`         | Drops families / sock‑    |
|    |                                  |                       | types / protocols that    |
|    |                                  |                       | clash with caller’s       |
|    |                                  |                       | `hints`.                  |
| 3  | Service name → port number       | `getservbyname_r()` → |
|    |                                  | `/etc/services`       | Alpha strings like        |
|    |                                  |                       | "https" become **443/tcp**.  |
|    |                                  |                       | Numeric strings use       |
|    |                                  |                       | `strtoul()`.              |
| 4  | Hostname resolution (NSS switch) | libc NSS dispatcher   | Order from `/etc/nss‑     |
|    |                                  |                       | switch.conf`, e.g.        |
|    |                                  |                       | `hosts: files dns mdns`.  |
| 4a |  → `files` module                | `nss_files`           | Scans `/etc/hosts`; zero  |
|    |                                  |                       | network I/O.              |
| 4b |  → `dns` module                  | `libresolv` /         |
|    |                                  | `systemd‑resolved`    | UDP 53 (or TCP on trun‑   |
|    |                                  |                       | cation); retries /        |
|    |                                  |                       | timeouts from `/etc/      |
|    |                                  |                       | resolv.conf`.             |
| 4c |  → `mdns` module                 | `nss_mdns`            | Multicast DNS on          |
|    |                                  |                       | 224.0.0.251:5353.         |
|    |                                  |                       | Only if `.local` or       |
|    |                                  |                       | explicitly requested.     |
| 5  | IPv4‑mapped tweaks               | `gaih_inet()`         | With `AI_V4MAPPED`/       |
|    |                                  |                       | `AI_ALL`, synthesizes     |
|    |                                  |                       | `::ffff:a.b.c.d` when     |
|    |                                  |                       | only v4 results exist.    |
| 6  | Canonical name fill              | same                  | If `AI_CANONNAME`, the    |
|    |                                  |                       | first successful lookup   |
|    |                                  |                       | sets `ai_canonname`.      |
| 7  | Build `addrinfo` nodes           | same                  | For each address and      |
|    |                                  |                       | each (socktype, proto)    |
|    |                                  |                       | pair, malloc an `addr‑    |
|    |                                  |                       | info` and chain to list.  |
| 8  | RFC 6724 sort / de‑preference    | `rfc3484_sort()`      | Loopback < IPv4 <         |
|    |                                  |                       | link‑local < global.      |
| 9  | Return to caller                 | —                     | Head pointer of singly‑   |
|    |                                  |                       | linked list. Caller must  |
|    |                                  |                       | `freeaddrinfo()`.         |

> ✔ **Entirely user‑space.**  No kernel interaction until you later call `socket()`, `bind()`, or `connect()`.  The only possible I/O is DNS traffic or a handful of tiny config‑file reads.

*Last update: 2025‑05‑10*
