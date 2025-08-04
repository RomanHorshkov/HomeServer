import React from 'react';

export default function Home() {
  return (
    <section id="home" className="prose max-w-none">
      <h2>🚀 Welcome to the C-Server Project</h2>
      <p>
        This web application is powered by a custom-built <strong>POSIX-compliant C11 web server</strong>.
        It's designed from scratch to ensure high performance, efficiency, and educational value.
      </p>
      <hr />
      <section id="features">
        <h3>🔧 Features (Deep Dive)</h3>
        <ul>
          <li>
            <strong>Pure C11 Implementation</strong>:
            <p>
              The server is developed using <strong>standard-compliant C11</strong>, ensuring portability and control over low-level resources.
            </p>
          </li>
          <li>
            <strong>Epoll-based Non-blocking I/O</strong>:
            <p>
              Uses Linux <code>epoll</code> for scalable I/O, monitoring thousands of connections efficiently.
            </p>
          </li>
          <li>
            <strong>Dynamic Routing</strong>:
            <p>
              Robust routing for static files, dynamic views, and JSON APIs with a modular structure.
            </p>
          </li>
          <li>
            <strong>Single Page Application (SPA)</strong>:
            <p>
              Client-side routing with React provides seamless transitions without full reloads.
            </p>
          </li>
          <li>
            <strong>Live Data Visualization with Chart.js</strong>:
            <p>
              Visualize dynamic data like expenses or real-time stats.
            </p>
          </li>
        </ul>
      </section>

      <section id="server-status">
        <h3>🛰️ Technical Server Details</h3>
        <ul>
          <li>IPv4/IPv6, persistent HTTP/1.1 keep-alive</li>
          <li>Accurate MIME types and content lengths</li>
          <li>Detailed logging for debugging and monitoring</li>
        </ul>
      </section>

      <section id="about-me">
        <h3>👨‍🚀 About the Author</h3>
        <p>
          Hi! I'm Roman Horshkov — a systems engineer and enthusiast of low-level, performant software.
        </p>
        <p>
          Check out my projects on <a href="https://github.com/RomanHorshkov" target="_blank" rel="noreferrer">GitHub</a>.
        </p>
      </section>
    </section>
  );
}
