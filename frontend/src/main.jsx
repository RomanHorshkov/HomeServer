import React, { Suspense, lazy } from 'react';
import ReactDOM from 'react-dom/client';
import { createBrowserRouter, RouterProvider, Navigate } from 'react-router-dom';
import AppShell from './AppShell.jsx';
import './index.css';
import { initTheme } from './theme';
import { ContractProvider, useContract } from './contract/ContractContext.jsx';

// Lazy route components (code-splitting like old dynamic import)
const Home = lazy(() => import('./routes/Home.jsx'));
const Expenses = lazy(() => import('./routes/Expenses.jsx'));
const Drive = lazy(() => import('./routes/Drive.jsx'));

initTheme();

function Routed(){
  // Use the contract to decide which routes to expose
  const { loading, error, views } = useContract() || {};
  if (loading) return <p style={{padding:'2rem'}}>Loading contract…</p>;
  if (error)   return <p style={{padding:'2rem', color:'var(--magenta)'}}>Contract error: {error}</p>;

  const allowed = new Set(views);

  const routes = [
    { path: '/',     element: <Navigate to="/home" replace /> },
    ...(allowed.has('/home')     ? [{ path:'/home',     element:<Suspense fallback={<p>…</p>}><Home /></Suspense> }] : []),
    ...(allowed.has('/expenses') ? [{ path:'/expenses', element:<Suspense fallback={<p>…</p>}><Expenses /></Suspense> }] : []),
    ...(allowed.has('/drive')    ? [{ path:'/drive',    element:<Suspense fallback={<p>…</p>}><Drive /></Suspense> }] : []),
    { path: '*', element: <Navigate to={allowed.has('/home')?'/home':'/'} replace /> }
  ];

  const router = createBrowserRouter([{ element:<AppShell />, children: routes }]);
  return <RouterProvider router={router} />;
}

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <ContractProvider>
      <Routed />
    </ContractProvider>
  </React.StrictMode>
);
