import { NavLink } from 'react-router-dom';
import { useContract } from '../contract/ContractContext';

function labelOf(path){
  const base = path.replace(/^\//,'');
  return base.charAt(0).toUpperCase() + base.slice(1);
}

export default function Nav(){
  const { loading, error, views } = useContract() || {};

  if (loading) return <nav className="site-nav"><span className="text-sm text-[var(--muted)]">Loading…</span></nav>;
  if (error)   return <nav className="site-nav"><span className="text-sm text-[var(--magenta)]">{error}</span></nav>;

  return (
    <nav className="site-nav">
      {views.map(v => (
        <NavLink key={v} to={v === '/home' ? '/home' : v} end className={({isActive})=>`nav-link ${isActive ? 'current-page' : ''}`}>
          {labelOf(v)}
        </NavLink>
      ))}
    </nav>
  );
}