// src/api/client.js
const DEFAULT_HEADERS = { 'Content-Type': 'application/json' };
const BASE = import.meta.env.VITE_API_BASE || ''; // e.g. "/api" via Vite proxy

function url(path) {
  return `${BASE}${path}`;
}

async function handle(res, path) {
  if (!res.ok) {
    let msg = `${path} → HTTP ${res.status}`;
    try {
      const data = await res.json();
      if (data?.error) msg = `${path} → ${data.error}`;
    } catch {}
    throw new Error(msg);
  }
  const ctype = res.headers.get('content-type') || '';
  return ctype.includes('application/json') ? res.json() : res.text();
}

export async function apiGet(path, opts = {}) {
  const res = await fetch(url(path), {
    method: 'GET',
    credentials: 'include',            // ← keep cookies (sessions)
    ...opts
  });
  return handle(res, path);
}

export async function apiPost(path, body, opts = {}) {
  const res = await fetch(url(path), {
    method: 'POST',
    headers: { ...DEFAULT_HEADERS, ...(opts.headers || {}) },
    credentials: 'include',            // ← important for auth flows
    body: JSON.stringify(body),
    ...opts
  });
  return handle(res, path);
}

export async function apiPut(path, body, opts = {}) {
  const res = await fetch(url(path), {
    method: 'PUT',
    headers: { ...DEFAULT_HEADERS, ...(opts.headers || {}) },
    credentials: 'include',
    body: JSON.stringify(body),
    ...opts
  });
  return handle(res, path);
}

// whoami api registration
export async function whoAmI() {
  // Delegate to apiGet; keep Accept header
  return apiGet('/whoami', { headers: { Accept: 'application/json' } });
}
