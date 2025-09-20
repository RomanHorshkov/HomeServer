// components/EmailField.jsx
import React, { useState, useEffect, useMemo } from "react";
import { canonicalizeEmail, sanitizeEmailInput } from "../utils/email";

export default function EmailField({ value, onChange, className = "" }) {
  const [raw, setRaw] = useState(value || "");
  const [err, setErr] = useState("");
  const [canon, setCanon] = useState("");

  useEffect(() => {
    setRaw(value || "");
  }, [value]);

  useEffect(() => {
    const c = canonicalizeEmail(raw);
    setErr(c.ok || raw.trim() === "" ? "" : "Not a valid address");
    setCanon(c.ok ? c.value : "");
    // tell parent both raw and canonical (parent can choose)
    onChange?.({ raw, canon: c.ok ? c.value : "", ok: c.ok });
  }, [raw, onChange]);

  const emailOk = useMemo(() => !!canon, [canon]);

  return (
    <div className={`flex flex-col ${className}`}>
      <input
        type="email"
        placeholder="email@example.com"
        value={raw}
        onChange={(e) => setRaw(e.target.value)}
        onBlur={() => {
          const snapped = sanitizeEmailInput(raw);
          if (snapped !== raw) setRaw(snapped);
        }}
        className={`rounded-lg border bg-[var(--surface)] px-3 py-2 text-sm w-64 ${
          err ? "border-red-500" : "border-[var(--border)]"
        }`}
        aria-invalid={!!err}
        aria-describedby="email-help email-canon"
        inputMode="email"
        autoCapitalize="none"
        autoCorrect="off"
        spellCheck={false}
      />
      <div className="min-h-[1.25rem] mt-1">
        {err ? (
          <span id="email-help" className="text-xs text-red-500">{err}</span>
        ) : emailOk ? (
          <span id="email-canon" className="text-xs text-[var(--muted)]">
            will send as <code>{canon}</code>
          </span>
        ) : null}
      </div>
    </div>
  );
}
