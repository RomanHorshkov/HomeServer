# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX)

A compact yet fully-featured teaching project that demonstrates how to write a modular **preforking TCP + HTTP/1.1 server** in pure C11/POSIX, now extended to serve **dynamic JSON** and **rich client-side pages**, with:

* non-blocking *listener* sockets  
* blocking *client* sockets protected by **SO_RCVTIMEO** (short-then-long)  
* graceful admission control (max-connections cap)  
* per-client **fork()** isolation  
* structured logging (to `server.log`)  
* production-grade **HTTP/1.1 parsing** via the 🏎️ **[llhttp](https://github.com/nodejs/llhttp)** state-machine library (static-linked)  
* full request-header capture (e.g. *User-Agent*, *Accept*, …)  
* proper **Connection: keep-alive/close** negotiation and persistent sockets (120 s idle timeout)  
* serving real **static pages** (`index.html`, `style.css`) from `www/`  
* a **JSON API** endpoint (`/api/whoami`) providing request metadata  
* a **dynamic HTML+JS** page (`whoami.html`) that fetches and animates server data  
* an optional **rich animation** demo (`dynamic.html`) with CSS/JS effects  
* log-controlled terminal shutdown (`q`)

Everything builds with **`gcc -std=c11 -Wall -Wextra -Werror -pedantic`** and only the POSIX libc + the bundled **`libllhttp.a`** and **`libcjson.a`**—no threads, no external runtime deps.

---

## Table of Contents

1. [Quick start](#quick-start)  
2. [Directory layout](#directory-layout)  
3. [Utils](#utils)  
4. [Top-level data-flow](#top-level-data-flow)  
5. [Sockets & blocking model](#sockets--blocking-model)  
6. [Fork lifecycle](#fork-lifecycle)  
7. [Timeouts & limits](#timeouts--limits)  
8. [HTTP capabilities & dynamic features](#http-capabilities--dynamic-features)  
9. [Build details](#build-details)  
10. [Logging semantics](#logging-semantics)  
11. [Testing matrix](#testing-matrix)  
12. [Future work](#future-work)  

---

## Quick start

```bash
$ make            # or `make debug` / `make release`
$ ./build/bin/server
```

*Press* **`q`** *+ ENTER* in the same terminal to shut down cleanly.

### Try it

```bash
# Terminal-1 (server)
$ ./build/bin/server

# Browser
http://localhost:3490/
```

You should see a styled **index.html** page with **style.css**, plus links to **Who Am I** (`whoami.html`) and **Dynamic Demo** (`dynamic.html`).

---

## Directory layout

```
.
├── .clang-format
├── .gitignore
├── Doxyfile
├── Makefile
├── README.md             ← this file
├── Utils/                ← helper scripts for testing & memchecks
│   ├── MemoryTests
│   │   └── memcheck_short.sh
│   └── NetworkUtils
│       ├── connect_11_times.sh
│       ├── connect_11_with_msg.sh
│       └── watch_port.sh
├── browser/
│   ├── inc/              ← high-level HTTP API (router.h, handlers.h, etc.)
│   └── src/              ← browser.c, handlers.c, http_manager.c, router.c, static_page.c
├── inc/                  ← core server headers (listener.h, server_settings.h, …)
├── libraries/
│   ├── cjson/            ← libcjson.a + cJSON.h
│   └── llhttp/           ← libllhttp.a + llhttp.h
├── src/                  ← core TCP/fork logic (server.c, listener.c, core.c, etc.)
└── www/                  ← static assets + client-side pages
    ├── dynamic.html      ← CSS/JS animation demo
    ├── images/           ← (ignored by git) slideshow JPEGs
    │   ├── img1.jpg
    │   ├── img2.jpg
    │   ├── img3.jpg
    │   └── img4.jpg
    ├── index.html
    ├── style.css
    └── whoami.html       ← static page that fetches `/api/whoami`
```

**`.gitignore`** includes:
```
.clang-format
/docs
.vscode/
www/images/
```

---

## Utils

Under **Utils/** you’ll find:

- **MemoryTests/** – scripts to run Valgrind memchecks on your binaries.  
- **NetworkUtils/** – simple Bash scripts to hammer the server with multiple connections or monitor port activity.  

---

## Top-level data-flow

```text
Parent (main)
│
├─ accept() new client FD (non-blocking listener)
├─ fork()
│  ├─ Child:
│  │   ├─ close other listeners
│  │   ├─ clients_handle_client(fd):
│  │   │   ├─ recv() HTTP request(s)
│  │   │   ├─ llhttp_parse() → HttpRequest struct
│  │   │   ├─ router_handle_request() → HttpResponse
│  │   │   │       static pages OR JSON API handler
│  │   │   ├─ build HTTP response (Connection negotiation)
│  │   │   └─ send() response
│  │   └─ exit after idle/timeouts
│  └─ Parent: track PID, reap zombies, continue
└─ waitpid() reaper
```

---

## Sockets & blocking model

| Layer                | Blocking?                                                                  | Behavior                                                      |
| -------------------- | -------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **Listener sockets** | **NON-blocking** (`O_NONBLOCK`)                                            | `SO_REUSEADDR`, dual-stack (IPv4 + IPv6)                     |
| **Client sockets**   | *Blocking* with **SO_RCVTIMEO**: 30 s pre-handshake, 120 s post-handshake  | HTTP/1.1 keep-alive, closes on `Connection: close` or timeout |

---

## Fork lifecycle

1. `accept()` new connection  
2. `fork()`

   * **Child**: single-responsibility handler loop → exit on close/timeout  
   * **Parent**: manages listener, limits, and reaps children  
3. `waitpid()` or signal handler to clean up zombies  

---

## Timeouts & limits

(see `inc/server_settings.h`)

| Symbol                    | Meaning                           | Default   |
| ------------------------- | --------------------------------- | --------- |
| `MAX_LISTENERS`           | Listening sockets (IPv4 + IPv6)   | **2**     |
| `MAX_CLIENTS`             | Simultaneous child processes      | **10**    |
| `MAX_PENDING_CONNECTIONS` | `listen()` backlog                | **10**    |
| `CLIENT_MAX_TIMEOUT_S`    | Pre-handshake idle timeout        | **30 s**  |
| `CLIENT_MAX_TIMEOUT_S_L`  | Post-handshake keep-alive timeout | **120 s** |
| `SERVER_LOOP_SLEEP_USEC`  | Ephemeral parent loop pause       | **50 ms** |

---

## HTTP capabilities & dynamic features

* **HTTP/1.1 parsing** via **llhttp** (Node.js engine port) in `browser/src/http_manager.c`
  * Callbacks in `on_url`, `on_method`, `on_header_field`, `on_header_value` fill the `HttpRequest` struct  
  * Connection policy determined by `determine_connection_policy()` (keep-alive vs close)  
* **Request orchestration** in `browser/src/browser.c`:
  * `browser_manage_client_req()` calls `http_parse_request()`, then `router_handle_request()`, then `http_build_response()`  
* **Building responses** in `browser/src/http_manager.c`:
  * `http_build_response()` uses `snprintf()` to assemble status line, headers, and body into the send buffer  
* **Static file serving** in `browser/src/static_page.c`:
  * `static_page_serve_file()` opens files under `www/`, `fseek()`/`ftell()` to determine size, `malloc()` + `fread()` to load content  
  * **Note:** the allocated buffer is passed to `HttpResponse.body` and later freed by the caller after sending  
* **JSON API** (`/api/whoami`) implemented in `browser/src/handlers.c`:
  * Uses **cJSON** (`libraries/cjson/libcjson.a`) to build a JSON object with:
    * `server_time` (ISO 8601 with ms)  
    * `method`, `path`  
    * all request headers in an object  
  * Serializes with `cJSON_PrintUnformatted()`; the returned `char*` must be freed when no longer needed  

**Router paths** (defined in `browser/src/router.c`):

| Path          | Handler                                    |
| ------------- | ------------------------------------------ |
| `/`, `/home`  | `index.html` (static_page_serve_file)      |
| `/style.css`  | `style.css` (static_page_serve_file)       |
| `/whoami`     | `whoami.html` (static_page_serve_file)     |
| `/dynamic`    | `dynamic.html` (static_page_serve_file)    |
| `/api/whoami` | `whoami_json_handler()` (JSON API)         |
| *otherwise*   | `404 Not Found`                            |

---

## Build details

* **Makefile** auto-detects C sources in `src/` and `browser/src/`.  
* Links static **`-lllhttp`** and **`-lcjson`** from `libraries/llhttp/` and `libraries/cjson/`.  
* Targets: `make`, `make debug`, `make release`, `make run`, `make clean`.  
* All warnings are errors (`-Werror`) — ensure clean builds.  

---

## Logging semantics

* Log file: **`server.log`** (overwritten each run)  
* Format: `[YYYY-MM-DD hh:mm:ss] [LEVEL] message`  
* Levels: `INFO`, `ERROR`  
* Flush on every write for real-time tailing  

---

## Testing matrix

| Scenario                                      | Expected result                                                  |
| --------------------------------------------- | ---------------------------------------------------------------- |
| Browser `/` + `/style.css` on same TCP socket | Served on same connection; persistent if no `Connection: close`  |
| Static `/whoami.html` load + JS fetch         | HTML delivered; JS fetches `/api/whoami`; clock animates locally |
| JSON `/api/whoami`                            | Correct JSON payload; proper headers; no side-effects            |
| CSS/JS animations on `/dynamic.html`          | Background gradient, typewriter, slideshow, pulse animations run |
| Client sends `Connection: close`              | Response with `Connection: close`; child closes socket           |
| >10 parallel clients                          | 11th connection refused (max clients = 10)                       |
| Idle >30 s before first req                   | Child exits                                                      |
| Idle >120 s after last req                    | Child exits                                                      |
| Press `q` in server                           | Parent stops accepting, reaps children, exits                    |

---

## Future work

* Chunked-encoding & streamed responses  
* MIME-type auto-detection  
* Extended router: POST, PUT, DELETE, pipelining  
* Optional TLS via OpenSSL wrappers  
* Thread-pool + `epoll` for high concurrency  
* CLI flags (port, backlog, www dir)  
* Additional JSON endpoints for metrics, config  

Pull requests & ideas welcome!  
Happy coding!  
