# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX)

A compact yet fully‑featured teaching project that demonstrates how to write a modular **preforking TCP + HTTP/1.1 server** in pure C11/POSIX, with:

* non‑blocking *listener* sockets
* blocking *client* sockets protected by **SO\_RCVTIMEO** (short‑then‑long)
* graceful admission control (max‑connections cap)
* per‑client **fork()** isolation
* structured logging (to `server.log`)
* production‑grade **HTTP/1.1 parsing** via the 🏎️ **[llhttp](https://github.com/nodejs/llhttp)** state‑machine library (static‑linked)
* full request‑header capture (e.g. *User‑Agent*, *Accept*, …)
* proper **Connection: keep‑alive/close** negotiation and persistent sockets (120 s idle timeout)
* serving real **static pages** (`index.html`, `style.css`) from `www/`
* log‑controlled terminal shutdown (`q`)

Everything builds with **`gcc -std=c11 -Wall -Wextra -Werror -pedantic`** and only the POSIX libc + the bundled `libllhttp.a`—no threads, no external runtime deps.

---

## Table of Contents

1. [Quick start](#quick-start)
2. [Directory layout](#directory-layout)
3. [Top‑level data‑flow](#top-level-data-flow)
4. [Sockets & blocking model](#sockets--blocking-model)
5. [Fork lifecycle](#fork-lifecycle)
6. [Timeouts & limits](#timeouts--limits)
7. [HTTP capabilities](#http-capabilities)
8. [Build details](#build-details)
9. [Logging semantics](#logging-semantics)
10. [Testing matrix](#testing-matrix)
11. [Future work](#future-work)

---

## Quick start

```bash
$ make            # or `make debug` / `make release`
$ ./build/bin/server
```

*Press* **`q`** *+ ENTER* in the same terminal to shut down cleanly.

### Try it

```bash
# Terminal‑1 (server)
$ ./build/bin/server

# Browser
http://localhost:3490/
```

You should see a styled **index.html** page with **style.css** loaded on the **same TCP socket** thanks to HTTP keep‑alive.

---

## Directory layout

```
.
├── Makefile
├── README.md             ← this file
├── browser/
│   ├── inc/              ← high‑level HTTP API
│   └── src/              ← router + http_manager (uses llhttp)
├── inc/                  ← core server headers
├── libraries/
│   └── llhttp/           ← **libllhttp.a** + public header
├── src/                  ← core TCP / fork() logic
└── www/                  ← static assets
```

---

## Top‑level data‑flow

```
Parent process (main loop)
│
├── accept() client socket (non‑blocking listeners)
├── fork()
│   ├── Child process
│   │   ├── Close listeners
│   │   ├── clients_handle_client(fd)
│   │   │   ├── recv() HTTP request (may loop for keep‑alive)
│   │   │   ├── llhttp_parse() → HttpRequest
│   │   │   ├── router → HttpResponse
│   │   │   ├── build HTTP response (Connection: keep‑alive/close)
│   │   │   └── send() response
│   │   └── Close fd and _exit(0) when idle >|120 s|
│   └── Parent process
│       ├── track PID + fd
│       └── continue
└── reap zombie children (waitpid)
```

---

## Sockets & blocking model

| Layer                | Blocking?                                                                   | Key options & behaviour                                                                |
| -------------------- | --------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **Listeners**        | **NON‑blocking** (`O_NONBLOCK`)                                             | `SO_REUSEADDR`, `SO_LINGER{1,0}`, dual‑stack (IPv4 + IPv6)                             |
| **Accepted clients** | *Blocking* with **SO\_RCVTIMEO**: 30 s pre‑handshake ⇒ 120 s post‑handshake | Persistent; same socket reused for multiple requests until idle or `Connection: close` |

---

## Fork lifecycle

1. `accept()` new connection
2. `fork()`

   * **Child**: handles client end‑to‑end, loops for keep‑alive, exits when `should_close` or timeout.
   * **Parent**: tracks child PID, returns to accept‑loop.
3. Parent reaps zombies via non‑blocking `waitpid()`.

---

## Timeouts & limits

(defined in `inc/server_settings.h`)

| Symbol                    | Meaning                             | Default   |
| ------------------------- | ----------------------------------- | --------- |
| `MAX_LISTENERS`           | Max listening sockets (IPv4 + IPv6) | **2**     |
| `MAX_CLIENTS`             | Max simultaneous children           | **10**    |
| `MAX_PENDING_CONNECTIONS` | `listen()` backlog                  | **10**    |
| `CLIENT_MAX_TIMEOUT_S`    | Pre‑handshake idle timeout          | **30 s**  |
| `CLIENT_MAX_TIMEOUT_S_L`  | Post‑handshake keep‑alive timeout   | **120 s** |
| `SERVER_SLEEP_TIME_*`     | Parent loop sleep                   | **50 ms** |

---

## HTTP capabilities

* **HTTP/1.1 compliant parsing** via **llhttp** (Node.js engine)
* Full header capture (case‑insensitive) → logged nicely
* Handles **`Connection: keep‑alive`** automatically (default)
* Respects **`Connection: close`** request header
* Supports only **`GET`** for now
* Simple router:

  * `/` or `/home` → `index.html`
  * `/style.css`      → `style.css`
  * Anything else     → `404 Not Found`
* Static files served from **`www/`**

---

## Build details

* **Makefile** auto‑detects `src/*.c` & `browser/src/*.c`.
* Links against bundled **`libraries/llhttp/libllhttp.a`**.
* Build targets: `make`, `make debug`, `make release`, `make run`, `make clean`.
* All warnings are treated as errors (`-Werror`) — clean builds only.

---

## Logging semantics

* Logfile: **`server.log`** (truncated each run).
* Format: `[YYYY-MM-DD hh:mm:ss] [LEVEL] message`.
* Levels: `INFO`, `ERROR`.
* Flushes on every write for tail‑friendly debugging.

---

## Testing matrix

| Scenario                                              | Expected result                                                   |
| ----------------------------------------------------- | ----------------------------------------------------------------- |
| Browser request `/` + `/style.css` on same TCP socket | Served correctly; single child process; persistent connection     |
| Browser sends `Connection: close`                     | Child returns response with `Connection: close` and closes socket |
| Connect 11× clients in parallel                       | 11th refused (max clients = 10)                                   |
| Abrupt client disconnect                              | Child exits cleanly, parent reaps                                 |
| Client idle > 30 s before first request               | Child exits                                                       |
| Client idle > 120 s after last request                | Child exits                                                       |
| Press `q` in server                                   | Parent closes listeners, waits for children, exits                |

---

## Future work

* Chunked‐encoding + streamed responses
* MIME‑type detection for arbitrary assets
* Simple templating + dynamic `/info` JSON endpoint
* Optional TLS (via `openssl` wrappers)
* Switch to thread‑pool + `epoll` for high concurrency
* CLI flags for port, backlog, www dir
* **HTTP/1.1 pipelining** support
* Explore HTTP/2 upgrade via `nghttp2`

Pull requests and ideas welcome!

