import { createContext, useContext, useEffect, useMemo, useState } from 'react';
import { apiGet } from '../api/client';

const ContractCtx = createContext({ loading: true, error: null, apis: [] });
export const useContract = () => useContext(ContractCtx);

async function fetchApis() {
  // First try your real contract, fall back to the example file
  try {
    const { apis } = await apiGet('/routes.json');
    return Array.isArray(apis) ? apis : [];
  } catch {
    const { apis } = await apiGet('/routes.json.example');
    return Array.isArray(apis) ? apis : [];
  }
}

export function ContractProvider({ children }) {
  const [apis, setApis] = useState(null);
  const [error, setError] = useState(null);

  useEffect(() => {
    let alive = true;
    fetchApis()
      .then(list => alive && setApis(list))
      .catch(err => alive && setError(err.message));
    return () => { alive = false };
  }, []);

  const value = useMemo(() => ({
    loading: apis === null && !error,
    error,
    apis: apis || []
  }), [apis, error]);

  return (
    <ContractCtx.Provider value={value}>
      {children}
    </ContractCtx.Provider>
  );
}
