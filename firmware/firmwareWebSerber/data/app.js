// Tema claro/oscuro con persistencia
(function initTheme() {
    const saved = localStorage.getItem("theme");
    if (saved === "dark" || saved === "light") {
      document.documentElement.setAttribute("data-theme", saved);
    }
  })();
  document.getElementById("themeToggle").addEventListener("click", () => {
    const isDark = document.documentElement.getAttribute("data-theme") === "dark";
    const next = isDark ? "light" : "dark";
    document.documentElement.setAttribute("data-theme", next);
    localStorage.setItem("theme", next);
  });
  
  // Render lecturas actuales (polling)
  const tempEl = document.getElementById("tempValue");
  const humEl = document.getElementById("humValue");
  const humStateEl = document.getElementById("humState");
  const lastUpdateEl = document.getElementById("lastUpdate");
  
  function formatTime(dt) {
    const pad = n => String(n).padStart(2, "0");
    return `${pad(dt.getHours())}:${pad(dt.getMinutes())}:${pad(dt.getSeconds())}`;
  }
  
  async function fetchLatest() {
    try {
      const r = await fetch("/api/latest");
      if (!r.ok) throw new Error("HTTP " + r.status);
      const data = await r.json();
      tempEl.textContent = data.temperature.toFixed(1);
      humEl.textContent = Math.round(data.humidity);
      humStateEl.textContent = data.humidity < 30 ? "Seco" : (data.humidity > 70 ? "Húmedo" : "Confortable");
      lastUpdateEl.textContent = formatTime(new Date(data.timestamp));
    } catch (e) {
      console.error(e);
    }
  }
  fetchLatest();
  setInterval(fetchLatest, 3000);
  
  // Popup con calendario + gráficos
  document.getElementById("openCharts").addEventListener("click", async () => {
    const today = new Date();
    const yyyy = today.getFullYear();
    const mm = String(today.getMonth() + 1).padStart(2, "0");
    const dd = String(today.getDate()).padStart(2, "0");
    const defaultDate = `${yyyy}-${mm}-${dd}`;
  
    await Swal.fire({
      title: "Histórico por fecha",
      showConfirmButton: false,
      showCloseButton: true,
      width: "min(1000px, 92vw)",
      html: `
        <div class="popup-grid">
          <div class="popup-card">
            <label for="datePick" class="muted">Seleccionar fecha</label>
            <input id="datePick" type="date" value="${defaultDate}" style="width:100%;padding:10px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--fg)">
            <p class="muted" style="margin-top:10px">Se muestran valores simulados cada hora.</p>
          </div>
          <div class="popup-row">
            <div class="popup-card"><canvas id="tempChart"></canvas></div>
            <div class="popup-card"><canvas id="humChart"></canvas></div>
          </div>
        </div>
      `,
      didOpen: () => buildCharts(defaultDate)
    });
  });
  
  let tempChart, humChart;
  async function buildCharts(dateStr) {
    const data = await fetchHistory(dateStr);
    const labels = data.hours.map(h => `${String(h).padStart(2, "0")}:00`);
  
    const tempCtx = document.getElementById("tempChart").getContext("2d");
    const humCtx  = document.getElementById("humChart").getContext("2d");
  
    // Destruir si existen
    if (tempChart) tempChart.destroy();
    if (humChart) humChart.destroy();
  
    tempChart = new Chart(tempCtx, {
      type: "line",
      data: { labels, datasets: [{ label: "Temperatura (°C)", data: data.temperature, tension: 0.35 }] },
      options: {
        responsive: true, maintainAspectRatio: false,
        scales: { y: { beginAtZero: false } },
        plugins: { legend: { display: true } }
      }
    });
    humChart = new Chart(humCtx, {
      type: "line",
      data: { labels, datasets: [{ label: "Humedad (%)", data: data.humidity, tension: 0.35 }] },
      options: {
        responsive: true, maintainAspectRatio: false,
        scales: { y: { suggestedMin: 0, suggestedMax: 100 } },
        plugins: { legend: { display: true } }
      }
    });
  
    // Al cambiar fecha -> recargar datos
    const datePick = document.getElementById("datePick");
    datePick.addEventListener("change", async (e) => {
      const newDate = e.target.value;
      const nd = await fetchHistory(newDate);
      const newLabels = nd.hours.map(h => `${String(h).padStart(2, "0")}:00`);
  
      tempChart.data.labels = newLabels;
      tempChart.data.datasets[0].data = nd.temperature;
      tempChart.update();
  
      humChart.data.labels = newLabels;
      humChart.data.datasets[0].data = nd.humidity;
      humChart.update();
    }, { once: true });
  }
  
  // Llama al backend del ESP32
  async function fetchHistory(dateStr) {
    const r = await fetch(`/api/history?date=${encodeURIComponent(dateStr)}`);
    if (!r.ok) throw new Error("HTTP " + r.status);
    return await r.json();
  }
  