// views/home.js
// home.js: Dynamically loaded homepage content for your SPA

export async function loadHome(container) {
  container.innerHTML = `
  <section id="home">
    <h2>🚀 Welcome to the C-Server Project</h2>
    <p>
      This web application is powered by a custom-built <strong>POSIX-compliant C11 web server</strong>. It's designed from scratch to ensure high performance, efficiency, and educational value.
    </p>
    <hr>

    <section id="features">
      <h3>🔧 Features (Deep Dive)</h3>
      <ul>
        <li><strong>Pure C11 Implementation</strong>:
          <p>
            The server is developed using <strong>standard-compliant C11</strong>, ensuring portability, minimal dependencies, and maximum control over low-level resources. No external frameworks or unnecessary libraries.
          </p>
        </li>
        <li><strong>Epoll-based Non-blocking I/O</strong>:
          <p>
            Utilizing Linux's <code>epoll</code> mechanism provides highly scalable and efficient input/output handling. Epoll allows monitoring thousands of connections simultaneously without significant overhead.
          </p>
        </li>
        <li><strong>Dynamic Routing</strong>:
          <p>
            Robust routing logic supports serving static files, dynamic JavaScript-based views (like this page), and structured JSON API endpoints. The modular structure facilitates easy additions and improvements.
          </p>
        </li>
        <li><strong>Single Page Application (SPA)</strong>:
          <p>
            The frontend dynamically loads content, providing seamless transitions between views without full page reloads. This approach significantly improves the user experience by minimizing load times.
          </p>
        </li>
        <li><strong>Live Data Visualization with Chart.js</strong>:
          <p>
            Visualize dynamic data like expenses or real-time statistics using the powerful Chart.js library, enhancing interactivity and insight.
          </p>
        </li>
      </ul>
    </section>

    <section id="architecture">
      <h3>🧱 Project Structure Explained</h3>
      <pre><code>
      app          → Core C source code (server, routing, handlers)
      ├── core     → Low-level server logic (epoll, workers, sockets)
      ├── browser  → HTTP parsing, request handling, routing
      var/www      → Public assets (HTML, CSS, JS, images)
      var/lib      → Persistent application data (JSON, configurations)
      utils        → Supporting scripts and utilities
      </code></pre>
      <p>
        This modular structure clearly separates core logic, frontend assets, persistent data, and utilities. Each part is focused and maintainable.
      </p>
    </section>

    <section id="server-status">
      <h3>🛰️ Technical Server Details</h3>
      <ul>
        <li>Supports IPv4 and IPv6 with persistent connections (HTTP/1.1)</li>
        <li>All responses contain accurate MIME types and proper content lengths</li>
        <li>Detailed logging mechanism for easy debugging and monitoring</li>
      </ul>
    </section>

    <section id="about-me">
      <h3>👨‍🚀 About the Author</h3>
      <p>
        Hi! I'm Roman Horshkov — a systems engineer and enthusiast of low-level, performant software. This project is a foundational piece of my broader vision for a fully self-hosted, efficient infrastructure stack.
      </p>
      <p>
        Check out my projects and more on my <a href="https://github.com/RomanHorshkov" target="_blank">GitHub profile</a>.
      </p>
    </section>
  </section>
  `;
}
