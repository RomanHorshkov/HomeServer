
import { createContext, useContext, useEffect, useMemo, useState } from 'react';
import { apiGet } from '../api/client';

const ContractCtx = createContext(null);

export function useContract() {
  return useContext(ContractCtx);
}

const SUPPORTED_VIEWS = new Set(['/home', '/expenses', '/drive']);

async function loadContract() {
  // Try backend contract first → /routes.json; fallback to example for local dev
  try {
    return await apiGet('/routes.json');
  } catch (_) {
    return await apiGet('/routes.json.example');
  }
}

export function ContractProvider({ children }) {
  const [contract, setContract] = useState(null);
  const [error, setError] = useState('');

  useEffect(() => {
    let dead = false;
    loadContract()
      .then(json => {
        if (dead) return;
        // Basic shape validation
        const apis = Array.isArray(json?.apis) ? json.apis : [];
        const views = Array.isArray(json?.views) ? json.views : [];
        if (!views.length) throw new Error('Contract missing views');
        setContract({ apis, views });
      })
      .catch(e => !dead && setError(String(e?.message || e)));
    return () => { dead = true; };
  }, []);

  const value = useMemo(() => {
    if (!contract) return { loading: true };

    // expose only views we actually implement (order preserved)
    const allowedViews = contract.views.filter(v => SUPPORTED_VIEWS.has(v));

    return {
      loading: false,
      error,
      apis: contract.apis,
      views: allowedViews
    };
  }, [contract, error]);

  return (
    <ContractCtx.Provider value={value}>
      {children}
    </ContractCtx.Provider>
  );
}