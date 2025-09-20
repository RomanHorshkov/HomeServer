import React, { useState } from "react";
import "../animations/cat.css";
import { whoAmI } from "../api/client";
import { db_add_user, db_list_users } from "../api/db";
import EmailField from "../components/EmailField";

function CopyButton({ value }) {
  const [ok, setOk] = useState(false);
  return (
    <button
      onClick={async () => {
        try { await navigator.clipboard.writeText(value); setOk(true); setTimeout(()=>setOk(false), 1200); } catch {}
      }}
      className="rounded-lg px-3 py-2 text-sm font-medium bg-[var(--accent)] text-black hover:bg-[var(--accent-hover)]"
    >
      {ok ? "Copied ✓" : "Copy"}
    </button>
  );
}

export default function Test() {
  const [out, setOut] = useState("Click Reveal to call /api/whoami");
  const [loading, setLoading] = useState(false);
  const [err, setErr] = useState("");

  const [emailRaw, setEmailRaw] = useState("");
  const [emailCanon, setEmailCanon] = useState("");
  const [emailOk, setEmailOk] = useState(false);

  const [usersJson, setUsersJson] = useState("");

  const reveal = async () => {
    setLoading(true); setErr("");
    try {
      const data = await whoAmI();
      const view = {
        server_time: data.server_time,
        method: data.method,
        path: data.path,
        contract_version: data.contract_version,
        ip_hint: data.headers?.["X-Real-IP"] || data.headers?.["x-real-ip"],
        user_agent: data.headers?.["User-Agent"] || data.headers?.["user-agent"],
        accept_language: data.headers?.["Accept-Language"] || data.headers?.["accept-language"]
      };
      setOut(JSON.stringify(view, null, 2));
    } catch (e) {
      setErr(e.message || String(e));
      setOut("");
    } finally {
      setLoading(false);
    }
  };

  const doAddUser = async () => {
    setErr("");
    try {
      const r = await db_add_user(emailCanon || emailRaw);
      setUsersJson(JSON.stringify(r, null, 2));
    } catch (e) {
      setErr(e.message || String(e));
    }
  };

  const doListUsers = async () => {
    setErr("");
    try {
      const r = await db_list_users();
      setUsersJson(JSON.stringify(r, null, 2));
    } catch (e) {
      setErr(e.message || String(e));
    }
  };

  return (
    <main className="max-w-5xl mx-auto px-4 sm:px-6 lg:px-8 py-10 sm:py-16">
      {/* whoami */}
      <section className="rounded-xl border border-[var(--border)] bg-[var(--surface)] p-4 sm:p-6 shadow-md mb-6">
        <div className="flex flex-wrap items-center gap-3 justify-between">
          <div className="flex items-center gap-3">
            <button
              onClick={reveal}
              disabled={loading}
              className="rounded-lg px-4 py-2.5 text-sm font-semibold bg-[var(--accent)] text-black hover:bg-[var(--accent-hover)] disabled:opacity-60"
            >
              {loading ? "Revealing…" : "Reveal /api/whoami"}
            </button>
            {out && <CopyButton value={out} />}
          </div>
          <div className="text-xs text-[var(--muted)]">
            Contract: <code>X-Contract-Version</code> also in body as <code>contract_version</code>
          </div>
        </div>
        {err && <p className="mt-3 text-sm text-red-500">Error: {err}</p>}
        <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json" aria-live="polite">
{out}
        </pre>
      </section>

      {/* users */}
      <section className="rounded-xl border border-[var(--border)] bg-[var(--surface)] p-4 sm:p-6 shadow-md">
        <div className="flex flex-wrap items-center gap-3">
          <EmailField
            value={emailRaw}
            onChange={({ raw, canon, ok }) => {
              setEmailRaw(raw);
              setEmailCanon(canon);
              setEmailOk(ok);
            }}
          />

          <button
            onClick={doAddUser}
            className="rounded-lg px-4 py-2.5 text-sm font-semibold bg-[var(--accent)] text-black hover:bg-[var(--accent-hover)] disabled:opacity-60"
            disabled={!emailOk}
          >
            add user
          </button>

          <button
            onClick={doListUsers}
            className="rounded-lg px-4 py-2.5 text-sm font-semibold border border-[var(--border)] hover:translate-y-[-1px] transition"
          >
            get users list
          </button>

          {usersJson && <CopyButton value={usersJson} />}
        </div>

        <pre className="mt-3 overflow-x-auto rounded-lg border border-[var(--border)] p-3 text-sm pretty-json">
{usersJson || "// Press one of the buttons above"}
        </pre>
      </section>
    </main>
  );
}
