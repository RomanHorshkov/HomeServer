import { createContext, useContext, useState } from 'react';

// shape: { uid, role, accTok, refTok } or null
const AuthCtx = createContext(null);

export function AuthProvider({ children }) {
  const [auth, setAuth] = useState(null);
  return (
    <AuthCtx.Provider value={{ auth, setAuth }}>
      {children}
    </AuthCtx.Provider>
  );
}

export function useAuth() {
  const ctx = useContext(AuthCtx);
  if (!ctx) throw new Error('useAuth must be used inside <AuthProvider>');
  return ctx;
}
