import React, { Suspense, lazy } from 'react';
import ReactDOM from 'react-dom/client';
import { createBrowserRouter, RouterProvider, Navigate } from 'react-router-dom';
import AppShell from './AppShell.jsx';
import './index.css';
import { initTheme } from './theme';
import { ContractProvider, useContract } from './contract/ContractContext.jsx';

// Lazy route components (code-splitting like old dynamic import)
const Home = lazy(() => import('./routes/Home.jsx'));
// const Expenses = lazy(() => import('./routes/Expenses.jsx'));
// const Drive = lazy(() => import('./routes/Drive.jsx'));

initTheme();

function App() {
  // Define routes statically:
  const routes = [
    { path: '/', element: <Navigate to="/home" replace /> },
    { path: '/home', element: <Suspense fallback={<p>Loading…</p>}><Home/></Suspense> },
    { path: '*', element: <Navigate to="/home" replace /> }
  ];

  const router = createBrowserRouter([
    { element: <AppShell/>, children: routes }
  ]);

  return <RouterProvider router={router}/>;
}

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <App/>
  </React.StrictMode>
);
