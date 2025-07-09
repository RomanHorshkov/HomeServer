// views/drive.js
export async function loadDrive(container) {
  // Load the file map via fetch (ensure map.json is served at root)
  let mapData;
  try {
    const resp = await fetch('/map.json');
    mapData = await resp.json();
  } catch (err) {
    container.innerHTML = '<p style="color:#f55">Failed to load file map.</p>';
    console.error('Error fetching map.json:', err);
    return;
  }

  // Inject UI
  container.innerHTML = `
    <h1>Drive</h1>
    <div class="toolbar" style="display:flex;align-items:center;gap:1rem;">
      <button id="upBtn" title="Up one level">🔼 Up</button>
      <span id="breadcrumbs" class="breadcrumbs">/</span>
    </div>
    <div id="tree"></div>
  `;

  const tree   = container.querySelector('#tree');
  const crumbs = container.querySelector('#breadcrumbs');
  const upBtn  = container.querySelector('#upBtn');
  let currentPath = [];

  // Icons
  const folderIcon = `<span class="entry-icon">📁</span>`;
  const fileIcon   = `<span class="entry-icon">📄</span>`;

  // Helper: get node at path array
  function getNode(pathArr) {
    return pathArr.reduce((node, seg) => node[seg], mapData);
  }

  // Render directory tree
  function render(pathArr, containerEl) {
    const node = getNode(pathArr);
    const entries = Object.entries(node)
      .sort(([aName, aVal], [bName, bVal]) => {
        const aDir = typeof aVal === 'object';
        const bDir = typeof bVal === 'object';
        if (aDir !== bDir) return aDir ? -1 : 1;
        return aName.localeCompare(bName);
      });

    entries.forEach(([name, value]) => {
      const det = document.createElement('details');
      if (typeof value === 'object') {
        det.innerHTML = `<summary>${folderIcon}<span class="directory">${name}</span></summary>`;
        det.addEventListener('toggle', () => {
          if (det.open && !det.dataset.loaded) {
            const ul = document.createElement('ul');
            det.appendChild(ul);
            render([...pathArr, name], ul);
            det.dataset.loaded = '1';
          }
        });
      } else {
        det.innerHTML = `<summary>${fileIcon}<a href="/${value}" class="file" target="_blank" rel="noopener">${name}</a></summary>`;
      }
      containerEl.appendChild(det);
    });
  }

  // Refresh UI
  function refresh() {
    crumbs.textContent = '/' + currentPath.join('/');
    upBtn.disabled = currentPath.length === 0;
    tree.innerHTML = '';
    render(currentPath, tree);
  }

  // Up navigation
  upBtn.addEventListener('click', () => {
    if (currentPath.length > 0) {
      currentPath.pop();
      refresh();
    }
  });

  // Initial load
  refresh();
}
