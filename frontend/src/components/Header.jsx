import { Link } from 'react-router-dom';
import Nav from './Nav';
import ThemeToggle from './ThemeToggle';

export default function Header() {
  return (
    <header className="site-header grid grid-cols-3 items-center px-4 py-2">
      {/* left: logo */}
      <Link to="/" className="logo text-xl font-semibold justify-self-start">
        C-Server
      </Link>

      {/* center: Nav (Home/Test) */}
      <div className="justify-self-center">
        <Nav />
      </div>

      {/* right: Login then Theme toggle */}
      <div className="justify-self-end flex items-center gap-3">
        <Link
          to="/login"
          className="rounded-xl px-3 py-1.5 text-sm font-medium border
                     hover:shadow-sm transition
                     border-neutral-300 bg-white text-neutral-900
                     dark:border-neutral-700 dark:bg-neutral-800 dark:text-neutral-100"
        >
          Login
        </Link>
        <ThemeToggle />
      </div>
    </header>
  );
}
