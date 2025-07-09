export function initializeTheme() {
  const theme = localStorage.getItem('theme') || 'evil';
  document.documentElement.dataset.theme = theme;
}

export function setupThemeToggle(toggleElId = 'theme-toggle') {
  const toggle = document.getElementById(toggleElId);
  if (!toggle) return;
  // initialize label
  toggle.textContent = document.documentElement.dataset.theme === 'evil' ? '😇' : '😈';
  toggle.addEventListener('click', () => {
    const root = document.documentElement;
    const next = root.dataset.theme === 'evil' ? 'angel' : 'evil';
    root.dataset.theme = next;
    localStorage.setItem('theme', next);
    toggle.textContent = next === 'evil' ? '😇' : '😈';
  });
}
