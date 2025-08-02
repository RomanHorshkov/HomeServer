export const THEMES = { EVIL: 'evil', ANGEL: 'angel' };

export function getInitialTheme() {
  return localStorage.getItem('theme') || THEMES.EVIL;
}

export function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  localStorage.setItem('theme', theme);
}

export function initTheme() {
  applyTheme(getInitialTheme());
}