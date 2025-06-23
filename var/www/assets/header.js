document.addEventListener('DOMContentLoaded', async () => {
    try {
        // Load the header and insert it at the very top
        const headerHtml = await fetch('/assets/header.html').then(res => res.text());
        document.body.insertAdjacentHTML('afterbegin', headerHtml);

        // Load the footer and insert it at the very bottom
        const footerHtml = await fetch('/assets/footer.html').then(res => res.text());
        document.body.insertAdjacentHTML('beforeend', footerHtml);

        // Optional: highlight the active link based on the current path
        const currentPath = window.location.pathname;
        const navLinks = document.querySelectorAll("header nav a");
        navLinks.forEach(link => {
            if (link.getAttribute("href") === currentPath) {
                link.classList.add("active");
            }
        });
    } catch (error) {
        console.error("Error loading header or footer:", error);
    }
});
