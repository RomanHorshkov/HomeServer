# Concurrent TCP + HTTP/1.1 Server (C11 / POSIX)

A compact yet fully‑featured teaching project that demonstrates how to write a **modular, preforking TCP + HTTP/1.1 server** in pure C11/POSIX.
It now serves **dynamic JSON APIs**, **rich client‑side pages**, a **build‑notes viewer**, and a **directory‑driven file browser**, with:

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
* a **Build Notes viewer** (`/build_notes`) that turns Markdown + PlantUML source into live docs
* matching HTML front‑ends: **Who Am I** (`whoami.html`), **Drive** (`drive.html`) and **Build Notes** (`build_notes/index.html`)
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
    ├── build_notes/        # static “Build Notes” viewer
    │   ├── index.html
    │   ├── load-notes.js
    │   ├── manifest.json
    │   ├── diagrams/
    │   │   └── example.puml
    │   └── notes/
    │       ├── intro.md
    │       └── login_flow.md
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
│  │   │   │       static_page | whoami_json | drive_json | build_notes_static
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

Below is a deep‑dive catalogue of security issues that emerged during the review.
For each item you will find **the underlying cause**, **the practical impact/attack scenario**, and **actionable mitigations**.

---

### 1. Memory‑ownership mistakes when freeing response bodies

| Aspect     | Details                                                                                                                                                                                                                                                        |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | `browser_manage_client_req()` calls `free((void*)response.body)` unconditionally. When helpers like `send_404()` or `send_405()` set `response.body` to a string literal, that literal lives in read‑only program text; freeing it is **undefined behaviour**. |
| **Impact** | Depending on libc build‑time sanity checks, you get a crash (DoS) or silent heap corruption—an attacker may be able to craft a sequence of requests that hit the literal and then trigger reuse of the freed pointer.                                          |
| **Fixes**  | ① Add a `needs_free` flag to `HttpResponse`. ② Or always duplicate constant strings with `strdup()` so everything is heap‑owned. ③ Consider a tiny wrapper `http_resp_set_body(HttpResponse*, const char *src, bool copy)`.                                    |

---

### 2. Fork‑per‑client model → resource‑exhaustion DoS

| Aspect     | Details                                                                                                                                                                                                                                             |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | Every new TCP connection triggers a `fork()`. On a busy port a malicious user can open thousands of parallel sockets, each consuming a full process image, stack, libc TLS and COW pages.                                                           |
| **Impact** | Rapid spike in PIDs, context‑switch overhead and memory: easy to hit `/proc/sys/kernel/pid_max` or the soft `ulimit -u`. Legitimate clients start timing‑out.                                                                                       |
| **Fixes**  | ① Switch to a single‑process event loop (epoll/kqueue) or a fixed worker pool that accepts on a shared socket. ② Add `MaxClients` limit at the accept layer and fail fast with `SO_REUSEPORT`. ③ Use `setrlimit(RLIMIT_NPROC, …)` to self‑throttle. |

---

### 3. Shared log file descriptor across forks

| Aspect     | Details                                                                                                                                                                                                                                         |
| ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | Parent and every child inherit the same `FILE *` opened in write‑mode. Writes are **not atomic** at the stdio layer—`fprintf()` buffers can overlap.                                                                                            |
| **Impact** | Garbled or truncated log lines break forensic value. Attackers could exploit predictable interleaving to obscure traces.                                                                                                                        |
| **Fixes**  | ① Re‑open the log in each child **after** `fork()` with `O_APPEND`. ② Or call `setvbuf(log_file,NULL,_IONBF,0)` to disable buffering (still risk torn writes). ③ Or ditch the file and use `syslog()`—the daemon manages serialisation for you. |

---

### 4. Incomplete HTTP frame parsing (pipelining / request‑smuggling)

