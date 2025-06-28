// Page bootstrapping logic for SPA
const dynamicViews = {
  '/whoami': async (container) => {
    const { loadWhoami } = await import('/views/whoami.js');
    return loadWhoami(container);
  },
  '/trial': async (container) => {
    const { loadTrial } = await import('/views/trial.js');
    return loadTrial(container);
  },
  // Add more dynamic views here as needed
};

document.addEventListener('DOMContentLoaded', async () => {
    try {
    // Inject stylesheet once
    const style = document.createElement('link');
    style.rel = 'stylesheet';
    style.href = '/assets/style.css';
    document.head.appendChild(style);

    // Load header and footer only once
    const [headerHtml, footerHtml] = await Promise.all([
        fetch('/assets/header.html').then(r => r.text()),
        fetch('/assets/footer.html').then(r => r.text())
        ]);
    document.body.insertAdjacentHTML('afterbegin', headerHtml);

    // Create main container if not present
    let mainEl = document.querySelector('main#main');
    if (!mainEl) {
        mainEl = document.createElement('main');
        mainEl.id = 'main';
        document.body.insertBefore(mainEl, document.querySelector('footer') || null);
    }
    document.body.insertAdjacentHTML('beforeend', footerHtml);

    // Build navigation from map.json
    const resp = await fetch('/map.json');
    if (!resp.ok) throw new Error(`Failed to fetch map.json: ${resp.status}`);
    const siteMap = await resp.json();

    // Select the navigation container inside the header
    const nav = document.querySelector('header nav'); 
    // Ensure the navigation container exists; throw an error if not found
    if (!nav) throw new Error('Navigation container not found');

    // Keep Home, rebuild rest
    const homeLink = nav.querySelector('a[href="/"]'); 
    // Find the existing "Home" link in the navigation (if it exists)

    nav.innerHTML = ''; 
    // Clear all existing navigation links to rebuild them dynamically

    if (homeLink) {
      nav.appendChild(homeLink); 
      // If the "Home" link exists, append it back to the navigation
    } else {
      nav.innerHTML = '<a href="/" class="nav-link">Home</a>'; 
      // If the "Home" link does not exist, create and add a new "Home" link
    }

    for (const [fileName, rawPath] of Object.entries(siteMap.views || {})) {
        // Skip the index.html file as it is typically the home page
        if (fileName === 'index.html') continue;

        // Determine if the file is dynamic (JavaScript-based view)
        const isDynamic = fileName.endsWith('.js') && !!dynamicViews[`/${fileName.replace(/\.js$/, '')}`];

        // Create a new anchor element for the navigation link
        const link = document.createElement('a');

        // Clean the rawPath to remove redundant prefixes like './'
        const cleanedPath = rawPath.replace(/^(\.\/)+/, '/');

        // Set the href attribute to the cleaned path
        link.href = isDynamic ? `/${fileName.replace(/\.js$/, '')}` : cleanedPath;

        // Add the 'nav-link' class for styling
        link.className = 'nav-link';

        // Set the link text to the title or a capitalized file name (without extension)
        link.textContent = fileName.replace(/\.(html|js)$/, '').charAt(0).toUpperCase() + fileName.replace(/\.(html|js)$/, '').slice(1);

        // Append the link to the navigation container
        nav.appendChild(link);
    }

    // Navigation click handler (SPA)
    nav.addEventListener('click', async e => {
    const a = e.target.closest('a.nav-link');
    if (!a || a.origin !== location.origin) return;  // external links pass through
    e.preventDefault();

    // Update URL
    history.pushState(null, '', a.getAttribute('href'));
    await loadView(a.getAttribute('href'));
    updateCurrentLink();
    });

    // Handle back/forward
    window.addEventListener('popstate', () => {
    loadView(location.pathname);
    updateCurrentLink();
    });

    // Utility: load view into <main> (brutal, pure path)
    async function loadView(path) {
        try {
        // Normalize path (e.g. /whoami or /whoami/)
        let cleanPath = path.replace(/\/+$/, '');
        if (cleanPath === '') cleanPath = '/';

        // If a dynamic view, run its handler
        if (dynamicViews[cleanPath]) {
        mainEl.innerHTML = ''; // clear old content
        await dynamicViews[cleanPath](mainEl);
        return;
    }

        // Otherwise, try fetching (static HTML)
        // const fetchPath = cleanPath.endsWith('.html') ? cleanPath : `${cleanPath}.html`;
        const res = await fetch(cleanPath);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const html = await res.text();
        mainEl.innerHTML = html;
        } catch (err) {
        mainEl.innerHTML = `<p>Error loading page: ${err.message}</p>`;
        console.error(err);
        }
    }


    // Utility: highlight current nav link
    function updateCurrentLink() {
        nav.querySelectorAll('.nav-link.current-page').forEach(el => el.classList.remove('current-page'));
        const selector = `.nav-link[href=\"${location.pathname}\"]`;
        const active = nav.querySelector(selector);
        if (active) active.classList.add('current-page');
    }

    // 6️⃣ Initial load
    await loadView(location.pathname);
    updateCurrentLink();

    } catch (err) {
        console.error('Boot error:', err);
    }
});
