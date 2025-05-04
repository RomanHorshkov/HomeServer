# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX)

A compact yet fully‑featured teaching project that demonstrates how to write a **modular, preforking TCP + HTTP/1.1 server** in pure C11/POSIX.
It now serves **dynamic JSON APIs**, **rich client‑side pages** and a **directory‑driven file browser**, with:

* non‑blocking *listener* sockets
* blocking *client* sockets protected by **SO\_RCVTIMEO** (short‑then‑long)
* graceful admission control (max‑connections cap)
* per‑client **fork()** isolation
* structured logging (to `server.log`)
* production‑grade **HTTP/1.1 parsing** via the 🏎️ **[llhttp](https://github.com/nodejs/llhttp)** state‑machine library (static‑linked)
* full request‑header capture (e.g. *User‑Agent*, *Accept*, …)
* proper **Connection: keep‑alive / close** negotiation and persistent sockets (120 s idle timeout)
* serving real **static pages** (`index.html`, `style.css`, **header component**, images, …) from `www/`
* a **JSON API** endpoint (`/api/whoami`) providing request metadata
* a **Drive API** endpoint (`/api/drive?path=/subdir`) that returns JSON directory listings
* matching HTML front‑ends: **Who Am I** (`whoami.html`) and **Drive** (`drive.html`)
* an optional **rich animation** demo (`dynamic.html`) with CSS/JS effects
* log‑controlled terminal shutdown (`q`)

Everything builds with **`gcc -std=c11 -Wall -Wextra -Werror -pedantic`** and only the POSIX libc + the bundled **`libllhttp.a`** and **`libcjson.a`**—no threads, no external runtime deps.

---

## Table of Contents

1. [Quick start](#quick-start)
2. [Directory layout](#directory-layout)
3. [Utils](#utils)
4. [Top‑level data‑flow](#top-level-data-flow)
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

*Press* **`q`** *+ ENTER* in the same terminal to shut down cleanly.

### Try it

```bash
# Terminal‑1 (server)
$ ./build/bin/server

# Browser
http://localhost:3490/
```

You should see the styled **index.html** page with **header.html** included, plus links to:

* **Who Am I** (`whoami.html` → calls `/api/whoami`)
* **Drive** (`drive.html` → calls `/api/drive`)
* **Dynamic demo** (`dynamic.html` – CSS/JS animations)

---

## Directory layout

```text
.
├── Doxyfile
├── Makefile
├── README.md              ← this file
├── compile_commands.json   # clangd / tooling database
├── copy_project.sh         # helper snapshot script
├── external/               # third‑party static libraries
│   ├── cjson/
│   │   ├── cJSON.h
│   │   └── libcjson.a
│   └── llhttp/
│       ├── libllhttp.a
│       └── llhttp.h
├── include/
│   ├── browser/
│   │   ├── browser.h
│   │   ├── handlers.h
│   │   ├── http_manager.h
│   │   ├── router.h
│   │   └── static_page.h
│   └── core/
│       ├── client.h
│       ├── core.h
│       ├── listener.h
│       ├── logger.h
│       ├── server_input.h
│       └── server_settings.h
├── src/
│   ├── browser/
│   │   ├── browser.c
│   │   ├── handlers.c
│   │   ├── http_manager.c
│   │   ├── router.c
│   │   └── static_page.c
│   └── core/
│       ├── client.c
│       ├── core.c
│       ├── listener.c
│       ├── logger.c
│       ├── server.c
│       └── server_input.c
├── utils/
│   ├── MemoryTests/
│   │   └── memcheck_short.sh
│   └── NetworkUtils/
│       ├── connect_11_times.sh
│       ├── connect_11_with_msg.sh
│       └── watch_port.sh
└── www/
    ├── assets/
    │   ├── header.html     # reusable navbar / hero component
    │   └── header.js       # helper script to inject header
    ├── drive.html          # Drive UI – fetches /api/drive
    ├── dynamic.html        # Animation demo
    ├── images/
    │   ├── html_headers_viasual_representation.png
    │   ├── img1.jpg
    │   ├── img2.jpg
    │   ├── img3.jpg
    │   └── img4.jpg
    ├── index.html
    ├── style.css
    └── whoami.html         # fetches /api/whoami
```

> **.gitignore** (not shown) skips `www/images/` binaries, generated docs, IDE folders and the compilation database.

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
Parent (main)
│
├─ accept() new client FD (non‑blocking listener)
├─ fork()
│  ├─ Child:
│  │   ├─ close other listeners
│  │   ├─ clients_handle_client(fd):
│  │   │   ├─ recv() HTTP request(s)
│  │   │   ├─ llhttp_parse() → HttpRequest struct
│  │   │   ├─ router_handle_request() → HttpResponse
│  │   │   │       static_page | whoami_json | drive_json
│  │   │   ├─ send_response() (headers + body, Connection handling)
│  │   │   └─ repeat / exit on close or timeouts
│  │   └─ _exit()
│  └─ Parent: track PID, reap zombies, continue
└─ waitpid() reaper
```

---

## Sockets & blocking model

| Layer                | Blocking?                                                              | Behaviour                                                      |
| -------------------- | ---------------------------------------------------------------------- | -------------------------------------------------------------- |
| **Listener sockets** | **NON‑blocking** (`O_NONBLOCK`)                                        | `SO_REUSEADDR`, dual‑stack (IPv4 + IPv6)                       |
| **Client sockets**   | *Blocking* with **SO\_RCVTIMEO**: 30 s pre‑handshake, 120 s keep‑alive | Negotiated via `Connection:` header; child exits on close/idle |

---

## Fork lifecycle

1. `accept()` new connection
2. `fork()` child

   * **Child**: one‑socket loop → exit on close/timeout
   * **Parent**: continues accepting, limits total children
3. `waitpid()` periodically reaps exited children

---

## Timeouts & limits

(see `include/core/server_settings.h`)

| Symbol                    | Meaning                           | Default   |
| ------------------------- | --------------------------------- | --------- |
| `MAX_LISTENERS`           | Listening sockets (IPv4 + IPv6)   | **2**     |
| `MAX_CLIENTS`             | Simultaneous child processes      | **10**    |
| `MAX_PENDING_CONNECTIONS` | `listen()` backlog                | **10**    |
| `CLIENT_MAX_TIMEOUT_S`    | Pre‑handshake idle timeout        | **30 s**  |
| `CLIENT_MAX_TIMEOUT_S_L`  | Post‑handshake keep‑alive timeout | **120 s** |
| `SERVER_LOOP_SLEEP_USEC`  | Parent loop pause between polls   | **50 ms** |

---

## HTTP capabilities & dynamic features

* **HTTP/1.1 parsing** via **llhttp** in `src/browser/http_manager.c`.
  Callbacks (`on_url`, `on_method`, `on_header_field`, `on_header_value`) build an `HttpRequest` struct.
  `determine_connection_policy()` honours `Connection: close`.
* **Request orchestration** in `src/browser/browser.c` → `browser_manage_client_req()`.
  Parses → routes → `send_response()` (headers + binary‑safe body).
* **Static file serving** in `src/browser/static_page.c` – binary‑safe buffer returned to caller (**must free**).
* **JSON APIs** in `src/browser/handlers.c`:

  * **`/api/whoami`**: echos request metadata + server UTC timestamp.
  * **`/api/drive?path=/subdir`**: JSON array of directory entries under `www/`.
* **Router paths** (defined in `src/browser/router.c`):

  | Path / Prefix   | Handler                                    |
  | --------------- | ------------------------------------------ |
  | `/`, `/home`    | `index.html` (static)                      |
  | `/style.css`    | `style.css` (static)                       |
  | `/whoami`       | `whoami.html` (static)                     |
  | `/dynamic`      | `dynamic.html` (static)                    |
  | `/drive`        | `drive.html` (static React‑less JS page)   |
  | `/api/whoami`   | `whoami_json_handler()` (JSON API)         |
  | `/api/drive`    | `drive_json_handler()` (directory listing) |
  | `/images/…`     | Binary files under `www/images/`           |
  | `/assets/…`     | Shared HTML/JS bits under `www/assets/`    |
  | *anything else* | `404 Not Found`                            |

---

## Build details

* **Makefile** auto‑detects all `src/` and `src/browser/` C sources, mirrors the directory tree in `build/obj`, and drops the final binary in `build/bin/`.
* Links static libraries from **`external/llhttp/`** and **`external/cjson/`** (`-Lexternal/... -lllhttp -lcjson`).
* **Compilation flags**: `-std=c11 -Wall -Wextra -Werror -pedantic` plus `-g` by default; add `-O0` for *debug* and `-O2 -DNDEBUG` for *release*.
* **Targets**

  * `make` (alias of *debug*) – fast build with symbols
  * `make debug` / `make release`
  * `make run` – launch the server in‑place
  * `make clean` – wipe `build/` + `server.log`
  * **Static‑analysis helpers**:

    * `make format` – clang‑format all `*.c`/`*.h` (or staged files via `FILES=...`)
    * `make lint` – `cppcheck` on the whole tree (suppressing missing‑system‑includes)
    * `make tidy` – generate `compile_commands.json` with **bear** and run **clang‑tidy** (disabling the *unsafe buffer* warning) then clean intermediates
* `make tidy` leaves **compile\_commands.json** in the repo root so editors (VS Code + clangd) get full‑fledged IntelliSense.
* Every build treats warnings as errors – the CI pipeline must always be green.

---

## Git pre‑commit hook

A sample `pre-commit` script (drop it in `.git/hooks/`) enforces the quality gates locally:

```bash
#!/usr/bin/env bash

echo "🔍 Running pre‑commit checks..."

if ! make lint; then
    echo "❌ Lint failed. Fix the issues before committing."
    exit 1
else
    echo "✅ lint passed."
fi

# Get staged .c/.h files
STAGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h)$')

if [ -n "$STAGED" ]; then
    # Auto‑format in place and re‑stage
    make format FILES="$STAGED" > /dev/null
    git add $STAGED
    echo "✅ formatting applied."
else
    echo "ℹ️  No staged C/header files — skipping format."
fi

echo "✅ Pre‑commit passed."
```

Running `git commit` now guarantees:

1. *cppcheck* passes with no new issues.
2. All touched C source files follow the project style guide.

Feel free to extend the hook with `make tidy` or unit‑test execution once the test‑suite lands.

---

## Logging semantics

* Log file: **`server.log`** (overwritten each run)
* Format: `[YYYY‑MM‑DD hh:mm:ss] [LEVEL] message`
* Levels: `INFO`, `ERROR` (extend as you wish)
* Every write is flushed so `tail -f server.log` shows live traffic.

---

## Testing matrix

| Scenario                                      | Expected result                                            |
| --------------------------------------------- | ---------------------------------------------------------- |
| Browser `/` + `/style.css` on same TCP socket | Served through keep‑alive; connection persists             |
| Static `/whoami.html` + JS fetch              | HTML delivered; JS fetches `/api/whoami`; clock animates   |
| `/drive` page                                 | React‑less UI loads; JS fetches `/api/drive`; list renders |
| JSON `/api/whoami`                            | 200, correct JSON payload, content‑type `application/json` |
| JSON `/api/drive?path=/images`                | 200, array of files (`img1.jpg` …)                         |
| 11th parallel client                          | Connection refused (max‑clients = 10)                      |
| Client sends `Connection: close`              | Response has `Connection: close`; child exits afterwards   |
| Idle >30 s before first request               | Child exits (pre‑handshake timeout)                        |
| Idle >120 s after last request                | Child exits (keep‑alive timeout)                           |
| Press `q` in server                           | Parent stops accepting, reaps children, exits cleanly      |

---

## Future work

* Chunked‑encoding & streamed responses
* MIME‑type auto‑detection beyond simple table
* Extended router: POST, PUT, DELETE, HTTP pipelining
* Optional TLS via a minimal OpenSSL wrapper
* Thread‑pool + `epoll`/`kqueue` for higher concurrency
* CLI flags (port, backlog, www dir)
* In‑band metrics (`/api/metrics`) for Prometheus
* Hot‑reload configuration with `inotify`
* Fuzz tests with libFuzzer

Pull requests & ideas welcome — **happy coding!**
