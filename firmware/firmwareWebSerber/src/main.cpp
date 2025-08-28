#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <time.h>

// ======== CONFIG WIFI ========
const char* WIFI_SSID = "electronicagambino.com";
const char* WIFI_PASS = "tesla0381";

// ======== SERVIDOR ========
WebServer server(80);

// ======== UTIL FECHA/HORA (Argentina) ========
static const long  gmtOffset_sec = -3 * 3600; // UTC-3
static const int   daylightOffset_sec = 0;    // sin DST
static const char* ntpServer = "pool.ntp.org";

// ======== HELPERS ========
String contentType(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  return "text/plain; charset=utf-8";
}

bool serveFile(const String &path) {
  File file = SPIFFS.open(path, "r");
  if (!file) return false;
  server.streamFile(file, contentType(path));
  file.close();
  return true;
}

// Pseudo-rand determinístico simple a partir de string (fecha)
uint32_t hashDate(const String& s) {
  uint32_t h = 2166136261u;
  for (size_t i=0; i<s.length(); ++i) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

// Simulación base senoidal + ruido
float simTemp(float hour, uint32_t seed) {
  float base = 24.0 + 6.0 * sinf((hour/24.0f) * 2.0f * PI); // 24±6°C
  // ruido determinístico
  randomSeed(seed + (uint32_t)hour);
  base += (random(-50, 51) / 100.0f); // ±0.5°C
  return base;
}
float simHum(float hour, uint32_t seed) {
  float base = 55.0 + 20.0 * sinf(((hour+6)/24.0f) * 2.0f * PI); // 55±20%
  randomSeed(seed + 1000 + (uint32_t)hour);
  base += (random(-100, 101) / 10.0f); // ±10%
  if (base < 15) base = 15;
  if (base > 95) base = 95;
  return base;
}

// ======== RUTAS ========
void handleRoot() {
  if (!serveFile("/index.html")) {
    server.send(500, "text/plain; charset=utf-8", "index.html no encontrado en SPIFFS");
  }
}
void handleStatic() {
  String path = server.uri();
  if (!serveFile(path)) server.send(404, "text/plain; charset=utf-8", "Archivo no encontrado");
}

// /api/latest -> devuelve lectura "actual"
void handleLatest() {
  // hora actual
  time_t now; time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  float hour = timeinfo.tm_hour + (timeinfo.tm_min/60.0f);
  uint32_t seed = (uint32_t)now;  // cambia con el tiempo

  float t = simTemp(hour, seed);
  float h = simHum(hour, seed);

  // timestamp JS (ms)
  uint64_t ms = ((uint64_t)now) * 1000ULL;

  String json = "{";
  json += "\"temperature\":" + String(t, 1) + ",";
  json += "\"humidity\":" + String(h, 0) + ",";
  json += "\"timestamp\":" + String(ms);
  json += "}";

  server.send(200, "application/json; charset=utf-8", json);
}

// /api/history?date=YYYY-MM-DD -> 24 puntos por hora (sintéticos pero determinísticos por fecha)
void handleHistory() {
  String date = server.hasArg("date") ? server.arg("date") : "";
  if (date.length() != 10) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"Parámetro 'date' inválido. Formato esperado YYYY-MM-DD\"}");
    return;
  }
  uint32_t seed = hashDate(date);

  // construir arrays json
  String hoursArr = "[";
  String tempArr  = "[";
  String humArr   = "[";

  for (int h=0; h<24; ++h) {
    float t = simTemp(h, seed);
    float u = simHum(h, seed);

    hoursArr += String(h);
    tempArr  += String(t, 1);
    humArr   += String(u, 0);

    if (h < 23) { hoursArr += ","; tempArr += ","; humArr += ","; }
  }
  hoursArr += "]"; tempArr += "]"; humArr += "]";

  String json = "{";
  json += "\"date\":\"" + date + "\",";
  json += "\"hours\":" + hoursArr + ",";
  json += "\"temperature\":" + tempArr + ",";
  json += "\"humidity\":" + humArr;
  json += "}";

  server.send(200, "application/json; charset=utf-8", json);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // FS
  if (!SPIFFS.begin(true)) {
    Serial.println("¡Error montando SPIFFS!");
  } else {
    Serial.println("SPIFFS montado");
  }

  // WIFI
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Rutas
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/styles.css", HTTP_GET, handleStatic);
  server.on("/app.js", HTTP_GET, handleStatic);
  server.on("/api/latest", HTTP_GET, handleLatest);
  server.on("/api/history", HTTP_GET, handleHistory);

  // 404 por defecto: intenta servir archivo
  server.onNotFound([](){
    String path = server.uri();
    if (!serveFile(path)) server.send(404, "text/plain; charset=utf-8", "Recurso no encontrado");
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void loop() {
  server.handleClient();
}
