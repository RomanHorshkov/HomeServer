import { loadHeader } from '/assets/header.js';
import { loadFooter } from '/assets/footer.js';
import { buildNavigation } from '/assets/navigation.js';

const dynamicViews = {
  '/whoami': async (container) => {
    const { loadWhoami } = await import('/views/whoami.js');
    return loadWhoami(container);
  },
  '/trial': async (container) => {
    const { loadTrial } = await import('/views/trial.js');
    return loadTrial(container);
  },
  '/party': async (container) => {
    const { loadParty } = await import('/views/party.js');
    return loadParty(container);
  },
  '/expenses': async (container) => {
    const { loadExpenses } = await import('/views/expenses.js');
    return loadExpenses(container); 
  },
  '/drive': async (container) => {
    const { loadDrive } = await import('/views/drive.js');
    return loadDrive(container);
  },
};

document.addEventListener('DOMContentLoaded', async () => {
  try {
    // Inject stylesheet
    const style = document.createElement('link');
    style.rel = 'stylesheet';
    style.href = '/assets/style.css';
    document.head.appendChild(style);

    // Insert header and footer
    document.body.insertAdjacentHTML('afterbegin', loadHeader());
    document.body.insertAdjacentHTML('beforeend', loadFooter());

    // Build Navigation
    await buildNavigation(dynamicViews, loadView);

    // Initial view load
    await loadView(location.pathname);
    updateCurrentLink();

    window.addEventListener('popstate', () => {
      loadView(location.pathname);
      updateCurrentLink();
    });
  } catch (err) {
    console.error('Boot error:', err);
  }
});

async function loadView(path) {
  const mainEl = document.querySelector('main#main');
  try {
    let cleanPath = path.replace(/\/+$/, '') || '/';

    if (dynamicViews[cleanPath]) {
      mainEl.innerHTML = '';
      await dynamicViews[cleanPath](mainEl);
      return;
    }

    const res = await fetch(cleanPath);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    mainEl.innerHTML = await res.text();
  } catch (err) {
    mainEl.innerHTML = `<p>Error loading page: ${err.message}</p>`;
    console.error(err);
  }
}

function updateCurrentLink() {
  document.querySelectorAll('.nav-link.current-page')
    .forEach(el => el.classList.remove('current-page'));
  const active = document.querySelector(`.nav-link[href="${location.pathname}"]`);
  if (active) active.classList.add('current-page');
}
