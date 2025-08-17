import { NavLink } from 'react-router-dom';

const items = [
  { path: '/home', label: 'Home', end: true },
  { path: '/test', label: 'Test', end: true },
];

const base =
  "inline-flex items-center rounded-md px-3 py-2 text-sm font-medium transition";
const active =
  "bg-[var(--surface)] text-[var(--text)] shadow-sm ring-1 ring-[var(--border)]";
const inactive =
  "text-[var(--muted)] hover:text-[var(--text)] hover:bg-[var(--surface)] focus:outline-none focus:ring-2 focus:ring-[var(--accent)]";

export default function Nav() {
  return (
    <nav className="site-nav flex gap-1">
      {items.map(({ path, label, end }) => (
        <NavLink
          key={path}
          to={path}
          end={end}
          className={({ isActive }) => `${base} ${isActive ? active : inactive}`}
        >
          {label}
        </NavLink>
      ))}
    </nav>
  );
}
