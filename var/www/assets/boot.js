// assets/boot.js: Main SPA bootstrap
import { initializeTheme, setupThemeToggle } from '/assets/theme.js';
import { loadHeader } from '/assets/header.js';
import { loadFooter } from '/assets/footer.js';
import { buildNavigation } from '/assets/navigation.js';

// 1. Dynamic view handlers map
const dynamicViews = {
  '/whoami': async c => (await import('/views/whoami.js')).loadWhoami(c),
  '/trial':  async c => (await import('/views/trial.js')).loadTrial(c),
  '/party':  async c => (await import('/views/party.js')).loadParty(c),
  '/expenses': async c => (await import('/views/expenses.js')).loadExpenses(c),
  '/drive':  async c => (await import('/views/drive.js')).loadDrive(c),
  '/home':   async c => (await import('/views/home.js')).loadHome(c),
  '/':       async c => (await import('/views/home.js')).loadHome(c),
};

document.addEventListener('DOMContentLoaded', async () => {
  try {

    // 0. Initialize theme (sets data-theme on <html>)
    initializeTheme();

    // 1. Attach global stylesheet if not present
    if (!document.querySelector('link[href="/assets/style.css"]')) {
      const style = document.createElement('link');
      style.rel = 'stylesheet';
      style.href = '/assets/style.css';
      document.head.appendChild(style);
    }

    // 2. Insert header and footer
    document.body.insertAdjacentHTML('afterbegin', loadHeader());
    document.body.insertAdjacentHTML('beforeend', loadFooter());

    // 3. Build navigation, passing SPA navigation handler
    await buildNavigation(dynamicViews, navigateTo);

    // wire up the theme toggle
    setupThemeToggle();
    
    // 4. Load initial view (and update nav highlight)
    navigateTo(location.pathname, { replace: true });

    // 5. History navigation (back/forward)
    window.addEventListener('popstate', () => {
      navigateTo(location.pathname, { push: false, replace: false });
    });
  } catch (err) {
    console.error('Boot error:', err);
  }
});

/**
 * SPA navigation: Load a view, update nav, update history if needed.
 */
async function navigateTo(path, opts = {}) {
  // path normalization
  let cleanPath = path.replace(/\/+$/, '') || '/home';
  if (cleanPath === '/') cleanPath = '/home';

  // Load the view
  await loadView(cleanPath);

  // Update nav link highlight
  updateCurrentLink(cleanPath);

  // Optionally update history
  if (opts.push)
    window.history.pushState({}, '', cleanPath);
  else if (opts.replace)
    window.history.replaceState({}, '', cleanPath);
}

/**
 * Dynamically load a view (JS or HTML) into <main id="main">
 */
async function loadView(path) {
  const mainEl = document.querySelector('main#main');
  try {
    if (dynamicViews[path]) {
      mainEl.innerHTML = '';
      await dynamicViews[path](mainEl);
      return;
    }
    // Try static HTML
    const res = await fetch(path);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    mainEl.innerHTML = await res.text();
  } catch (err) {
    mainEl.innerHTML = `<p>Error loading page: ${err.message}</p>`;
    console.error(err);
  }
}

/**
 * Highlight the current nav link (assumes nav links have hrefs matching the clean path)
 */
function updateCurrentLink(path) {
  // fallback: strip trailing slash, etc.
  let cleanPath = path.replace(/\/+$/, '') || '/home';
  if (cleanPath === '/') cleanPath = '/home';
  // Remove .active/current-page from all, add to current
  document.querySelectorAll('nav a').forEach(link => {
    link.classList.toggle('current-page', link.pathname === cleanPath);
    link.classList.toggle('active', link.pathname === cleanPath); // For old code compatibility
  });
}

/**
 * Navigation handler for SPA nav links (called by buildNavigation)
 */
export function spaNavHandler(event) {
  if (event.target.tagName === 'A' && event.target.href.startsWith(location.origin)) {
    event.preventDefault();
    const url = new URL(event.target.href);
    navigateTo(url.pathname, { push: true });
  }
}
