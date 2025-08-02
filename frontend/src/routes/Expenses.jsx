import { useEffect, useMemo, useRef, useState } from 'react';
import { apiGet, apiPut } from '../api/client';
import Chart from 'chart.js/auto';
import '../styles/expenses.css';

export default function Expenses(){
  const [all, setAll] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  // filters
  const [fromDate, setFromDate] = useState(''); // yyyy-mm-dd
  const [toDate, setToDate] = useState('');     // yyyy-mm-dd

  // charts refs
  const barRef = useRef(null);
  const pieRef = useRef(null);
  const barChartRef = useRef(null);
  const pieChartRef = useRef(null);

  // add form
  const [savingMsg, setSavingMsg] = useState('');
  const [categories, setCategories] = useState([]);
  const [form, setForm] = useState(() => ({
    date: new Date().toISOString().slice(0,10), // yyyy-mm-dd for input[type=date]
    category: '',
    amount: '',
    comment: ''
  }));

  useEffect(() => {
    let dead = false;
    (async () => {
      try {
        setLoading(true);
        // 1) months list
        const months = await apiGet('/api/expenses'); // ["2024-12", "2025-01", ...]
        // 2) load each month json
        const chunks = await Promise.all(months.map(ms => {
          const [y, m] = ms.split('-');
          return apiGet(`/api/expenses/${y}/${m}.json`);
        }));
        const flat = chunks.flat();
        // enrich
        for (const e of flat) {
          const [d, m, y] = e.date.split('/').map(Number);
          e.dateObj = new Date(y, m-1, d);
        }
        if (!dead) setAll(flat);
      } catch (e) {
        if (!dead) setError(String(e.message || e));
      } finally {
        if (!dead) setLoading(false);
      }
    })();

    // categories
    apiGet('/api/expenses/settings.json')
      .then(s => Array.isArray(s?.categories) ? setCategories(s.categories) : setCategories([]))
      .catch(() => setCategories([]));

    return () => { dead = true; destroyCharts(); };
  }, []);

  function destroyCharts(){
    if (barChartRef.current){ barChartRef.current.destroy(); barChartRef.current = null; }
    if (pieChartRef.current){ pieChartRef.current.destroy(); pieChartRef.current = null; }
  }

  // filtered list
  const filtered = useMemo(() => {
    if (!fromDate && !toDate) return all;
    const from = fromDate ? new Date(...fromDate.split('-').map((n,i)=> i===1?Number(n)-1:Number(n))) : null;
    const to   = toDate   ? new Date(...toDate.split('-').map((n,i)=> i===1?Number(n)-1:Number(n))) : null;
    if (to) to.setHours(23,59,59,999);
    return all.filter(e => {
      const t = e.dateObj.getTime();
      if (from && t < from.getTime()) return false;
      if (to   && t > to.getTime())   return false;
      return true;
    });
  }, [all, fromDate, toDate]);

  // data for charts
  const chartData = useMemo(() => {
    const byMonth = new Map(); // key: YYYY-MM → entries[]
    for (const e of filtered){
      const key = `${e.dateObj.getFullYear()}-${String(e.dateObj.getMonth()+1).padStart(2,'0')}`;
      if (!byMonth.has(key)) byMonth.set(key, []);
      byMonth.get(key).push(e);
    }
    const months = Array.from(byMonth.keys()).sort();

    // categories list (sorted stable)
    const cats = Array.from(new Set(filtered.map(e => e.category)));

    // stacked bar datasets per category
    const datasets = cats.map((cat, idx) => ({
      label: cat,
      data: months.map(mo => byMonth.get(mo).filter(e => e.category === cat).reduce((s,e)=>s+e.amount, 0)),
      backgroundColor: `hsl(${(idx*70)%360},70%,60%)`,
      borderColor: '#111', borderWidth: 1, hoverBorderColor: '#fff'
    }));

    // pie totals
    const totalsByCat = cats.map(cat => filtered.filter(e => e.category===cat).reduce((s,e)=>s+e.amount, 0));
    const pieColors = cats.map((_,i)=>`hsl(${(i*60)%360},70%,60%)`);

    return { months, cats, datasets, pieTotals: totalsByCat, pieColors };
  }, [filtered]);

  // instantiate/update charts when data changes
  useEffect(() => {
    if (!barRef.current || !pieRef.current) return;

    // const Chart = window.Chart || (await import('chart.js/auto')).default; // fallback if needed

    if (!barChartRef.current){
      barChartRef.current = new Chart(barRef.current.getContext('2d'), {
        type: 'bar',
        data: { labels: [], datasets: [] },
        options: { responsive:true, scales:{ x:{ stacked:true }, y:{ stacked:true, beginAtZero:true } }, plugins:{ title:{ display:true, text:'Expenses Totals Composition' }, legend:{ position:'top' } } }
      });
    }
    if (!pieChartRef.current){
      pieChartRef.current = new Chart(pieRef.current.getContext('2d'), {
        type: 'pie',
        data: { labels: [], datasets:[{ data: [], backgroundColor: [], hoverOffset: 8 }] },
        options: { responsive:true, plugins:{ title:{ display:true, text:'Expenses by Category' }, legend:{ position:'right' } } }
      });
    }

    // update
    const { months, cats, datasets, pieTotals, pieColors } = chartData;
    const b = barChartRef.current; const p = pieChartRef.current;
    if (b){ b.data.labels = months; b.data.datasets = datasets; b.update(); }
    if (p){ p.data.labels = cats; p.data.datasets[0].data = pieTotals; p.data.datasets[0].backgroundColor = pieColors; p.update(); }

    return () => {};
  }, [chartData]);

  async function onSubmitAdd(e){
    e.preventDefault();
    setSavingMsg('');
    const { date, category, amount, comment } = form;
    const amt = parseFloat(amount);
    if (!amt || amt <= 0){ setSavingMsg('Amount must be positive'); return; }
    const [y,m,d] = date.split('-');
    const payload = { date: `${d}/${m}/${y}`, category, amount: amt, comment };
    try {
      await apiPut('/api/expenses/', payload);
      setSavingMsg('Saved!');
      setTimeout(()=> setSavingMsg(''), 1500);
      // naive refresh: reload dataset
      const months = await apiGet('/api/expenses');
      const chunks = await Promise.all(months.map(ms => {
        const [yy, mm] = ms.split('-');
        return apiGet(`/api/expenses/${yy}/${mm}.json`);
      }));
      const flat = chunks.flat();
      for (const e of flat){ const [dd, mm, yy] = e.date.split('/').map(Number); e.dateObj = new Date(yy, mm-1, dd); }
      setAll(flat);
      setForm(f => ({ ...f, amount: '', comment: '' }));
    } catch (err) {
      setSavingMsg('Error: ' + (err.message || err));
    }
  }

  if (loading) return <p id="loading-indicator">Loading…</p>;
  if (error)   return <p style={{color:'var(--magenta)'}}>Couldn’t load expenses: {error}</p>;

  return (
    <section className="expenses-page">
      <h1>Expenses</h1>

      <div id="date-filter">
        <label>From <input type="date" value={fromDate} onChange={e=>setFromDate(e.target.value)} /></label>
        <label>To <input type="date" value={toDate} onChange={e=>setToDate(e.target.value)} /></label>
        <button onClick={()=>{ setFromDate(''); setToDate(''); }}>Reset</button>
      </div>

      <div className="charts-row">
        <div className="chart-wrapper"><canvas ref={barRef} id="monthly-chart" /></div>
        <div className="chart-wrapper"><canvas ref={pieRef} id="category-chart" /></div>
      </div>

      <h2 className="expenses-table-title">Latest</h2>
      <div id="expenses-table-wrapper">
        <table id="expenses-table">
          <thead>
            <tr><th>Date</th><th>Category</th><th>Amount</th><th>Comment</th></tr>
          </thead>
          <tbody>
            {filtered.slice().sort((a,b)=>b.dateObj-a.dateObj).map((e,idx)=> (
              <tr key={idx}>
                <td>{e.date}</td>
                <td>{e.category}</td>
                <td>€{e.amount.toFixed(2)}</td>
                <td>{e.comment || ''}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div id="fab-form-container">
        <button id="fab-add-expense" onClick={()=>{
          const el = document.getElementById('add-expense-form');
          if (el) el.style.display = (el.style.display !== 'flex') ? 'flex' : 'none';
        }}>＋</button>

        <form id="add-expense-form" onSubmit={onSubmitAdd} style={{display:'none'}}>
          <div>
            <label>Date<br/>
              <input type="date" value={form.date} onChange={e=>setForm(f=>({...f, date:e.target.value}))} />
            </label>
          </div>
          <div>
            <label>Category<br/>
              <select value={form.category} onChange={e=>setForm(f=>({...f, category:e.target.value}))}>
                <option value="">Select…</option>
                {categories.map(c => <option key={c} value={c}>{c[0].toUpperCase()+c.slice(1)}</option>)}
              </select>
            </label>
          </div>
          <div>
            <label>Amount<br/>
              <input type="number" step="0.01" value={form.amount} onChange={e=>setForm(f=>({...f, amount:e.target.value}))} />
            </label>
          </div>
          <div>
            <label>Comment<br/>
              <input type="text" value={form.comment} onChange={e=>setForm(f=>({...f, comment:e.target.value}))} />
            </label>
          </div>
          <button type="submit">Save</button>
          <span id="expense-form-error" style={{marginLeft:'1rem'}}>{savingMsg}</span>
        </form>
      </div>
    </section>
  );
}