| Aspect     | Details                                                                                                                                                                |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | You pass the whole `recv()` buffer to `llhttp` once and ignore `parser.off`. If two requests arrive back‑to‑back (pipelined) the surplus bytes are silently discarded. |
| **Impact** | **Request‑smuggling**: an attacker tunnels an extra request through an idle keep‑alive socket; subsequent handler sees stale state or mis‑routes.                      |
| **Fixes**  | ① Loop while `parsed < n` and feed remaining bytes into a fresh `llhttp_t`. ② Maintain a per‑connection buffer + offset so you can append and parse incrementally.     |

---

### 5. Path traversal guards are bypassable

| Aspect     | Details                                                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | Checks like `if(strstr(path, ".."))` occur **before URL‑decoding** in some branches and don’t cover encoded forms (`..%2f`, `%2e%2e/`).                                                                                   |
| **Impact** | Classic directory traversal → arbitrary file read, e.g. `GET /assets/..%2f..%2fetc/passwd`.                                                                                                                               |
| **Fixes**  | ① Always call `url_decode()` **first**. ② Use `realpath()` on the assembled path and verify the result begins with your web‑root prefix. ③ Chroot or drop privileges (`setuid(nobody)`) so escapes are less catastrophic. |

---

### 6. Blocking `send()` enables slow‑loris style attacks

| Aspect     | Details                                                                                                                                                                                     |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | Child sockets remain in blocking mode. A client that ACKs each TCP segment slowly stalls the process inside `send_all()`.                                                                   |
| **Impact** | Ten malicious sockets → ten hung processes → service freeze despite low network traffic.                                                                                                    |
| **Fixes**  | ① Set `SO_SNDTIMEO` to a few seconds. ② Or switch sockets to non‑blocking and integrate a `poll()`/`epoll()`‑based write loop with rate limiting. ③ Consider `TCP_NODELAY` + write retries. |

---

### 7. Large‑file integer truncation

| Aspect     | Details                                                                                                                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Cause**  | `ftell()` returns `long` but you store it in `long` then cast to `size_t`; on 32‑bit or when `_FILE_OFFSET_BITS=32` this truncates >2 GiB.                               |
| **Impact** | Short reads (partial file leak) or, worse, negative size when cast to `size_t` → huge allocation attempt → crash.                                                        |
| **Fixes**  | ① Use `struct stat st; fstat(fileno(file), &st); off_t sz = st.st_size;` ② Add an upper bound (e.g. `MAX_STATIC_FILE = 50*1024*1024`) and refuse to serve bigger assets. |

---

### 8. Hard‑coded protocol limits

| Aspect     | Details                                                                                                                                                                                                          |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Cause**  | `HTTP_RECEIVE_BUFFER_LEN` is 4 KiB, `HTTP_MAX_HEADER_COUNT` is 20. Modern browsers can send 10 KiB cookie headers easily.                                                                                        |
| **Impact** | Oversized request truncates in the middle of a header → parse error → vague 400 or crash → trivial DoS.                                                                                                          |
| **Fixes**  | ① Grow the buffer dynamically (`realloc`) until a sane max (e.g. 64 KiB). ② If limit is hit, reply `431 Request Header Fields Too Large` not just close. ③ Stream‑parse with `llhttp_execute()` as bytes arrive. |

---

### 9. Missing transport‑layer security & privilege separation *(defence‑in‑depth)*

Although out‑of‑scope for localhost demos, real deployment must run behind TLS (nginx/Caddy) and as an **unprivileged UID** inside a chroot/container.  Combine with `seccomp()` or `landlock` to sandbox file system access.

---

### 10. Future counter‑measures checklist

* Enable **ASLR, RELRO, PIE** at compile time (`-fPIE -pie -Wl,-z,relro,-z,now`).
* Ship a **Content‑Security‑Policy** header in every HTML response to curb XSS.
* Add **rate‑limiting** (token bucket per‑IP) on the accept loop to blunt brute‑force traffic.
* Write unit tests that fuzz‑feed headers and URLs through `http_parse_request()` under AddressSanitizer and UndefinedBehaviourSanitizer.



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
