import React, { useEffect, useRef } from 'react';

export default function Expenses() {
  const barRef = useRef(null);
  const pieRef = useRef(null);
  const charts = useRef({ bar: null, pie: null });

  useEffect(() => {
    let isMounted = true;

    (async () => {
      const { default: Chart } = await import('chart.js/auto');

      if (!isMounted) return;
      if (barRef.current && !charts.current.bar) {
        charts.current.bar = new Chart(barRef.current, {
          type: 'bar',
          data: {
            labels: ['Mon', 'Tue', 'Wed', 'Thu', 'Fri'],
            datasets: [{ label: '€', data: [12, 19, 3, 5, 2] }]
          }
        });
      }
      if (pieRef.current && !charts.current.pie) {
        charts.current.pie = new Chart(pieRef.current, {
          type: 'pie',
          data: {
            labels: ['Food', 'Bills', 'Fun'],
            datasets: [{ data: [40, 35, 25] }]
          }
        });
      }
    })();

    return () => {
      isMounted = false;
      Object.values(charts.current).forEach(c => c && c.destroy());
      charts.current = { bar: null, pie: null };
    };
  }, []);

  return (
    <section className="prose max-w-none">
      <h2>Expenses</h2>
      <div className="grid md:grid-cols-2 gap-6">
        <canvas ref={barRef} height="200" />
        <canvas ref={pieRef} height="200" />
      </div>
    </section>
  );
}
