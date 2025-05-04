document.addEventListener('DOMContentLoaded', async () => {
    /* Fetch once, then insert at the very top of <body> */
    const html = await fetch('/assets/header.html').then(r => r.text());
    document.body.insertAdjacentHTML('afterbegin', html);
});
