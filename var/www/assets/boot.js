// assets/boot.js: Main SPA bootstrap
import { loadHeader } from '/assets/header.js';
import { loadFooter } from '/assets/footer.js';
import { buildNavigation, updateCurrentLink } from '/assets/navigation.js';

// Map client paths to dynamic view loaders
const dynamicViews = {
  '/whoami': async container => {
    const { loadWhoami } = await import('/views/whoami.js');
    return loadWhoami(container);
  },
  '/trial': async container => {
    const { loadTrial } = await import('/views/trial.js');
    return loadTrial(container);
  },
  '/party': async container => {
    const { loadParty } = await import('/views/party.js');
    return loadParty(container);
  },
  '/expenses': async container => {
    const { loadExpenses } = await import('/views/expenses.js');
    return loadExpenses(container);
  },
  '/drive': async container => {
    const { loadDrive } = await import('/views/drive.js');
    return loadDrive(container);
  },
  '/': async container => {
    const { loadHome } = await import('/views/home.js');
    return loadHome(container);
  },
};

document.addEventListener('DOMContentLoaded', async () => {
  try {
    // 1 Attach global stylesheet
    const style = document.createElement('link');
    style.rel = 'stylesheet';
    style.href = '/assets/style.css';
    document.head.appendChild(style);

    // 2 Insert header and footer
    document.body.insertAdjacentHTML('afterbegin', loadHeader());
    document.body.insertAdjacentHTML('beforeend', loadFooter());

    // 3 Build navigation (passes loadView and updateCurrentLink)
    await buildNavigation(dynamicViews, loadView);

    // 4 Load initial view
    await loadView(location.pathname);
    updateCurrentLink();

    // 5 Handle back/forward
    window.addEventListener('popstate', () => {
      loadView(location.pathname);
      updateCurrentLink();
    });
  } catch (err) {
    console.error('Boot error:', err);
  }
});

/**
 * Dynamically load a view (static HTML or JS-based) into <main id="main">
 */
async function loadView(path) {
  const mainEl = document.querySelector('main#main');
  try {
    let clean = path.replace(/\/+$|^\s+|\s+$/g, '');
    if (!clean) clean = '/';

    if (dynamicViews[clean]) {
      mainEl.innerHTML = '';
      await dynamicViews[clean](mainEl);
      return;
    }

    // Fallback: fetch static HTML
    const res = await fetch(clean.endsWith('.html') ? clean : `${clean}.html`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    mainEl.innerHTML = await res.text();
  } catch (err) {
    mainEl.innerHTML = `<p>Error loading view: ${err.message}</p>`;
    console.error(err);
  }
}
