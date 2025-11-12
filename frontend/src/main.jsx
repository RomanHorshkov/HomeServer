import React, { Suspense, lazy } from 'react';
import ReactDOM from 'react-dom/client';
import { createBrowserRouter, RouterProvider, Navigate } from 'react-router-dom';
import AppShell from './AppShell.jsx';
import './styles/main.css';
import { initTheme } from './theme';
import { AuthProvider } from './auth/AuthContext';

initTheme();

// Code-split pages
const Home  = lazy(() => import('./routes/Home.jsx'));
const Test  = lazy(() => import('./routes/Test.jsx'));
const Login = lazy(() => import('./routes/Login.jsx'));

const withFallback = (el) => (
  <Suspense fallback={<p style={{ padding: '2rem' }}>Loading…</p>}>
    {el}
  </Suspense>
);

const routes = [
  { path: '/', element: <Navigate to="/home" replace /> },
  { path: '/home',  element: withFallback(<Home />) },
  { path: '/test',  element: withFallback(<Test />) },
  { path: '/login', element: withFallback(<Login />) },
  { path: '*', element: <Navigate to="/home" replace /> }
];

const router = createBrowserRouter([{ element: <AppShell />, children: routes }]);

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <RouterProvider router={router} />
  </React.StrictMode>
);

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <AuthProvider>                              {/* ← wrap router */}
      <RouterProvider router={router} />
    </AuthProvider>
  </React.StrictMode>
);

