// views/party.js

// CSS for the party page, injected dynamically so it's loaded only with this view
const partyCss = `
@keyframes rainbow {
  0%   { background-position: 0%   50%; }
  50%  { background-position: 100% 50%; }
  100% { background-position: 0%   50%; }
}
.party-bg {
  background: linear-gradient(270deg,
    #ff0000, #ff7f00, #ffff00, #00ff00,
    #0000ff, #4b0082, #8f00ff);
  background-size: 1400% 1400%;
  animation: rainbow 10s ease infinite;
  color: white;
  text-align: center;
  padding: 2rem;
  font-family: sans-serif;
  min-height: 100vh;
}
@keyframes pulse {
  0%   { transform: scale(1);   opacity: 1; }
  50%  { transform: scale(1.2); opacity: 0.7;}
  100% { transform: scale(1);   opacity: 1; }
}
.pulse { animation: pulse 2s ease-in-out infinite; }
@keyframes fadeIn {
  from { opacity: 0; }
  to   { opacity: 1; }
}
.fade-in { animation: fadeIn 2s forwards; }
#typewriter {
  margin-top: 2rem;
  font-size: 1.5rem;
  white-space: pre-wrap;
}
#slideshow {
  width: 200px;
  margin-top: 2rem;
  border: 5px solid white;
  border-radius: 10px;
  animation: slideDown 1s ease-out forwards;
}
@keyframes slideDown {
  from { transform: translateY(-50px); opacity: 0; }
  to   { transform: translateY(0);     opacity: 1; }
}
`;

export async function loadParty(container) {
  // Inject style for the party view only if not already injected
  if (!document.getElementById('party-css')) {
    const style = document.createElement('style');
    style.id = 'party-css';
    style.textContent = partyCss;
    document.head.appendChild(style);
  }

  // Set up the main content
  container.className = 'party-bg'; // Rainbow animated background
  container.innerHTML = `
    <h1 class="pulse">Welcome to the Party!</h1>
    <div id="typewriter" class="fade-in"></div>
    <img id="slideshow" src="/images/img1.jpg" alt="slide">
  `;

  // Typewriter effect
  function typeWriter(el, text, delay=100) {
    el.textContent = '';
    let i = 0;
    const timer = setInterval(() => {
      el.textContent += text.charAt(i);
      i++;
      if (i >= text.length) clearInterval(timer);
    }, delay);
  }

  const tw = container.querySelector('#typewriter');
  typeWriter(
    tw,
    "This page is super-colored!\nText types itself,\nImages slide down,\nEverything pulses!",
    75
  );

  // Simple slideshow
  const images = [
    '/images/img1.jpg',
    '/images/img2.jpg',
    '/images/img3.jpg',
    '/images/img4.jpg'
  ];
  let idx = 0;
  const slide = container.querySelector('#slideshow');
  setInterval(() => {
    idx = (idx + 1) % images.length;
    slide.src = images[idx];
  }, 3000);
}
