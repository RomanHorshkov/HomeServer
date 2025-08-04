import { NavLink } from 'react-router-dom';

// Static navigation items; add paths and labels here as your app grows
const navItems = [
  { path: '/home', label: 'Home' }
];

export default function Nav() {
  return (
    <nav className="site-nav">
      {navItems.map(({ path, label }) => (
        <NavLink
          key={path}
          to={path}
          end
          className={({ isActive }) => `nav-link ${isActive ? 'current-page' : ''}`}
        >
          {label}
        </NavLink>
      ))}
    </nav>
  );
}
