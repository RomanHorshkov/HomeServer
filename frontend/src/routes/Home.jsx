import React, { useState } from "react";
import "../animations/cat.css";

/* --- tiny helpers kept in-file for simplicity --- */
function Section({ id, title, kicker, children }) {
  return (
    <section id={id} className="py-10 sm:py-16">
      {kicker && <p className="mb-2 text-sm font-medium tracking-wide text-[var(--muted)]">{kicker}</p>}
      {title && <h2 className="mb-6 text-2xl sm:text-3xl font-extrabold tracking-tight text-[var(--text)]">{title}</h2>}
      {children}
    </section>
  );
}

function Card({ title, icon, children }) {
  return (
    <div className="rounded-xl border border-[var(--border)] bg-[var(--surface)] p-4 sm:p-6 shadow-md">
      <div className="mb-2 flex items-center gap-2">
        <span className="text-xl">{icon}</span>
        <h3 className="text-lg font-semibold text-[var(--text)]">{title}</h3>
      </div>
      <div className="text-[.95rem] leading-relaxed text-[var(--muted)]">{children}</div>
    </div>
  );
}

function CopyButton({ value, small }) {
  const [ok, setOk] = useState(false);
  return (
    <button
      onClick={async () => {
        try {
          await navigator.clipboard.writeText(value);
          setOk(true);
          setTimeout(() => setOk(false), 1200);
        } catch {}
      }}
      className={`rounded-lg px-3 py-2 font-medium ${small ? "text-xs" : "text-sm"} bg-[var(--accent)] text-black hover:bg-[var(--accent-hover)]`}
      aria-label="Copy to clipboard"
    >
      {ok ? "Copied ✓" : "Copy"}
    </button>
  );
}

