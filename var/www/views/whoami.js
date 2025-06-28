export async function loadWhoami(container) {
  container.innerHTML = ''; // Clear the container before loading content

  const resp = await fetch('/api/whoami');
  if (!resp.ok) {
    container.textContent = 'Error loading whoami'; // Display error message
    return;
  }

  const data = await resp.json();

  // Create the main section for the "Who Am I?" view
  const section = document.createElement('section');
  section.id = 'whoami';

  const title = document.createElement('h2');
  title.textContent = 'Who Am I?';
  section.appendChild(title);

  const info = document.createElement('div');
  info.id = 'info';
  section.appendChild(info);

  container.appendChild(section);

  // Clock functionality
  let serverTime = new Date(data.server_time);
  const clockEl = document.createElement('div');
  clockEl.id = 'clock';
  info.appendChild(clockEl);

  const fmt = new Intl.DateTimeFormat(undefined, {
    hour: 'numeric',
    minute: 'numeric',
    second: 'numeric',
    fractionalSecondDigits: 3
  });

  function updateClock() {
    serverTime = new Date(serverTime.getTime() + 100);
    clockEl.textContent = fmt.format(serverTime);
  }
  updateClock();
  setInterval(updateClock, 100);

  // Meta information
  const meta = document.createElement('div');
  meta.innerHTML = `
    <p><strong>Method:</strong> ${data.method}</p>
    <p><strong>Path:</strong> ${data.path}</p>
    <h3>Headers</h3>
    <ul>
      ${Object.entries(data.headers).map(([k, v]) => `<li>${k}: ${v}</li>`).join('')}
    </ul>`;
  info.append(meta);
}
