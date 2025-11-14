// www/build_notes/load-notes.js
(async function () {
    const root = '/build_notes';
    const manifest = await fetch(`${root}/manifest.json`).then(r => r.json());

    /* -------------------------------------------------------- *
     *  DIAGRAMS
     * -------------------------------------------------------- */
    const sel    = document.getElementById('diagramSelect');
    const iframe = document.getElementById('diagram');

    // populate the dropdown
    manifest.diagrams.forEach(fn => {
        const opt = document.createElement('option');
        opt.value       = fn;
        opt.textContent = fn.replace(/\.puml$/, '');
        sel.appendChild(opt);
    });

    // initial load + onchange handler
    sel.onchange = () => loadDiagram(sel.value);
    loadDiagram(sel.value);

    async function loadDiagram(filename) {
        try {
            const txt     = await fetch(`${root}/diagrams/${filename}`).then(r => r.text());
            const encoded = plantumlEncoder.encode(txt);
            const svgUrl  = `https://www.plantuml.com/plantuml/svg/${encoded}`;

            /* measure SVG size so the <iframe> fits snugly */
            const svg     = await fetch(svgUrl).then(r => r.text());
            const div     = document.createElement('div');
            div.innerHTML = svg;
            const svgElem = div.querySelector('svg');
            if (!svgElem) throw new Error('Invalid SVG');

            const { width, height } = svgElem.viewBox.baseVal;
            iframe.style.width  = `${width}px`;
            iframe.style.height = `${height}px`;
            iframe.src          = svgUrl;
        } catch (err) {
            console.error('Failed to load diagram:', err);
        }
    }

    /* -------------------------------------------------------- *
     *  NOTES (Markdown → HTML)
     * -------------------------------------------------------- */
    const container = document.getElementById('notesContainer');

    manifest.notes.forEach(async fn => {
        try {
            const text  = await fetch(`${root}/notes/${fn}`).then(r => r.text());

            // 1. full Markdown render
            const dirty = marked.parse(text, { mangle: false, headerIds: false });

            // 2. sanitise to strip <script>, on* handlers, etc.
            const html  = DOMPurify.sanitize(dirty);

            // create collapsible note
            const details = document.createElement('details');
            details.classList.add('post');

            const sum = document.createElement('summary');
            sum.classList.add('post-title');
            sum.textContent = fn.replace(/\.(md|txt)$/i, '').replace(/_/g, ' ');
            details.appendChild(sum);

            const body = document.createElement('div');
            body.classList.add('post-body');
            body.innerHTML = html;
            details.appendChild(body);

            container.appendChild(details);

        } catch (err) {
            console.error('Failed to load note:', err);
        }
    });
})();
