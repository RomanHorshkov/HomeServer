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
