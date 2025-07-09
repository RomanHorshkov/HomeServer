// views/expenses.js

export async function loadExpenses(container) {
  // 1. Inject Chart.js if not already loaded
  if (!window.Chart) {
    await new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = 'https://cdn.jsdelivr.net/npm/chart.js';
      script.onload = resolve;
      script.onerror = reject;
      document.head.appendChild(script);
    });
  }

  // 2. Dynamically load robust, themed expenses CSS
  if (!document.getElementById('expenses-style-link')) {
    const link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = '/assets/expenses_style.css';
    link.id = 'expenses-style-link';
    document.head.appendChild(link);
  }

  // 3. Fetch and inject main content (HTML)
  container.className = 'centered expenses-page';
  const html = await fetch('/views/expenses.html').then(res => {
    if (!res.ok) throw new Error(`Failed to fetch expenses.html: ${res.status}`);
    return res.text();
  });
  container.innerHTML = html;

  // 4. Script logic (charts, fetch, filter, form, etc.)
  const loading = container.querySelector('#loading-indicator');
  loading.style.display = '';
  let all = [];
  let barChart, pieChart;

  try {
    // LOAD: fetch the list of months, then each month’s JSON
    const months = await fetch('/api/expenses/')
      .then(r => { if (!r.ok) throw new Error('months list fetch failed'); return r.json(); });

    const perMonth = await Promise.all(
      months.map(ms => {
        const [year, month] = ms.split('-');
        return fetch(`/api/expenses/${year}/${month}.json`)
          .then(r => { if (!r.ok) throw new Error(`failed ${ms}`); return r.json(); });
      })
    );
    all = perMonth.flat();

    // PREPROCESS: convert string dates → JS Date objects
    all.forEach(e => {
      const [d, m, y] = e.date.split('/').map(Number);
      e.dateObj = new Date(y, m - 1, d);
    });

    // FILTER HANDLERS
    const fromInput = container.querySelector('#from-date');
    const toInput   = container.querySelector('#to-date');
    const resetBtn  = container.querySelector('#reset-filters');

    // RENDER FUNCTION
    function renderExpenses(expenses) {
      const sorted = expenses.slice().sort((a, b) => b.dateObj - a.dateObj);

      // A) MONTHLY CHART
      const monthMap = {};
      sorted.forEach(e => {
        const key = e.dateObj.getFullYear() + '-' +
                    String(e.dateObj.getMonth()+1).padStart(2,'0');
        (monthMap[key] ||= []).push(e);
      });
      const filteredMonths = Object.keys(monthMap).sort();
      const categories = new Set();
      filteredMonths.forEach(mo => {
        monthMap[mo].forEach(e => categories.add(e.category));
      });
      const catList = Array.from(categories);
      const barDatasets = catList.map((cat, idx) => {
        const data = filteredMonths.map(mo =>
          monthMap[mo]
            .filter(e => e.category === cat)
            .reduce((sum, e) => sum + e.amount, 0)
        );
        const hue = (idx * 70) % 360;
        return {
          label: cat,
          data,
          backgroundColor: `hsl(${hue},70%,60%)`,
          borderColor:   '#111',
          borderWidth:   1,
          hoverBorderColor: '#fff'
        };
      });

      barChart.data.labels   = filteredMonths;
      barChart.data.datasets = barDatasets;
      barChart.update();

      // B) PIE CHART
      const totalsByCat = {};
      sorted.forEach(e => {
        totalsByCat[e.category] = (totalsByCat[e.category] || 0) + e.amount;
      });
      const pieLabels = Object.keys(totalsByCat);
      const pieData   = pieLabels.map(l => totalsByCat[l]);
      const pieColors = pieLabels.map((_, i) => {
        const hue = (i * 60) % 360;
        return `hsl(${hue},70%,60%)`;
      });

      pieChart.data.labels = pieLabels;
      pieChart.data.datasets[0].data            = pieData;
      pieChart.data.datasets[0].backgroundColor = pieColors;
      pieChart.update();

      // C) TABLE
      let table = container.querySelector('#expenses-table');
      const tbody = table.querySelector('tbody');
      tbody.innerHTML = '';
      sorted.forEach(e => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td>${e.date}</td>
          <td>${e.category}</td>
          <td>€${e.amount.toFixed(2)}</td>
          <td>${e.comment || ''}</td>
        `;
        tbody.appendChild(tr);
      });
    }

    // INSTANTIATE CHARTS (once)
    const ctxStacked = container.querySelector('#monthly-chart').getContext('2d');
    barChart = new Chart(ctxStacked, {
      type: 'bar',
      data: { labels: [], datasets: [] },
      options: {
        responsive: true,
        scales: { x:{ stacked:true }, y:{ stacked:true, beginAtZero:true } },
        plugins: {
          title: { display:true, text:'Expenses Totals Composition' },
          legend: { position:'top' }
        }
      }
    });

    const ctxPie = container.querySelector('#category-chart').getContext('2d');
    pieChart = new Chart(ctxPie, {
      type: 'pie',
      data: {
        labels: [],
        datasets: [{ data: [], backgroundColor: [], hoverOffset:8 }]
      },
      options: {
        responsive: true,
        plugins: {
          title: { display:true, text:'Expenses by Category' },
          legend: { position:'right' }
        }
      }
    });

    // Date filtering
    function onFilterChange() {
      let from = null, to = null;
      if (fromInput.value) {
        const [y, m, d] = fromInput.value.split('-').map(Number);
        from = new Date(y, m - 1, d, 0, 0, 0, 0);
      }
      if (toInput.value) {
        const [y, m, d] = toInput.value.split('-').map(Number);
        to = new Date(y, m - 1, d, 23, 59, 59, 999);
      }
      const filtered = all.filter(e => {
        const t = e.dateObj.getTime();
        if (from && t < from.getTime()) return false;
        if (to   && t > to.getTime())   return false;
        return true;
      });
      renderExpenses(filtered);
    }

    fromInput.addEventListener('change', onFilterChange);
    toInput.addEventListener  ('change', onFilterChange);
    resetBtn.addEventListener('click', () => {
      fromInput.value = '';
      toInput.value = '';
      renderExpenses(all);
    });

    // INITIAL RENDER with the full dataset
    renderExpenses(all);
    loading.style.display = 'none';

  } catch (err) {
    loading.style.display = 'none';
    console.error('Error loading expenses data:', err);
    container.insertAdjacentHTML(
      'afterbegin',
      `<p style="color:red">Couldn’t load expenses: ${err.message}</p>`
    );
    return;
  }

  // Set default date to today for the add expense form
  const expenseDateInput = container.querySelector('#expense-date');
  if (expenseDateInput) {
    const today = new Date();
    expenseDateInput.value = today.toISOString().slice(0,10);
  }

  // Handle add expense form submission
  const addExpenseForm = container.querySelector('#add-expense-form');
  const formError = container.querySelector('#expense-form-error');
  if (addExpenseForm) {
    addExpenseForm.addEventListener('submit', function(e) {
      e.preventDefault();
      formError.textContent = '';

      const date = expenseDateInput.value;
      const category = container.querySelector('#expense-category').value;
      const amount = parseFloat(container.querySelector('#expense-amount').value);
      const comment = container.querySelector('#expense-comment').value;
      if (!amount || amount <= 0) {
        formError.textContent = 'Amount is required and must be positive.';
        return;
      }
      const [y, m, d] = date.split('-');
      const formattedDate = `${d}/${m}/${y}`;
      const newExpense = {
        date: formattedDate,
        category,
        amount,
        comment
      };
      fetch('/api/expenses/', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newExpense)
      })
      .then(response => {
        if (!response.ok) throw new Error('Failed to save expense');
        formError.style.color = 'green';
        formError.textContent = 'Expense saved!';
        setTimeout(() => { formError.textContent = ''; formError.style.color = 'red'; }, 2000);
        addExpenseForm.reset();
        expenseDateInput.value = new Date().toISOString().slice(0,10);
        // Optionally, reload data to show new expense
        location.reload();
      })
      .catch(err => {
        formError.style.color = 'red';
        formError.textContent = 'Error saving expense: ' + err.message;
      });
    });
  }

  // Dynamically load categories from settings.json
  try {
    const settings = await fetch('/api/expenses/settings.json').then(r => r.json());
    const catSelect = container.querySelector('#expense-category');
    if (settings.categories && Array.isArray(settings.categories)) {
      catSelect.innerHTML = settings.categories.map(cat =>
        `<option value="${cat}">${cat.charAt(0).toUpperCase() + cat.slice(1)}</option>`
      ).join('');
    } else {
      catSelect.innerHTML = '<option value="">No categories</option>';
    }
  } catch (e) {
    const catSelect = container.querySelector('#expense-category');
    catSelect.innerHTML = '<option value="">Error loading categories</option>';
  }

  // Floating Action Button (FAB) toggle and form slide-down
  const fab = container.querySelector('#fab-add-expense');
  const form = container.querySelector('#add-expense-form');
  fab.addEventListener('click', () => {
    const isOpen = form.style.display !== 'flex';
    form.style.display = isOpen ? 'flex' : 'none';
    if (isOpen) {
      setTimeout(() => { expenseDateInput.focus(); }, 200);
    }
  });
}
