// frontend/src/api/mockServer.js
// Tiny in-memory mock used during frontend development.  Enable it by running
// VITE_USE_MOCK_API=true npm run dev

const state = {
  users: [
    { email: 'viewer@example.com', created_at: '2024-11-14T18:00:00Z' },
    { email: 'publisher@example.com', created_at: '2024-11-14T18:05:00Z' },
  ],
};

const wait = (ms = 250) => new Promise((resolve) => setTimeout(resolve, ms));

const clone = (obj) => JSON.parse(JSON.stringify(obj));

export async function mockRequest(method, path, body) {
  await wait();

  if (method === 'GET' && path === '/whoami') {
    return {
      server_time: new Date().toISOString(),
      method: 'GET',
      path: '/api/whoami',
      contract_version: 'mock-0',
      headers: {
        'User-Agent': 'HomeServer Frontend Mock',
        'Accept-Language': 'en-US',
        'X-Real-IP': '127.0.0.1',
      },
    };
  }

  if (method === 'PUT' && path === '/api/db_add_user') {
    if (!body?.email) {
      throw new Error('[mock-api] Missing email');
    }
    const exists = state.users.find((u) => u.email === body.email);
    if (exists) {
      return { ok: true, already_present: true, users: clone(state.users) };
    }
    state.users.push({ email: body.email, created_at: new Date().toISOString() });
    return { ok: true, users: clone(state.users) };
  }

  if (method === 'GET' && path === '/api/db_list_users') {
    return { ok: true, users: clone(state.users) };
  }

  throw new Error(`[mock-api] No handler for ${method} ${path}`);
}
