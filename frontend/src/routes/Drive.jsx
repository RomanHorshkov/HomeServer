import { useEffect, useState } from 'react';
import { apiGet } from '../api/client';

export default function Drive(){
  const [data, setData] = useState(null);
  const [error, setError] = useState('');

  useEffect(()=>{
    let dead = false;
    apiGet('/api/drive')
      .then(json => { if (!dead) setData(json); })
      .catch(e => { if (!dead) setError(String(e.message || e)); });
    return ()=>{ dead = true; };
  },[]);

  return (
    <section>
      <h1>Drive</h1>
      {error && <p style={{color:'var(--magenta)'}}>{error}</p>}
      {data ? <pre className="pretty-json">{JSON.stringify(data,null,2)}</pre> : <p>Loading…</p>}
    </section>
  );
}