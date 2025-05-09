// www/build_notes/load-notes.js
(async function() {
  const root = '/build_notes';
  const m = await fetch(`${root}/manifest.json`).then(r=>r.json());

  // 3a) build the diagrams dropdown
  const sel = document.getElementById('diagramSelect');
  m.diagrams.forEach(fn => {
    const opt = document.createElement('option');
    opt.value = fn; opt.textContent = fn.replace(/\.puml$/, '');
    sel.appendChild(opt);
  });
  sel.onchange = updateDiagram;
  function updateDiagram() {
    const file = sel.value;
    fetch(`${root}/diagrams/${file}`).then(r=>r.text()).then(txt=>{
      const h = plantumlEncoder.encode(txt);
      document.getElementById('diagram').src =
        `https://www.plantuml.com/plantuml/svg/${h}`;
    });
  }
  updateDiagram(); // load first

  // 3b) build the notes accordion
  const container = document.getElementById('notesContainer');
  m.notes.forEach(fn => {
    fetch(`${root}/notes/${fn}`)
      .then(r=>r.text())
      .then(text => {
        // if using markdown, convert here (e.g. marked.parse)
        const html = text
          .split('\n\n')
          .map(p => `<p>${p}</p>`).join('');
        const details = document.createElement('details');
        details.classList.add('post');

        const sum = document.createElement('summary');
        sum.classList.add('post-title');
        sum.textContent = fn.replace(/\.(md|txt)$/, '').replace(/_/g,' ');
        details.appendChild(sum);

        const body = document.createElement('div');
        body.classList.add('post-body');
        body.innerHTML = html;
        details.appendChild(body);

        container.appendChild(details);
      });
  });
})();
