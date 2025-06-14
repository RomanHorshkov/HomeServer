# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX, Multithreaded)

A compact yet fully‑featured teaching project that demonstrates how to write a **modular, multithreaded TCP + HTTP/1.1 server** in pure C11/POSIX.  
It now serves **dynamic JSON APIs**, **rich client‑side pages**, a **build‑notes viewer**, and a **directory‑driven file browser**, with:

* non‑blocking *listener* sockets (epoll-based)
* event-driven *worker* thread managing all client sockets
* graceful admission control (max‑connections cap)
* structured logging (to `server.log`)
* production‑grade **HTTP/1.1 parsing** via the 🏎️ **[llhttp](https://github.com/nodejs/llhttp)** state‑machine library (static‑linked)
* full request‑header capture (e.g. *User‑Agent*, *Accept*, …)
* proper **Connection: keep‑alive / close** negotiation and persistent sockets (120 s idle timeout)
* serving real **static pages** (`index.html`, `style.css`, **header component**, images, …) from `www/`
* a **JSON API** endpoint (`/api/whoami`) providing request metadata
* a **Drive API** endpoint (`/api/drive?path=/subdir`) that returns JSON directory listings
* a **Build Notes viewer** (`/build_notes`) that turns Markdown + PlantUML source into live docs
* matching HTML front‑ends: **Who Am I** (`whoami.html`), **Drive** (`drive.html`) and **Build Notes** (`build_notes/index.html`)
* an optional **rich animation** demo (`dynamic.html`) with CSS/JS effects
* log‑controlled terminal shutdown (`q`)

Everything builds with **`gcc -std=c11 -Wall -Wextra -Werror -pedantic`** and only the POSIX libc + the bundled **`libllhttp.a`** and **`libcjson.a`**—no external runtime deps.

---

## Table of Contents

1. [Quick start](#quick-start)
2. [Directory layout](#directory-layout)
3. [Utils](#utils)
4. [Top‑level data‑flow](#top-level-data-flow)
5. [Sockets & threading model](#sockets--threading-model)
6. [Timeouts & limits](#timeouts--limits)
7. [HTTP capabilities & dynamic features](#http-capabilities--dynamic-features)
8. [Build details](#build-details)
9. [Logging semantics](#logging-semantics)
10. [Testing matrix](#testing-matrix)
11. [Future work](#future-work)

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
* **Build Notes** (`build_notes/index.html` → loads `/build_notes`)
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
├── src/
│   ├── main
│   ├── browser/
│   │   ├── browser
│   │   ├── handlers
│   │   ├── http_manager
│   │   ├── router
│   │   └── static_page
│   └── core/
│       ├── core
│       ├── listener
│       ├── logger
│       └── worker
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
    ├── build_notes/        # static “Build Notes” viewer
    │   ├── index.html
    │   ├── load-notes.js
    │   ├── manifest.json
    │   ├── diagrams/
    │   └── notes/
    ├── drive.html          # Drive UI – fetches /api/drive
    ├── dynamic.html        # Animation demo
    ├── images/
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

- **Admission control:** Max-clients cap enforced in worker.
- **Shutdown:** Atomic status flags signal threads to exit cleanly.
- **Inter-thread communication:** Pipe (listener → worker) for new client FDs.

---

## Timeouts & limits

(see `include/core/server_settings.h`)

| Symbol                    | Meaning                           | Default   |
| ------------------------- | --------------------------------- | --------- |
| `MAX_LISTENERS`           | Listening sockets (IPv4 + IPv6)   | **2**     |
| `MAX_CLIENTS`             | Simultaneous clients (epoll)      | **100**   |
| `MAX_PENDING_CONNECTIONS` | `listen()` backlog                | **10**    |
| `SERVER_LOOP_SLEEP_USEC`  | Main loop pause between polls     | **50 ms** |

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

  | Path / Prefix    | Handler                                       |
  | ---------------- | --------------------------------------------- |
  | `/`, `/home`     | `index.html` (static)                         |
  | `/style.css`     | `style.css` (static)                          |
  | `/whoami`        | `whoami.html` (static)                        |
  | `/dynamic`       | `dynamic.html` (static)                       |
  | `/drive`         | `drive.html` (static JS page)                 |
  | `/build_notes`   | `build_notes/index.html` (static + client JS) |
  | `/build_notes/…` | Static files under `www/build_notes/`         |
  | `/api/whoami`    | `whoami_json_handler()` (JSON API)            |
  | `/api/drive`     | `drive_json_handler()` (directory listing)    |
  | `/images/…`      | Binary files under `www/images/`              |
  | `/assets/…`      | Shared HTML/JS bits under `www/assets/`       |
  | *anything else*  | `404 Not Found`                               |

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

## Logging semantics

* Log file: **`server.log`** (overwritten each run)
* Format: `[YYYY‑MM‑DD hh:mm:ss] [LEVEL] message`
* Levels: `INFO`, `ERROR` (extend as you wish)
* Every write is flushed so `tail -f server.log` shows live traffic.

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

| Scenario                                      | Expected result                                                                                   |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Browser `/` + `/style.css` on same TCP socket | Served through keep‑alive; connection persists                                                    |
| Static `/whoami.html` + JS fetch              | HTML delivered; JS fetches `/api/whoami`; clock animates                                          |
| `/drive` page                                 | JS UI loads; JS fetches `/api/drive`; list renders                                                |
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
