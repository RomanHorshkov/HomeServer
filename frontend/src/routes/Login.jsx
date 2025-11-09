import { useState } from 'react';
import { login } from '../api/auth';
import { useAuth } from '../auth/AuthContext';

export default function Login() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [ok, setOk] = useState(false);
  const { auth, setAuth } = useAuth();

  const onSubmit = async (e) => {
    e.preventDefault();
    setErr('');
    setOk(false);
    setBusy(true);
    try {
      const data = await login(email, password);
      setAuth(data);     // store { uid, role, accTok, refTok }
      setOk(true);       // show green success line
    } catch (e) {
      setErr(e.message || 'Login failed');
    } finally {
      setBusy(false);
    }
  };

  return (
    <main className="mx-auto max-w-md p-6">
      <h1 className="text-2xl font-semibold mb-4">Login</h1>

      <form className="space-y-3" onSubmit={onSubmit}>
        <label className="block">
          <span className="text-sm">Email</span>
          <input
            type="email"
            value={email}
            onChange={(e)=>setEmail(e.target.value)}
            className="mt-1 w-full rounded-xl border px-3 py-2
                       border-neutral-300 dark:border-neutral-700
                       bg-white dark:bg-neutral-800"
            placeholder="you@example.com"
            required
          />
        </label>

        <label className="block">
          <span className="text-sm">Password</span>
          <input
            type="password"
            value={password}
            onChange={(e)=>setPassword(e.target.value)}
            className="mt-1 w-full rounded-xl border px-3 py-2
                       border-neutral-300 dark:border-neutral-700
                       bg-white dark:bg-neutral-800"
            placeholder="••••••••"
            required
          />
        </label>

        {err && <p className="text-red-600 text-sm">{err}</p>}
        {ok  && <p className="text-green-600 text-sm">
          Signed in. Role: {auth?.role} — UID: {auth?.uid}
        </p>}

        <button
          type="submit"
          disabled={busy}
          className="w-full rounded-xl px-4 py-2 font-medium
                     bg-neutral-900 text-white hover:opacity-90
                     disabled:opacity-50
                     dark:bg-neutral-100 dark:text-neutral-900"
        >
          {busy ? 'Signing in…' : 'Sign in'}
        </button>
      </form>
    </main>
  );
}
