// views/drive.js

export async function loadDrive(container) {
  // Use global stylesheet (assumes style.css is already loaded by SPA boot)
  container.innerHTML = `
    <h1>Drive</h1>
    <div class="toolbar" style="display:flex;align-items:center;gap:1rem;">
      <button id="upBtn" title="Up one level">
        <span style="font-size:1.2em;vertical-align:middle;">&#8593;</span> Up
      </button>
      <span id="breadcrumbs" class="breadcrumbs">/</span>
    </div>
    <div id="tree"></div>
  `;

  const tree   = container.querySelector('#tree');
  const crumbs = container.querySelector('#breadcrumbs');
  const upBtn  = container.querySelector('#upBtn');
  let currentPath = '/';

  // Font Awesome replacement: fallback to text icons, unless already loaded by your CSS elsewhere.
  const folderIcon = `<span class="entry-icon" style="margin-right:0.4em;">&#128193;</span>`;
  const fileIcon   = `<span class="entry-icon" style="margin-right:0.4em;">&#128196;</span>`;

  const enc = p => p.split('/').map(encodeURIComponent).join('/');
  const parent = p => (p === '/') ? '/' : '/' + p.replace(/^\/+/, '').replace(/\/+$/, '').split('/').slice(0, -1).join('/');

  function load(path, container) {
    fetch('/api/drive?path=' + enc(path))
      .then(r => r.json())
      .then(data => {
        container.innerHTML = '';
        data.items.sort((a, b) => a.type.localeCompare(b.type) || a.name.localeCompare(b.name));
        data.items.forEach(item => {
          const full = path + (path.endsWith('/') ? '' : '/') + item.name;
          if (item.type === 'directory') {
            const det = document.createElement('details');
            det.innerHTML = `<summary><span class="arrow">&#9656;</span>${folderIcon}<span class="directory">${item.name}</span></summary>`;
            det.addEventListener('toggle', () => {
              if (det.open && !det.dataset.loaded) {
                const ul = document.createElement('ul');
                det.appendChild(ul);
                load(full, ul);
                det.dataset.loaded = 1;
              }
            });
            container.appendChild(det);
          } else {
            const li = document.createElement('li');
            li.innerHTML = `${fileIcon}<a href="${full}" class="file" target="_blank" rel="noopener">${item.name}</a>`;
            container.appendChild(li);
          }
        });
      })
      .catch(err => {
        console.error(err);
        container.innerHTML = '<p style="color:#f55">Failed to load directory.</p>';
      });
  }

  function refresh() {
    crumbs.textContent = currentPath;
    upBtn.disabled = currentPath === '/';
    tree.innerHTML = '';
    load(currentPath, tree);
  }

  upBtn.addEventListener('click', () => {
    currentPath = parent(currentPath);
    refresh();
  });

  refresh();
}
