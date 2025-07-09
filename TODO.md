**TODO.md**

## Immediate Network Hardening

*Bite‑size tasks that close the easiest denial‑of‑service holes while we work on the bigger redesign.*

### 1  Socket flags & Nagle

* **Where:** `listener.c` right after each `accept()`.
* **Exact code:**

  ```c
  int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK);
  if (fd < 0) { /* handle */ }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  ```
* **Re‑test:** open 10 k keep‑alive fetches via `wrk2`; latency p99 ≤ 5 ms.

### 2  Content‑Length guard (8 MiB default)

| Step | File                                                                                   | Code pointer                 |
| ---- | -------------------------------------------------------------------------------------- | ---------------------------- |
| 1    | `http_manager.c`                                                                       | after `http_parse_request()` |
| 2    | Read header: `const char *cl = http_get_header(req, "Content-Length");`                |                              |
| 3    | `if (cl && strtoull(cl,NULL,10) > settings.max_body_bytes)` → `error_413(fd); return;` |                              |

`error_413()` sends:

```http
HTTP/1.1 413 Payload Too Large
Connection: close
Content-Length: 0


```

### 3  Body streaming (spill > 64 KiB)

1. **Struct upgrades**

   ```c
   typedef struct {
       size_t body_bytes;
       int    tmp_fd;      /* -1 until spill */
       char   *mem_buf;    /* <= 64 KiB */
   } body_store_t;
   ```
2. **When `body_bytes + chunk > BODY_RAM_THRESHOLD`**

   * `if (tmp_fd == -1) tmp_fd = mkstemp("/tmp/upXXXXXX");`
   * `write(tmp_fd, chunk, len);`
   * Free `mem_buf` if it exists.
3. **Router contract** — handlers now receive either

   * `req->body_fd  != -1` **or** `req->body_ptr`.

### 4  Keep‑alive lifecycle

| Parameter           | Value | Location     |
| ------------------- | ----- | ------------ |
| `KEEPALIVE_TIMEOUT` | 15 s  | `settings.h` |
| `MAX_REQ_PER_CONN`  | 100   | `settings.h` |

**Connection state struct**

```c
typedef struct {
    int    fd;
    time_t last_activity;
    int    req_count;
} conn_t;
```

**Workflow**

1. Accept → push `conn` into an array / hashmap keyed by `fd`.
2. After each response:

   ```c
   conn->last_activity = now; conn->req_count++; 
   ev.events = EPOLLIN | EPOLLONESHOT | EPOLLET; 
   epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
   ```
3. **Idle‑sweep** (timerfd tick every 2 s):

   * Iterate `conn_set`; close if `now‑last_activity > KEEPALIVE_TIMEOUT` *or* `req_count >= MAX_REQ_PER_CONN`.

**Acceptance test**

* Launch `wrk2 -c4000 -d30s --rate 0 http://127.0.0.1/`.
* Verify all idle fds evaporate ≤ 20 s after run ends (`ss -n state established | wc -l`).

---

## Project Structure & Organization

**Goal:** Ensure maintainability, clarity, and scalability.

* [ ] **Modular Codebase**: Organize code into logical modules/packages (e.g., `/app`, `/routes`, `/models`).

  * **Why:** Modularization enables easier debugging, testing, and future expansion.
* [ ] **Dedicated Directories**:

  * `/static` for static assets (CSS, JS, images)
  * `/templates` for HTML or template files
  * `/logs` for log files
  * `/tests` for test code
  * **Why:** Separation of concerns prevents accidental overwrites and improves security.
* [ ] **Configuration Files**: Store settings in files like `config.yaml`, `.env`, or `settings.py`.

  * **Why:** Decouples code from environment-specific settings.

## Configuration Management

**Goal:** Achieve flexibility and security.

* [ ] **Environment Variables**: Use environment variables for secrets, database URIs, and API keys.

  * **Why:** Prevents sensitive data from being exposed in code repositories.
* [ ] **Config Validation**: Validate configuration at startup; fail fast if misconfigured.

  * **Why:** Prevents undefined behavior due to missing or invalid settings.

## Security

**Goal:** Protect data, users, and infrastructure.

