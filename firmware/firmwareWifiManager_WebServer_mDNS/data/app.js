// ================================
// Tema claro/oscuro con persistencia
// ================================
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

// ================================
// Elementos de UI
// ================================
const tempEl = document.getElementById("tempValue");
const humEl = document.getElementById("humValue");
const humStateEl = document.getElementById("humState");
const lastUpdateEl = document.getElementById("lastUpdate");

// ================================
// Utilidades
// ================================
function formatTime(dt) {
  const pad = n => String(n).padStart(2, "0");
  return `${pad(dt.getHours())}:${pad(dt.getMinutes())}:${pad(dt.getSeconds())}`;
}

// ================================
// Simulación DHT22
// ================================
function fakeLatest() {
  const temp = 18 + Math.random() * 12; // 18°C - 30°C
  const hum = 20 + Math.random() * 60;  // 20% - 80%
  return {
    temperature: temp,
    humidity: hum,
    timestamp: Date.now()
  };
}

async function fetchLatest() {
  try {
    const data = fakeLatest();

    // Mostrar en UI
    if (tempEl) tempEl.textContent = data.temperature.toFixed(1);
    if (humEl) humEl.textContent = Math.round(data.humidity);
    if (humStateEl) {
      humStateEl.textContent =
        data.humidity < 30 ? "Seco" :
        (data.humidity > 70 ? "Húmedo" : "Confortable");
    }
    if (lastUpdateEl) lastUpdateEl.textContent = formatTime(new Date(data.timestamp));

    // Guardar en localStorage
    const today = new Date().toISOString().slice(0, 10); // YYYY-MM-DD
    const stored = JSON.parse(localStorage.getItem("history") || "{}");
    if (!stored[today]) {
      stored[today] = { timestamps: [], temperature: [], humidity: [] };
    }
    stored[today].timestamps.push(data.timestamp);
    stored[today].temperature.push(data.temperature);
    stored[today].humidity.push(data.humidity);

    localStorage.setItem("history", JSON.stringify(stored));

    // 🔎 Log de depuración
    console.log("Lectura guardada:", data);
    console.log("LocalStorage ahora:", stored);
  } catch (e) {
    console.error("Error en fetchLatest:", e);
  }
}

// Primera lectura inmediata + luego cada 3s
fetchLatest();
setInterval(fetchLatest, 3000);

// ================================
// Popup con SweetAlert2 y gráficas
// ================================
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
          <p class="muted" style="margin-top:10px">Se muestran las lecturas simuladas guardadas en localStorage.</p>
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

// ================================
// Construcción de las gráficas
// ================================
let tempChart, humChart;
async function buildCharts(dateStr) {
  const data = await fetchHistory(dateStr);
  if (!data || data.timestamps.length === 0) {
    await Swal.fire("Sin datos", "No hay datos simulados guardados para esta fecha.", "info");
    return;
  }

  const labels = data.timestamps.map(ts => formatTime(new Date(ts)));
  const tempCtx = document.getElementById("tempChart").getContext("2d");
  const humCtx = document.getElementById("humChart").getContext("2d");

  if (tempChart) tempChart.destroy();
  if (humChart) humChart.destroy();

  tempChart = new Chart(tempCtx, {
    type: "line",
    data: { labels, datasets: [{ label: "Temperatura (°C)", data: data.temperature, tension: 0.35 }] },
    options: { responsive: true, maintainAspectRatio: false }
  });

  humChart = new Chart(humCtx, {
    type: "line",
    data: { labels, datasets: [{ label: "Humedad (%)", data: data.humidity, tension: 0.35 }] },
    options: { responsive: true, maintainAspectRatio: false }
  });

  // Cambio de fecha
  document.getElementById("datePick").addEventListener("change", async (e) => {
    const newDate = e.target.value;
    const nd = await fetchHistory(newDate);
    if (!nd || nd.timestamps.length === 0) {
      await Swal.fire("Sin datos", "No hay datos simulados guardados para esta fecha.", "info");
      return;
    }
    const newLabels = nd.timestamps.map(ts => formatTime(new Date(ts)));

    tempChart.data.labels = newLabels;
    tempChart.data.datasets[0].data = nd.temperature;
    tempChart.update();

    humChart.data.labels = newLabels;
    humChart.data.datasets[0].data = nd.humidity;
    humChart.update();
  }, { once: true });
}

// ================================
// Recuperar datos de localStorage
// ================================
async function fetchHistory(dateStr) {
  const stored = JSON.parse(localStorage.getItem("history") || "{}");
  return stored[dateStr] || null;
}
