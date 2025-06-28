// Page bootstrapping logic
document.addEventListener('DOMContentLoaded', async () => {
    try {
        // Load the header and insert it at the very top
        const headerHtml = await fetch('/assets/header.html').then(res => res.text());
        document.body.insertAdjacentHTML('afterbegin', headerHtml);

        // Add a <main> element for content separation
        const mainElement = document.createElement('main');
        mainElement.id = 'main';
        document.body.appendChild(mainElement);

        // Load the footer and insert it at the very bottom
        const footerHtml = await fetch('/assets/footer.html').then(res => res.text());
        document.body.insertAdjacentHTML('beforeend', footerHtml);

        // Build dynamic navigation
        const response = await fetch('/map.json');
        if (!response.ok) {
            throw new Error(`Failed to fetch map.json: ${response.status} ${response.statusText}`);
        }

        const siteMap = await response.json();
        const nav = document.querySelector('header nav');
        if (!nav) {
            console.error('[Header] Navigation container not found!');
            return;
        }

        // Clear existing navigation links, keeping the Home link if it exists
        const homeLink = nav.querySelector('a[href="/"]');
        nav.innerHTML = ''; // Remove all existing links
        if (homeLink) {
            nav.appendChild(homeLink); // Re-add the Home link
        } else {
            // Create and add a Home link if it doesn't exist
            const home = document.createElement('a');
            home.href = '/';
            home.textContent = 'Home';
            home.className = 'nav-link';
            nav.appendChild(home);
        }

        // Check if the site map contains views
        if (!siteMap.views) {
            console.warn('[Header] No views found in site map');
            return; // Exit if no views are available
        }

    // Iterate over the views in the site map to build navigation links
    for (const [fileName, path] of Object.entries(siteMap.views)) {
        
        // Skip the index.html file
        if (fileName === 'index.html') continue;

        // Extract the base name without extension (e.g. "drive" from "drive.html")
        const pageName = fileName.replace(/\.html$/, '');

        // Create a new navigation link element
        const link = document.createElement('a');
        // Use the raw path, but make it absolute by stripping "./"
        // this is done to ensure the new link is absolute and not relative to the current page
        link.href = path.replace(/^\.\//, '/');
        link.className = 'nav-link';

        // Use the title from path if available, otherwise fallback to prettified pageName
        if (path && path.title) {
            link.textContent = path.title;
        } else {
            // Capitalize only the first letter, rest lowercase
            link.textContent = pageName.charAt(0).toUpperCase() + pageName.slice(1);
        }

        // Highlight the current page link based on the URL path
        if (window.location.pathname === `/page/${pageName}`) {
            link.classList.add('current-page');
        }

        nav.appendChild(link);
    }


    } catch (error) {
        console.error("Error loading header, footer, or navigation:", error);
    }
});
