# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX, Multithreaded)

A compact yet fully‑featured teaching project that demonstrates how to write a **modular, multithreaded TCP + HTTP/1.1 server** in pure C11/POSIX.
It now serves **dynamic JSON APIs**, **rich client‑side pages**, a **build-notes viewer**, and a **directory‑driven file browser**, with:

* non‑blocking *listener* sockets (epoll-based)
* event-driven *worker* thread managing all client sockets
* graceful admission control (max‑connections cap)
* structured logging (to `server.log`)
* production‑grade **HTTP/1.1 parsing** via the 🏎️ **[llhttp](https://github.com/nodejs/llhttp)** state‑machine library (static‑linked)
* full request‑header capture (e.g. *User‑Agent*, *Accept*, …)
* proper **Connection: keep‑alive / close** negotiation and persistent sockets (120 s idle timeout)
* serving real **static pages** (`index.html`, `style.css`, **header component**, images, …) from `var/www/`
* a **JSON API** endpoint (`/api/whoami`) providing request metadata
* a **Drive API** endpoint (`/api/drive?path=/subdir`) that returns JSON directory listings
* a **Build Notes viewer** (`/build_notes`) that turns Markdown + PlantUML source into live docs
* matching HTML front‑ends: **Who Am I** (`whoami.html`), **Drive** (`drive.html`) and **Build Notes** (`build_notes/index.html`)
* an optional **rich animation** demo (`dynamic.html`) with CSS/JS effects
* log‑controlled terminal shutdown (`q`)

Everything builds with **`gcc -std=c11 -Wall -Wextra -Werror -pedantic`** and only the POSIX libc + the bundled **`libllhttp.a`** and **`libcjson.a`**—no external runtime deps.

---

## Table of Contents

1. [Quick start](#quick-start)
2. [Directory layout](#directory-layout)
3. [Utils](#utils)
4. [Top-level data-flow](#top-level-data-flow)
5. [Sockets & threading model](#sockets--threading-model)
6. [Timeouts & limits](#timeouts--limits)
7. [HTTP capabilities & dynamic features](#http-capabilities--dynamic-features)
8. [Router paths](#router-paths)
9. [Build details](#build-details)
10. [Logging semantics](#logging-semantics)
11. [Testing matrix](#testing-matrix)
12. [Security Concerns](#security-concerns)
13. [Future work](#future-work)

---

## Quick start

```bash
$ make all            # or `make debug` / `make release`
$ make run            # runs on :3490
```

*Press* **`q`** *+ ENTER* in the same terminal to shut down cleanly.

## Directory layout

```text
.
├── Doxyfile
├── Makefile
├── README.md              ← this file
├── TODO.md                # to do file
├── app/                   # all source, include, and external files
│   ├── external/
│   │   ├── cjson/
│   │   └── llhttp/
│   ├── include/
│   │   ├── browser/
│   │   │   ├── handlers/
│   │   └── core/
│   └── src/
│       ├── browser/
│       │   ├── handlers/
│       ├── core/
│       └── main.c
├── utils/
│   ├── MemoryTests/
│   │   └── memcheck_short.sh
│   └── NetworkUtils/
│       ├── connect_11_times.sh
│       ├── connect_11_with_msg.sh
│       └── watch_port.sh
└── var
    ├── lib
    │   └── expenses
    │       ├── 2024
    │       ├── 2025
    │       └── settings.json
    └── www
        ├── assets/
        ├── build_notes/        # static “Build Notes” viewer
        ├── favicon.ico
        ├── images/
        ├── views/
        └── server.log
```

> **.gitignore** (not shown) skips `var/www/images/` binaries, generated docs, IDE folders and the compilation database.

---

## Utils

Under **utils/** you’ll find:

| Path                                  | What it does                                  |
| ------------------------------------- | --------------------------------------------- |
| `MemoryTests/memcheck_short.sh`       | Runs Valgrind with ASan‑like flags            |
| `NetworkUtils/connect_11_times.sh`    | Opens 11 parallel sockets to test max‑clients |
| `NetworkUtils/connect_11_with_msg.sh` | Same but sends a small HTTP request           |
| `NetworkUtils/watch_port.sh`          | `lsof`‑style live view of the listener port   |

---

## Top‑level data‑flow

```text
Main thread (core)
│
├─ Initializes logger, listener, worker, and control subsystems
├─ Starts three threads:
│    ├─ Listener thread: accepts new TCP connections (epoll, non-blocking)
│    │    └─ Forwards client FDs to worker via pipe
│    ├─ Worker thread: event-driven (epoll), manages all client sockets
│    │    └─ Reads new client FDs from pipe, handles all I/O and HTTP requests
│    └─ Control thread: interactive menu (shutdown, info)
└─ Waits for all threads to finish (shutdown or fatal error)
```

---

## Sockets & threading model

| Layer                | Blocking?         | Behaviour                                                      |
| -------------------- | ---------------- | -------------------------------------------------------------- |
| **Listener sockets** | **NON‑blocking** | Managed by listener thread, epoll-based, dual-stack (IPv4/6)   |
| **Client sockets**   | **NON‑blocking** | Managed by worker thread, epoll-based, persistent connections  |

**Admission control:** Max-clients cap enforced in worker.  
**Shutdown:** Atomic status flags signal threads to exit cleanly.  
**Inter-thread communication:** Pipe (listener → worker) for new client FDs.

**Listener sockets:**

- Set **SO_REUSEADDR** to allow immediate rebinding after shutdown.
- Set **SO_LINGER** (zero timeout) so sockets close instantly, discarding unsent data.
- Set **O_NONBLOCK** to ensure accept/read/write never block the listener thread.
- For IPv6, set **IPV6_V6ONLY** to avoid IPv4/IPv6 ambiguity.
- Registered with **epoll** for efficient event-driven notification.

**Client sockets:**

- Set **O_NONBLOCK** so all I/O is non-blocking, allowing the worker thread to multiplex many clients.
- Set **TCP_NODELAY** to disable Nagle’s algorithm, reducing latency for interactive protocols.
- Each client socket is registered with epoll by the worker for read events.
- Connection state (e.g., last activity, request count) is tracked for keep-alive and idle timeout enforcement.
- When a client disconnects or times out, the socket is removed from epoll and closed cleanly.

**Communication pipe (listener → worker):**

- Implemented as a regular UNIX pipe (two file descriptors).
- Both ends are set **O_NONBLOCK** to prevent blocking on read/write.
- The listener writes accepted client file descriptors into the pipe.
- The worker monitors the pipe with epoll and reads new client FDs as they arrive.
- This decouples the accept loop from the worker’s event loop and enables safe, lock-free handoff of new connections.

This setup ensures that all network and inter-thread communication is non-blocking, scalable, and robust against slow or malicious clients. All resource limits and timeouts are enforced in the worker, and the system is designed to recover quickly from restarts or overloads.

---

### Listener socket setup and management

When the server starts, the listener thread creates and configures one or more listening sockets (typically one for IPv4 and one for IPv6). Each socket is set up with the following options and flags to ensure robust, non-blocking, and restart-friendly operation:

- **SO_REUSEADDR** is enabled so that the server can be restarted quickly without waiting for old sockets to leave the TIME_WAIT state. This allows the address/port to be reused immediately after shutdown.
- **SO_LINGER** is set with a zero timeout, which ensures that when the socket is closed, any unsent data is discarded and the socket closes immediately. This prevents sockets from lingering in the system and speeds up restarts.
- **O_NONBLOCK** is set on all sockets so that operations like accept, read, and write do not block the thread. This is essential for event-driven (epoll-based) concurrency and prevents a single slow client from stalling the server.
- **IPV6_V6ONLY** is enabled for IPv6 sockets to ensure they only accept IPv6 connections, avoiding ambiguity and conflicts with IPv4-mapped addresses.
- **TCP_NODELAY** is enabled on accepted client sockets to disable Nagle's algorithm, reducing latency for interactive protocols by sending small packets immediately.
- All listener sockets are registered with **epoll** to efficiently monitor multiple sockets for incoming connections without busy-waiting or polling.

**Lifecycle:**

1. The listener thread calls `getaddrinfo()` with dual-stack hints to obtain suitable addresses.
2. For each address, it creates a socket and applies the above options.
3. Each socket is bound and set to listen with a configurable backlog.
4. All sockets are added to an epoll instance.
5. When a connection arrives, the listener accepts it with `accept4()` (using `SOCK_NONBLOCK`), disables Nagle's algorithm (`TCP_NODELAY`) on the client socket, and forwards the new client file descriptor to the worker thread via a pipe.

This setup ensures that the server can handle many simultaneous incoming connections efficiently and can be restarted without waiting for old sockets to time out.

---

## Timeouts & limits

(see `app/include/core/server_settings.h`)

| Symbol                    | Meaning                           | Default   |
| ------------------------- | --------------------------------- | --------- |
| `MAX_LISTENERS`           | Listening sockets (IPv4 + IPv6)   | **2**     |
| `MAX_CLIENTS`             | Simultaneous clients (epoll)      | **100**   |
| `MAX_PENDING_CONNECTIONS` | `listen()` backlog                | **10**    |
| `SERVER_LOOP_SLEEP_USEC`  | Main loop pause between polls     | **50 ms** |

---

## HTTP capabilities & dynamic features

* **HTTP/1.1 parsing** via **llhttp** in `app/src/browser/http_manager.c`.
  Callbacks (`on_url`, `on_method`, `on_header_field`, `on_header_value`) build an `HttpRequest` struct.
  `determine_connection_policy()` honours `Connection: close`.
* **Request orchestration** in `app/src/browser/browser.c` → `browser_manage_client_req()`.
  Parses → routes → `send_response()` (headers + binary‑safe body).
* **Static file serving** in `app/src/browser/handlers/handler_static.c` – binary‑safe buffer returned to caller (**must free**).
* **JSON APIs** in `app/src/browser/handlers/handlers.c`:

  * **`/api/whoami`**: echos request metadata + server UTC timestamp.
  * **`/api/drive?path=/subdir`**: JSON array of directory entries under `var/www/`.
* **Router paths** (defined in `app/src/browser/router.c`):

  | Path / Prefix    | Handler                                       |
  | ---------------- | --------------------------------------------- |
  | `/`, `/home`     | `pages/index.html` (static)                   |
  | `/assets/…`      | Shared HTML/JS/CSS under `var/www/assets/`    |
  | `/pages/…`       | HTML pages under `var/www/pages/`             |
  | `/images/…`      | Binary files under `var/www/images/`          |
  | `/build_notes/…` | Static files under `var/www/build_notes/`     |
  | `/api/whoami`    | `handler_whoami()` (JSON API)                 |
  | `/api/drive`     | `handler_drive()` (directory listing)         |
  | *anything else*  | `404 Not Found`                               |

---

## Build details

* **Makefile** auto‑detects all `app/src/` C sources, mirrors the directory tree in `build/obj`, and drops the final binary in `build/bin/`.
* Links static libraries from **`app/external/llhttp/`** and **`app/external/cjson/`** (`-Lapp/external/... -lllhttp -lcjson`).
* **Compilation flags**: `-std=c11 -Wall -Wextra -Werror -pedantic` plus `-g` by default; add `-O0` for *debug* and `-O2 -DNDEBUG` for *release*.
* **Targets**

  * `make` (alias of *debug*) – fast build with symbols
  * `make debug` / `make release`
  * `make run` – launch the server in‑place
  * `make clean` – wipe `build/` and other files.

  * **Static‑analysis helpers**:
    * `make format` – clang‑format all `*.c`/`*.h`
    * `make lint` – `cppcheck` on the whole tree (suppressing missing‑system‑includes)
    * `make tidy` – generate `compile_commands.json` with **bear** and run **clang‑tidy** then clean intermediates
    * `make tidy` leaves **compile\_commands.json** in the repo root so editors (VS Code + clangd) get full‑fledged IntelliSense.
* Every build treats warnings as errors – the CI pipeline must always be green.

---

## Logging semantics

* Log file: **`var/www/server.log`** (overwritten each run)
* Format: `[YYYY‑MM‑DD hh:mm:ss] [LEVEL] message`
* Levels: `INFO`, `ERROR` (extend as you wish)
* Every write is flushed so `tail -f var/www/server.log` shows live traffic.

---

## Git pre‑commit hook

A sample `pre-commit` script (drop it in `.git/hooks/`) enforces the quality gates locally running make lint and make format

Running `git commit` guarantees:

1. *cppcheck* passes with no new issues.
2. All touched C source files follow the project style guide.

---

## Testing matrix

| Scenario                                      | Expected result                                                                                   |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Browser `/` + `/assets/style.css` on same TCP socket | Served through keep‑alive; connection persists                                                    |
| Static `/pages/whoami.html` + JS fetch        | HTML delivered; JS fetches `/api/whoami`; clock animates                                          |
| `/pages/drive.html` page                      | JS UI loads; JS fetches `/api/drive`; list renders                                                |
| `/build_notes` page                           | HTML delivered; JS fetches `manifest.json` + notes + diagrams; accordion & PlantUML iframe render |
| JSON `/api/whoami`                            | 200, correct JSON payload, content‑type `application/json`                                        |
| JSON `/api/drive?path=/images`                | 200, array of files (`img1.jpg` …)                                                                |
| 11th parallel client                          | Connection refused (max‑clients = 10)                                                             |
| Client sends `Connection: close`              | Response has `Connection: close`; child exits afterwards                                          |
| Idle >30 s before first request               | Child exits (pre‑handshake timeout)                                                               |
| Idle >120 s after last request                | Child exits (keep‑alive timeout)                                                                  |
| Press `q` in server                           | Parent stops accepting, reaps children, exits cleanly                                             |

---

## Security Concerns

Below is a deep‑dive catalogue of security issues identified in the current multithreaded, event-driven server implementation.  
For each item you will find **the underlying cause**, **the practical impact/attack scenario**, and **actionable mitigations**.

---

### 1. Memory‑ownership mistakes when freeing response bodies

| Aspect     | Details                                                                                                                                                                                                                                                        |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | `browser_manage_client_req()` calls `free((void*)response.body)` unconditionally. If helpers like `send_404()` or `send_405()` set `response.body` to a string literal, freeing it is **undefined behaviour**.                                                |
| **Impact** | Crash (DoS) or heap corruption. An attacker may trigger this by requesting a path that causes a literal to be used as the response body.                                                                                |
| **Fixes**  | ① Add a `needs_free` flag to `HttpResponse`. ② Or always duplicate constant strings with `strdup()`. ③ Use a wrapper like `http_resp_set_body(HttpResponse*, const char *src, bool copy)`.                             |

---

### 2. Incomplete HTTP frame parsing (pipelining / request‑smuggling)

| Aspect     | Details                                                                                                                                                                |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | If you pass the whole `recv()` buffer to `llhttp` once and ignore `parser.off`, pipelined requests (multiple HTTP requests in one TCP packet) may be mishandled.        |
| **Impact** | **Request‑smuggling**: an attacker can tunnel extra requests through a keep-alive socket; subsequent handler sees stale state or misroutes.                            |
| **Fixes**  | ① Loop while `parsed < n` and feed remaining bytes into a fresh `llhttp_t`. ② Maintain a per‑connection buffer + offset for incremental parsing.                      |

---

### 3. Path traversal guards are bypassable

| Aspect     | Details                                                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | Checks like `if(strstr(path, ".."))` may occur **before URL‑decoding** and don’t cover encoded forms (`..%2f`, `%2e%2e/`).                                                                                                |
| **Impact** | Directory traversal → arbitrary file read, e.g. `GET /assets/..%2f..%2fetc/passwd`.                                                                                                |
| **Fixes**  | ① Always call `url_decode()` **first**. ② Use `realpath()` and verify the result begins with your web‑root. ③ Chroot or drop privileges (`setuid(nobody)`).                        |

---

### 4. Blocking `send()` enables slow‑loris style attacks

| Aspect     | Details                                                                                                                                                                                     |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | If you use blocking `send()` in the worker, a slow client can stall a worker thread.                                                                                                        |
| **Impact** | A few malicious clients can exhaust all worker threads, freezing the service.                                                                         |
| **Fixes**  | ① Set `SO_SNDTIMEO` to a few seconds. ② Or use non-blocking sockets and integrate a `poll()`/`epoll()`-based write loop. ③ Consider `TCP_NODELAY` + write retries.                        |

---

### 5. Large‑file integer truncation

| Aspect     | Details                                                                                                                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Cause**  | Using `ftell()`/`fseek()` and casting to `size_t` can truncate files >2 GiB on 32-bit or with `_FILE_OFFSET_BITS=32`.                                                    |
| **Impact** | Partial file reads or huge allocation attempts → crash.                                                                            |
| **Fixes**  | ① Use `struct stat st; fstat(fileno(file), &st); off_t sz = st.st_size;` ② Add a max file size (e.g. `MAX_STATIC_FILE = 50*1024*1024`).                                 |

---

### 6. Hard‑coded protocol limits

| Aspect     | Details                                                                                                                                                                                                          |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | `HTTP_RECEIVE_BUFFER_LEN` is 4 KiB, `HTTP_MAX_HEADER_COUNT` is 20. Modern browsers can send 10 KiB cookie headers easily.                                                                                        |
| **Impact** | Oversized request truncates in the middle of a header → parse error → vague 400 or crash → trivial DoS.                                                                                                          |
| **Fixes**  | ① Grow the buffer dynamically (`realloc`) until a sane max (e.g. 64 KiB). ② If limit is hit, reply `431 Request Header Fields Too Large`. ③ Stream‑parse with `llhttp_execute()` as bytes arrive.               |

---

### 7. Missing transport‑layer security & privilege separation *(defence‑in‑depth)*

| Aspect     | Details                                                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | No TLS, no chroot, no privilege drop.                                                                                                                                                                                    |
| **Impact** | If exposed to the internet, traffic is unencrypted and a compromise is more severe.                                                                                                |
| **Fixes**  | ① Run behind nginx/Caddy for TLS. ② Drop privileges after binding (`setuid(nobody)`). ③ Use chroot/container. ④ Use `seccomp()` or `landlock` to sandbox file system access.        |

---

### 8. Additional concurrency and resource management risks

| Aspect     | Details                                                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Race conditions** | Shared state (e.g. status flags, counters) must be protected with atomics or mutexes. Data races can cause undefined behavior, crashes, or security bugs. **Mitigation:** Audit all shared variables for proper atomic/mutex protection. |
| **Pipe buffer overflow** | If the worker thread is slow or blocked, the listener may fill the pipe buffer and lose client FDs. **Mitigation:** Monitor pipe usage, consider backpressure or a bounded queue. |
| **Resource leaks on thread exit** | If a thread exits abnormally, sockets or memory may not be freed. **Mitigation:** Ensure all cleanup paths are robust; use thread join and error logging. |
| **Unvalidated input in APIs** | JSON and file APIs may not validate all user input (e.g. path, query params). **Mitigation:** Strictly validate and sanitize all user input. |
| **Denial-of-service via many connections** | If `MAX_CLIENTS` is too high or not enforced, a flood of connections can exhaust memory or file descriptors. **Mitigation:** Enforce connection caps, monitor resource usage, and consider per-IP limits. |
| **Log injection** | If user input is logged without sanitization, attackers can inject fake log lines. **Mitigation:** Sanitize or escape user input before logging. |

---

### 9. Future counter‑measures checklist

* Enable **ASLR, RELRO, PIE** at compile time (`-fPIE -pie -Wl,-z,relro,-z,now`).
* Ship a **Content‑Security‑Policy** header in every HTML response to curb XSS.
* Add **rate‑limiting** (token bucket per‑IP) on the accept loop to blunt brute‑force traffic.
* Write unit tests that fuzz‑feed headers and URLs through `http_parse_request()` under AddressSanitizer and UndefinedBehaviourSanitizer.
* Consider using a static analysis tool (e.g. cppcheck, clang-tidy) in CI.
* Regularly review and update all dependencies (even static libraries).
* Document and test all error paths and edge cases.

---

## Future work

* Chunked‑encoding & streamed responses
* MIME‑type auto‑detection beyond simple table
* Extended router: POST, PUT, DELETE, HTTP pipelining
* Optional TLS via a minimal OpenSSL wrapper
* WORK IN PROGRESS: Thread‑pool + `epoll`/`kqueue` for higher concurrency
* CLI flags (port, backlog, www dir)
* In‑band metrics (`/api/metrics`) for Prometheus
* Hot‑reload configuration with `inotify`
* Fuzz tests with libFuzzer

Pull requests & ideas welcome — **happy coding!**
