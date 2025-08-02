import { Link } from 'react-router-dom';
import Nav from './Nav';
import ThemeToggle from './ThemeToggle';

export default function Header() {
  return (
    <header className="site-header">
      <Link to="/" className="logo">C-Server</Link>
      <Nav />
      <ThemeToggle />
    </header>
  );
}