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

  // 2. Inject custom CSS (normally from /expenses/expenses_style.css)
  // For simplicity, basic table styling is included inline here. 
  // You can expand or adapt this as needed.
  if (!document.getElementById('expenses-style')) {
    const style = document.createElement('style');
    style.id = 'expenses-style';
    style.textContent = `
      .centered { max-width:900px; margin:0 auto; }
      .charts-row { display:flex; flex-wrap:wrap; gap:2rem; justify-content:center; }
      .chart-wrapper { flex:1 1 350px; min-width:280px; }
      .expenses-table { width:100%; border-collapse:collapse; margin-top:2rem; }
      .expenses-table th, .expenses-table td { border:1px solid #ccc; padding:0.5rem 1rem; text-align:left; }
      .expenses-table-title { margin-top:2rem; }
      #fab-add-expense { font-size:2rem; width:2.7em; height:2.7em; border-radius:50%; background:#0cf; color:#fff; border:none; cursor:pointer; }
      #fab-add-expense:active { background:#0ae; }
      #add-expense-form input, #add-expense-form select { min-width:100px; }
    `;
    document.head.appendChild(style);
  }

  // 3. Inject main content (HTML)
  container.className = 'centered';
  container.innerHTML = `
    <h1 id="expenses-title" style="text-align:center;margin:2.5rem 0 2.5rem 0;">Expenses Overview</h1>
    <fieldset id="date-filter" style="margin-bottom:2.5rem;">
      <legend style="font-size:1rem; font-weight:600;">Filter by date</legend>
      <label for="from-date">From:
        <input id="from-date" name="from-date" type="date" autocomplete="off">
      </label>
      <label for="to-date" style="margin-left:1rem;">To:
        <input id="to-date" name="to-date" type="date" autocomplete="off">
      </label>
      <button id="reset-filters" type="button" style="margin-left:1rem;">Reset Filters</button>
    </fieldset>
    <div class="charts-row" style="margin-bottom:2.5rem;">
      <div class="chart-wrapper">
        <canvas id="monthly-chart" height="400" aria-label="Monthly expenses chart" role="img"></canvas>
      </div>
      <div class="chart-wrapper">
        <canvas id="category-chart" height="400" aria-label="Expenses by category chart" role="img"></canvas>
      </div>
    </div>
    <div id="fab-form-container" style="display:flex;align-items:center;gap:1.5rem;margin-bottom:2.5rem;">
      <button id="fab-add-expense" title="Add Expense" aria-label="Add Expense">+</button>
      <form id="add-expense-form" style="display:none;flex-direction:row;align-items:flex-end;gap:1.5rem;margin:0;">
        <div>
          <label for="expense-date">Date<br>
            <input type="date" id="expense-date" name="date" required>
          </label>
        </div>
        <div>
          <label for="expense-category">Category<br>
            <select id="expense-category" name="category" required>
              <option value="">Loading…</option>
            </select>
          </label>
        </div>
        <div>
          <label for="expense-amount">Amount (€)<br>
            <input type="number" id="expense-amount" name="amount" min="0.01" step="0.01" required>
          </label>
        </div>
        <div>
          <label for="expense-comment">Comment<br>
            <input type="text" id="expense-comment" name="comment" maxlength="100">
          </label>
        </div>
        <button type="submit" style="margin-top:1.2em;">Add Expense</button>
        <span id="expense-form-error" style="color:red; margin-left:1rem;"></span>
      </form>
    </div>
    <div id="loading-indicator" style="text-align:center; margin:2.5rem 0; display:none;">
      <span>Loading expenses data…</span>
    </div>
    <div id="expenses-table-wrapper">
      <h2 class="expenses-table-title">All Expenses</h2>
      <table id="expenses-table" class="expenses-table" aria-label="Expenses table">
        <thead>
          <tr>
            <th>Date</th>
            <th>Category</th>
            <th>Amount</th>
            <th>Comment</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>
    </div>
  `;

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
