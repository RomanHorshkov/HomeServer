import React, { Suspense, lazy } from 'react';
import ReactDOM from 'react-dom/client';
import {
  createBrowserRouter,
  RouterProvider,
  Navigate
} from 'react-router-dom';
import AppShell from './AppShell.jsx';
import './styles/main.css';
import { initTheme } from './theme';

initTheme();

// Code-split home page
const Home = lazy(() => import('./routes/Home.jsx'));

const routes = [
  { path: '/',     element: <Navigate to="/home" replace /> },
  {
    path: '/home',
    element: (
      <Suspense fallback={<p style={{padding:'2rem'}}>Loading…</p>}>
        <Home />
      </Suspense>
    )
  },
  { path: '*', element: <Navigate to="/home" replace /> }
];

const router = createBrowserRouter([
  { element: <AppShell />, children: routes }
]);

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <RouterProvider router={router} />
  </React.StrictMode>
);
