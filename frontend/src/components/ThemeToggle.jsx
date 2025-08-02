import { useEffect, useState } from 'react';
import { THEMES, getInitialTheme, applyTheme } from '../theme';

export default function ThemeToggle() {
  const [theme, setTheme] = useState(getInitialTheme());
  useEffect(() => { applyTheme(theme); }, [theme]);
  return (
    <button id="theme-toggle" onClick={() => setTheme(t => t === THEMES.EVIL ? THEMES.ANGEL : THEMES.EVIL)}>
      {theme === THEMES.EVIL ? '😇' : '😈'}
    </button>
  );
}