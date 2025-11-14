# Expenses Dashboard Documentation

## Table of Contents

1. [Introduction](#introduction)
2. [Features Overview](#features-overview)
3. [Architecture & Data Flow](#architecture--data-flow)

   * [Mermaid Flowchart](#mermaid-flowchart)
4. [Implementation Details](#implementation-details)

   1. [Data Loading](#1-data-loading)
   2. [Date Filtering](#2-date-filtering)
   3. [Chart Rendering](#3-chart-rendering)
   4. [Table Rendering](#4-table-rendering)
   5. [Dynamic Updates](#5-dynamic-updates)
5. [Usage Example](#usage-example)
6. [Conclusion](#conclusion)

---

## Introduction

This document provides a comprehensive overview of the **Expenses Dashboard** implementation. It covers key features such as data ingestion, dynamic date filtering, interactive chart rendering, and tabular display. The goal is to equip developers and stakeholders with a clear understanding of the system’s architecture and code structure.

## Features Overview

* **Flexible Date Range Filtering**: Users can specify `from` and `to` dates to narrow down which expenses are visualized.
* **Stacked Bar Chart**: Monthly totals are displayed per category, leveraging [Chart.js](https://www.chartjs.org/) for responsive, interactive visualizations.
* **Pie Chart**: Category distribution over the selected period.
* **Tabular View**: A detailed list of individual expense records.
* **Real-Time Updates**: Changing the date range triggers instant re-computation and re-rendering of charts and tables.

## Architecture & Data Flow

The following flowchart illustrates the end-to-end data pipeline from API endpoints to UI components.

### Mermaid Flowchart

```mermaid
flowchart TD
  A[Start] --> B[Fetch `/api/expenses/months`]
  B --> C{Unique Months?}
  C -->|Yes| D[Fetch each `/expenses/YYYY/MM.json`]
  C -->|No| C
  D --> E[Flatten data into `allExpenses[]`]
  E --> F[Attach `Date` objects]
  F --> G[Initialize Charts]
  G --> H[Render with full dataset]
  H --> I[User changes date inputs]
  I --> J[Filter `allExpenses[]` by date]
  J --> K[Re-render Charts & Table]
  K --> H
```

## Implementation Details

### 1. Data Loading

* **Endpoint**: `/api/expenses/months`

  * Returns an array of strings: `['2024-02', '2025-01', ...]`.
* **Per-Month JSON**: `/expenses/YYYY/MM.json`

  * Each file contains an array of expense objects:

    ```json
    {
      "date": "DD/MM/YYYY",
      "category": "rent",
      "amount": 850.00,
      "comment": ""
    }
    ```
* **Deduplication & Sorting**:

  ```js
  const rawMonths = await fetch(...).then(r => r.json());
  const months = [...new Set(rawMonths)]
    .sort((a, b) => new Date(a + '-01') - new Date(b + '-01'));
  ```

### 2. Date Filtering

* **HTML Controls**:

  ```html
  <input id="from-date" type="date">
  <input id="to-date"   type="date">
  ```
* **Local-Midnight Parsing**: Convert input `.value` (`YYYY-MM-DD`) into a local JS `Date` at `00:00` or `23:59:59.999` for an inclusive `to` bound.

  ```js
  const [y,m,d] = fromInput.value.split('-').map(Number);
  const from = new Date(y, m-1, d, 0,0,0,0);
  const to   = new Date(y, m-1, d,23,59,59,999);
  ```
* **Filtering Logic**:

  ```js
  const filtered = allExpenses.filter(e => {
    const t = e.dateObj.getTime();
    return (!from || t >= from.getTime()) && (!to || t <= to.getTime());
  });
  ```

### 3. Chart Rendering

* **Stacked Bar Chart**:

  * **Data structure**: one dataset per category, with monthly sums.
  * **Initialization**:

    ```js
    window.barChart = new Chart(ctx, {
      type: 'bar', data: { labels: [], datasets: [] },
      options: { scales:{ x:{stacked:true}, y:{stacked:true} } }
    });
    ```
  * **Update**:

    ```js
    barChart.data.labels   = monthLabels;
    barChart.data.datasets = barDatasets;
    barChart.update();
    ```

* **Pie Chart**:

  * **Data structure**: single dataset with sums per category.
  * **Initialization**:

    ```js
    window.pieChart = new Chart(ctxPie, {
      type: 'pie', data:{ labels:[], datasets:[{data:[], backgroundColor:[]}]}
    });
    ```
  * **Update**:

    ```js
    pieChart.data.labels                 = pieLabels;
    pieChart.data.datasets[0].data       = pieData;
    pieChart.data.datasets[0].backgroundColor = pieColors;
    pieChart.update();
    ```

### 4. Table Rendering

* **Clearing & Rebuilding**:

  ```js
  const tbody = document.querySelector('#expenses-table tbody');
  tbody.innerHTML = '';
  filtered.forEach(e => {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${e.date}</td>
      <td>${e.category}</td>
      <td>€${e.amount.toFixed(2)}</td>
      <td>${e.comment}</td>
    `;
    tbody.appendChild(tr);
  });
  ```

### 5. Dynamic Updates

* **Event Listeners**:

  ```js
  fromInput.addEventListener('change', onFilterChange);
  toInput.addEventListener(  'change', onFilterChange);
  ```
* **`onFilterChange()`** orchestrates:

  1. Re-parse `from` / `to` as local-midnight dates
  2. Filter `allExpenses`
  3. Call `renderExpenses(filtered)`

## Usage Example

1. Open the page.  By default you see **all expenses** plotted and listed.
2. Click the **From** date-picker and choose `2025-01-01`.
3. Click the **To** date-picker and choose `2025-01-31`.
4. The bar chart and pie chart instantly update to show only January 2025 data, and the table lists just that slice.

## Conclusion

This modular approach cleanly separates data fetching, transformation, rendering, and user interaction. Adding new filters (e.g. by category, amount range, or keyword search) follows the same pattern:

1. Update the filter UI and parsing logic
2. Re-filter `allExpenses`
3. Re-render charts & table

Feel free to extend, refactor, or integrate with frameworks (React, Vue) by moving `renderExpenses` into a reactive component. The underlying data-flow remains the same.