/* --- the page --- */
export default function Home() {
  const nginxSnippet = `limit_req_zone $binary_remote_addr zone=api_zone:10m rate=10r/s;
upstream backend_c_server {
  server 127.0.0.1:3490;
  keepalive 64;
}
server {
  listen 443 ssl http2;
  ssl_protocols TLSv1.2 TLSv1.3;
  add_header X-Frame-Options "SAMEORIGIN";
  add_header X-Content-Type-Options "nosniff";
  add_header X-XSS-Protection "1; mode=block";
  add_header Referrer-Policy "strict-origin-when-cross-origin";
  add_header Content-Security-Policy "default-src 'self'; script-src 'self'; object-src 'none';";

  location /api/ {
    limit_req zone=api_zone burst=20 nodelay;
    proxy_pass http://backend_c_server;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
  }
}`;

  return (
    <main className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8">
      {/* HERO */}
      <header className="py-10 sm:py-16">
        <div className="inline-flex items-center gap-2 rounded-full border border-[var(--border)] bg-[var(--surface)] px-3 py-1 text-xs sm:text-sm mb-4 shadow">
          <span>🧵 epoll + threads</span>
          <span className="opacity-50">•</span>
          <span>HTTP/1.1 keep-alive</span>
          <span className="opacity-50">•</span>
          <span>SPA frontend</span>
        </div>

        <h1 className="text-balance text-3xl sm:text-5xl font-extrabold tracking-tight text-[var(--text)]">
          C11 / POSIX micro-server<br className="hidden sm:block" /> with a clean, fast frontend
        </h1>

        <p className="mt-4 max-w-2xl text-[1.05rem] leading-relaxed text-[var(--muted)]">
          Lean, multithreaded, edge-triggered IO. This UI showcases features, docs, and quick actions—mobile-first, no backend changes needed.
        </p>

        <div className="mt-6 flex flex-wrap gap-3">
          <a
            className="rounded-lg bg-[var(--accent)] text-black px-4 py-2.5 text-sm font-semibold hover:bg-[var(--accent-hover)] shadow"
            href="/build_notes"
          >
            📓 Build Notes
          </a>
          <a
            className="rounded-lg border border-[var(--border)] bg-[var(--surface)] px-4 py-2.5 text-sm font-semibold hover:translate-y-[-1px] transition"
            href="https://github.com/RomanHorshkov" target="_blank" rel="noreferrer"
          >
            ⭐ GitHub
          </a>
          <a
            className="rounded-lg border border-[var(--border)] bg-[var(--surface)] px-4 py-2.5 text-sm font-semibold hover:translate-y-[-1px] transition"
            href="/"
          >
            🏠 Home
          </a>
        </div>
      </header>

      {/* WHAT IT DOES */}
      <Section id="capabilities" kicker="Capabilities" title="High-level snapshot">
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 sm:gap-6">
          <Card title="True multithreading" icon="🧩">
            Listener and worker threads connected via lock-free SPSC ring and eventfd; graceful back-pressure when full.
          </Card>
          <Card title="Edge-triggered epoll" icon="⚡">
            Minimal syscalls per wake-up; scales to thousands of sockets without busy polling.
          </Card>
          <Card title="HTTP/1.1 parser" icon="📜">
            llhttp state machine, spec-grade callbacks, persistent keep-alive with correct connection policy.
          </Card>
          <Card title="Static + JSON" icon="🗂️">
            Serves assets and JSON endpoints with accurate MIME and content lengths.
          </Card>
          <Card title="SPA shell" icon="🧭">
            Client-side routing, instant nav, zero full reloads; responsive by default.
          </Card>
          <Card title="Observability" icon="📝">
            Line-buffered log ready for <code>tail -f</code> or shipping to your stack.
          </Card>
        </div>
      </Section>

      {/* QUICK START / COMMANDS */}
      <Section id="quickstart" kicker="Get started" title="Build & run in seconds">
        <div className="rounded-xl border border-[var(--border)] bg-[var(--surface)] p-4 sm:p-6 shadow-md">
          <div className="flex items-center justify-between gap-2">
            <p className="text-sm font-semibold">Debug build</p>
            <CopyButton value={`make debug && build/bin/server`} />
          </div>
          <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json">
{`make debug
build/bin/server`}
          </pre>

          <div className="mt-6 flex items-center justify-between gap-2">
            <p className="text-sm font-semibold">Release build</p>
            <CopyButton value={`make release && build/bin/server`} />
          </div>
          <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json">
{`make release
build/bin/server`}
          </pre>
        </div>
      </Section>

      {/* TRY IT */}
      <Section id="try" kicker="Try it" title="Poke it from your terminal">
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4 sm:gap-6">
          <Card title="Fetch a page" icon="🌐">
            <div className="flex items-center justify-between gap-2">
              <span className="text-sm font-semibold">curl example</span>
              <CopyButton value={`curl -i http://localhost:3490/`} small />
            </div>
            <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json">
{`curl -i http://localhost:3490/`}
            </pre>
          </Card>

          <Card title="Keep-alive request" icon="🔄">
            <div className="flex items-center justify-between gap-2">
              <span className="text-sm font-semibold">HTTP/1.1 keep-alive</span>
              <CopyButton value={`printf 'GET / HTTP/1.1\\r\\nHost: localhost\\r\\n\\r\\n' | nc localhost 3490`} small />
            </div>
            <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json">
{`printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 3490`}
            </pre>
          </Card>
        </div>

        <div className="mt-4 grid grid-cols-1 lg:grid-cols-3 gap-4 sm:gap-6">
          <Card title="Build notes (UI)" icon="📓">
            <p className="mb-3">Static viewer bundled with the server.</p>
            <a className="inline-block rounded-lg border border-[var(--border)] bg-[var(--surface)] px-3 py-2 text-sm font-semibold hover:translate-y-[-1px] transition"
               href="/build_notes">
              Open viewer →
            </a>
          </Card>
          <Card title="Routes map" icon="🧭">
            <p className="mb-3">Discover navigable paths exposed by the UI.</p>
            <a className="inline-block rounded-lg border border-[var(--border)] bg-[var(--surface)] px-3 py-2 text-sm font-semibold hover:translate-y-[-1px] transition"
               href="/routes.json">
              View JSON →
            </a>
          </Card>
          <Card title="Server log" icon="🪵">
            <p className="mb-3">Live traffic while you browse locally.</p>
            <a className="inline-block rounded-lg border border-[var(--border)] bg-[var(--surface)] px-3 py-2 text-sm font-semibold hover:translate-y-[-1px] transition"
               href="/server.log">
              Open log →
            </a>
          </Card>
        </div>
      </Section>

      {/* ARCHITECTURE HIGHLIGHTS */}
      <Section id="architecture" kicker="Under the hood" title="Architecture highlights">
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 sm:gap-6">
          <Card title="Listener → Worker hand-off" icon="🔁">
            Zero-copy FD transfer via SPSC ring; eventfd wakeups; pipe for load state (ACTIVE/FULL).
          </Card>
          <Card title="Idle reaper" icon="⏱️">
            Single timerfd ticks; evicts idle sockets; keeps latency stable under load.
          </Card>
          <Card title="Sanity limits" icon="🧰">
            Header/body caps, accurate content types, defensive path handling.
          </Card>
          <Card title="Production-adjacent" icon="🐳">
            Make targets, static libs, Docker & optional nginx front.
          </Card>
        </div>
      </Section>

      {/* CTA */}
      <Section id="cta" kicker="Next" title="Where to go from here">
        {/* ← new cat animation above the buttons ↓ */}
        <div className="flex justify-center mb-6">
          <div className="cat">
            <div className="ear ear--left" />
            <div className="ear ear--right" />
            <div className="face">
              <div className="eye eye--left">
                <div className="eye-pupil" />
              </div>
              <div className="eye eye--right">
                <div className="eye-pupil" />
              </div>
              <div className="muzzle" />
            </div>
          </div>
        </div>

        <div className="flex flex-wrap gap-3">
          <a
            className="rounded-lg bg-[var(--accent)] text-black px-4 py-2.5 text-sm font-semibold hover:bg-[var(--accent-hover)] shadow"
            href="/build_notes"
          >
            Explore build notes
          </a>
          <a
            className="rounded-lg border border-[var(--border)] bg-[var(--surface)] px-4 py-2.5 text-sm font-semibold"
            href="https://github.com/RomanHorshkov"
            target="_blank"
            rel="noreferrer"
          >
            Star the repo
          </a>
        </div>
      </Section>

      {/* ... footer ... */}
    </main>
  );
}