* [ ] **Input Validation & Sanitization**: Validate all user input (forms, URLs, APIs).

  * **Why:** Prevents SQL injection, XSS, and other vulnerabilities.
* [ ] **HTTPS**: Use TLS/SSL certificates; redirect all HTTP traffic to HTTPS.

  * **Why:** Encrypts data in transit, protecting against eavesdropping.
* [ ] **File Permissions**: Restrict read/write/execute permissions for files and directories.

  * **Why:** Limits damage if the server is compromised.
* [ ] **Dependency Auditing**: Regularly scan dependencies for known vulnerabilities.

  * **Why:** Prevents exploitation of third-party code.
* [ ] **Secret Management**: Never commit secrets to version control; use secret managers or environment variables.

  * **Why:** Reduces risk of credential leaks.
* [ ] **Upload Limits** (*NEW*) — Reject `Content‑Length` > `MAX_BODY_BYTES`; stream large bodies.
* [ ] **Session & Cookie Security** (*NEW*) — Implement `sid` opaque cookie with `Secure; HttpOnly; SameSite=Lax` plus CSRF token.

## Logging & Monitoring

**Goal:** Enable observability and troubleshooting.

* [ ] **Structured Logging**: Use a logging framework (e.g., `logfmt` style for the C logger).

  * **Why:** Structured logs are easier to parse and analyze.
* [ ] **Log Rotation**: Implement log rotation to prevent disk exhaustion.

  * **Why:** Ensures logs don’t fill up storage.
* [ ] **Error Reporting**: Capture and report errors (e.g., Sentry, Rollbar).

  * **Why:** Enables proactive issue resolution.
* [ ] **Health Checks**: Implement endpoints or scripts to check server health.

  * **Why:** Facilitates uptime monitoring and automated recovery.
* [ ] **Per‑connection Metrics** (*NEW*) — Track `active_fds`, `keepalive_timed_out`, `bytes_uploaded`.

## Testing

**Goal:** Ensure reliability and correctness.

* [ ] **Unit Tests**: Cover individual functions and components.

  * **Why:** Detects regressions early.
* [ ] **Integration Tests**: Test interactions between components (e.g., API endpoints, database).

  * **Why:** Ensures components work together as expected.
* [ ] **End-to-End Tests**: Simulate real user scenarios.

  * **Why:** Validates the entire system.
* [ ] **Continuous Integration (CI)**: Automate tests on every commit (e.g., GitHub Actions, GitLab CI).

  * **Why:** Prevents broken code from being deployed.
* [ ] **Fuzzing Suite** (*NEW*) — Harness `http_manage_request()` with `libFuzzer` corpus.

## Deployment Preparation

**Goal:** Achieve reproducible, reliable deployments.

* [ ] **Process Manager**: Use a process manager (e.g., systemd, Supervisor).

  * **Why:** Ensures the server restarts on failure and manages multiple instances.
* [ ] **Reverse Proxy**: Deploy behind Nginx or Apache for SSL termination, load balancing, and static file serving.

  * **Why:** Improves performance and security.
* [ ] **Containerization**: Use Docker for consistent environments.

  * **Why:** Simplifies deployment and scaling.
* [ ] **Database Migrations**: Automate schema migrations (e.g., Flyway).

  * **Why:** Keeps database schema in sync with code.
* [ ] **Backup Strategy**: Regularly back up databases and critical files.

  * **Why:** Enables recovery from data loss.

## Performance & Scalability

**Goal:** Ensure responsiveness under load.

* [ ] **Profiling**: Identify and optimize bottlenecks.
* [ ] **Caching**: Use caching for expensive operations (e.g., Redis).
* [ ] **Load Testing**: Simulate high traffic (e.g., with `wrk2`).
* [ ] **Connection Timeouts** (*NEW*) — Enforce keep‑alive limits and idle‑fd eviction.

## Final Pre-Deployment Steps

* [ ] All tests pass in CI.
* [ ] No critical or high vulnerabilities in dependencies.
* [ ] Secrets are not present in the codebase.
* [ ] Logs are rotating and not world-readable.
* [ ] Health checks pass.
* [ ] Backups are recent and restorable.
* [ ] Documentation is up to date.
* [ ] Rollback plan is in place.
