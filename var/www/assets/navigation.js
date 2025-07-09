// navigation.js: Dynamically builds the navigation menu and handles SPA navigation

// Function to build the navigation menu dynamically
export async function buildNavigation(dynamicViews, loadView) {
  // Fetch site map (map.json)
  const resp = await fetch('/map.json');
  if (!resp.ok) throw new Error(`Failed to fetch map.json: ${resp.status}`);
  const siteMap = await resp.json();

  const nav = document.querySelector('header nav');
  nav.innerHTML = ''; // Clear any existing nav

  // 1. Add Home link first (if present)
  if (siteMap.views && siteMap.views['home.js']) {
    const link = document.createElement('a');
    link.href = '/home';
    link.className = 'nav-link';
    link.textContent = 'Home';
    nav.appendChild(link);
  }

  // 2. Add all other views (except home.js)
  for (const [fileName, rawPath] of Object.entries(siteMap.views || {})) {
    if (!fileName.endsWith('.js')) continue;
    if (fileName === 'home.js') continue; // already added above

    const isDynamic = dynamicViews[`/${fileName.replace(/\.js$/, '')}`];
    const link = document.createElement('a');
    link.href = isDynamic ? `/${fileName.replace(/\.js$/, '')}` : rawPath.replace(/^(\.\/)+/, '/');
    link.className = 'nav-link';
    link.textContent = fileName.replace(/\.js$/, '').replace(/^\w/, c => c.toUpperCase());
    nav.appendChild(link);
  }

  // SPA navigation click handling
  nav.addEventListener('click', async e => {
    const a = e.target.closest('a.nav-link');
    if (!a || a.origin !== location.origin) return;
    e.preventDefault();
    history.pushState(null, '', a.getAttribute('href'));
    await loadView(a.getAttribute('href'));
    updateCurrentLink();
  });
}

// Function to highlight the current navigation link
function updateCurrentLink() {
  // Remove the 'current-page' class from all navigation links
  document.querySelectorAll('.nav-link.current-page')
    .forEach(el => el.classList.remove('current-page'));

  // Add the 'current-page' class to the active navigation link
  const active = document.querySelector(`.nav-link[href="${location.pathname}"]`);
  if (active) active.classList.add('current-page');
}

// Export the updateCurrentLink function for external use
export { updateCurrentLink };
