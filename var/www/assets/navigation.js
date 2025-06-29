export async function buildNavigation(dynamicViews, loadView) {
  const resp = await fetch('/map.json');
  if (!resp.ok) throw new Error(`Failed to fetch map.json: ${resp.status}`);
  const siteMap = await resp.json();

  const nav = document.querySelector('header nav');
  nav.innerHTML = '<a href="/" class="nav-link">Home</a>';

  for (const [fileName, rawPath] of Object.entries(siteMap.views || {})) {
    if (fileName === 'index.html') continue;

    const isDynamic = fileName.endsWith('.js') && dynamicViews[`/${fileName.replace(/\.js$/, '')}`];
    const link = document.createElement('a');
    link.href = isDynamic ? `/${fileName.replace(/\.js$/, '')}` : rawPath.replace(/^(\.\/)+/, '/');
    link.className = 'nav-link';
    link.textContent = fileName.replace(/\.(html|js)$/, '')
      .replace(/^\w/, c => c.toUpperCase());

    nav.appendChild(link);
  }

  nav.addEventListener('click', async e => {
    const a = e.target.closest('a.nav-link');
    if (!a || a.origin !== location.origin) return;
    e.preventDefault();

    history.pushState(null, '', a.getAttribute('href'));
    await loadView(a.getAttribute('href'));
    updateCurrentLink();
  });
}

function updateCurrentLink() {
  document.querySelectorAll('.nav-link.current-page')
    .forEach(el => el.classList.remove('current-page'));
  const active = document.querySelector(`.nav-link[href="${location.pathname}"]`);
  if (active) active.classList.add('current-page');
}

export { updateCurrentLink };
