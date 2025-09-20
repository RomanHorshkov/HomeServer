// utils/email.js
const ZERO_WIDTH = /[\u200B-\u200D\u2060\uFEFF]/g; // common invisible chars

export function sanitizeEmailInput(s) {
  if (!s) return "";
  let x = String(s).normalize("NFKC");     // fold weird unicode (full-width, etc.)
  x = x.replace(ZERO_WIDTH, "");           // strip zero-width
  x = x.replace(/\s+/g, "");               // remove all whitespace
  x = x.replace(/@\./g, "@");              // fix accidental "@."
  x = x.replace(/\.\.+/g, ".");            // collapse ".."
  x = x.replace(/\.$/, "");                // drop trailing dot on domain
  return x;
}

// keep local-part case as-is; lowercase domain (safe + standard)
export function canonicalizeEmail(s) {
  const clean = sanitizeEmailInput(s);
  const parts = clean.split("@");
  if (parts.length !== 2) return { ok: false, reason: "missing or multiple @" };

  const [local, domainRaw] = parts;
  if (!local || !domainRaw) return { ok: false, reason: "empty local/domain" };

  const domain = domainRaw.toLowerCase();
  const candidate = `${local}@${domain}`;

  // pragmatic format checks (fast + good enough for UI)
  if (candidate.length > 254) return { ok: false, reason: "too long" };
  const [l, d] = candidate.split("@");
  if (l.length > 64) return { ok: false, reason: "local too long" };
  if (d.startsWith("-") || d.endsWith("-") || d.includes(".."))
    return { ok: false, reason: "bad domain" };

  // basic TLD-ish check; avoids RFC monsters but filters junk
  const BASIC = /^[^\s@]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$/;
  if (!BASIC.test(candidate)) return { ok: false, reason: "format" };

  return { ok: true, value: candidate };
}
