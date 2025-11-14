# Frontend Dev Cheatsheet

This directory contains the Vite + React UI for HomeServer. The scripts are defined in `package.json`; the table below explains how to run them and which env vars matter.

## 0. Install once

```bash
cd frontend
npm ci           # installs the exact lockfile deps
```

## 1. Local dev server (with mock API)

Use the in-memory mock (`src/api/mockServer.js`) so you can iterate without the C backend:

```bash
VITE_USE_MOCK_API=true npm run dev
```

What this does:

- Boots Vite on http://localhost:5173 with hot reload
- Automatically handles `/whoami`, `/api/db_add_user`, `/api/db_list_users` using fake data
- Still uses the real React components, so the UI behaves exactly as in prod

Want to expose it on your LAN? Append `-- --host 0.0.0.0`.

## 2. Local dev server (real backend)

When the backend + nginx proxy are up, point Vite to them:

```bash
VITE_API_BASE=/api npm run dev
```

Notes:

- `VITE_API_BASE` gets prepended to every fetch path (see `src/api/client.js`)
- Do not set `VITE_USE_MOCK_API` here; otherwise requests never reach nginx

## 3. Production build

```bash
npm run build    # writes optimized assets into frontend/dist
```

`Personal/server/deploy/software/build_frontend.sh` calls the same command for deploys. If you want to smoke-test the built bundle locally:

```bash
npm run preview  # serves dist/ on http://localhost:4173 by default
```

Preview uses the same env vars as `npm run dev`, so you can combine it with `VITE_API_BASE` or `VITE_USE_MOCK_API`.

## 4. Useful environment variables

| Variable              | Default | Description |
|-----------------------|---------|-------------|
| `VITE_API_BASE`       | `''`    | Prefix added to every API path (set to `/api` when the backend sits behind nginx). |
| `VITE_USE_MOCK_API`   | `false` | When `true`, fetches are short-circuited to `src/api/mockServer.js`. Perfect for offline/frontend-only work. |

You can drop these in `.env.local`, `.env.devmock`, etc., per Vite’s standard convention, or prefix them inline as shown above.

## 5. Repo tips

- The build scripts in `Personal/server/deploy/software/*.sh` already call `npm ci` + `npm run build`, so keeping this README handy helps only when you are iterating manually.
- If you add new API calls, remember to update both the real client code and the mock handlers so `VITE_USE_MOCK_API=true` continues to represent reality.
