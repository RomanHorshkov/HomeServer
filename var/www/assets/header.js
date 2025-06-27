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
        for (const [fileName, pageData] of Object.entries(siteMap.views)) {
            if (fileName === 'index.html') continue; // Skip the index.html file

            // Extract the page name by removing the .html extension
            const pageName = fileName.replace('.html', '');

            // Create a new navigation link element
            const link = document.createElement('a');
            link.href = `/page/${pageName}`; // Set the link's href attribute
            link.className = 'nav-link'; // Add the nav-link class for styling

            // Use the title from pageData if available, otherwise fallback to capitalized page name
            if (pageData && pageData.title) {
                link.textContent = pageData.title; // Set the link text to the title
            } else {
                link.textContent = pageName.charAt(0).toUpperCase() + pageName.slice(1); // Capitalize the page name
            }

            // Highlight the current page link based on the URL path
            if (window.location.pathname === `/page/${pageName}`) {
                link.classList.add('current-page'); // Add the current-page class
            }

            // Append the link to the navigation container
            nav.appendChild(link);
        }

    } catch (error) {
        console.error("Error loading header, footer, or navigation:", error);
    }
});
