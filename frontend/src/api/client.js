const DEFAULT_HEADERS = { 'Content-Type': 'application/json' };

export async function apiGet(path, opts = {})
{
  const res = await fetch(path, { method: 'GET', ...opts });
  if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
  const ctype = res.headers.get('content-type') || '';
  return ctype.includes('application/json') ? res.json() : res.text();
}

export async function apiPut(path, body, opts = {})
{
  const res = await fetch(path, {
    method: 'PUT',
    headers: { ...DEFAULT_HEADERS, ...(opts.headers || {}) },
    body: JSON.stringify(body),
    ...opts
  });
  if (!res.ok) throw new Error(`${path} → HTTP ${res.status}`);
  return res.json().catch(() => ({}));
}

// whoami api registration
export async function whoAmI()
{
  const res = await fetch('/api/whoami', {
    method: 'GET',
    headers: { 'Accept': 'application/json' },
    credentials: 'same-origin'
  });
  if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
  return res.json();
}

// DataBase functions
export async function db_add_user(email)
{
  return apiPut('/api/users', { email });
}

export async function db_list_users()
{
  return apiGet('/api/users');
}