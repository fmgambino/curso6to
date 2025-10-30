/****************************************************
 * ESP32 + WiFiManager + DHT22 + Temp Interna + Telegram
 * + Persistencia SPIFFS (config.json) + Estabilidad
 *
 * Comandos:
 *  /menu
 *  /DataSensores
 *  /setInterval [seg]        (mín 2s por DHT22)
 *  /setModo [auto|manual]
 *  /modo
 *  /status                   (incluye tiempo restante)
 *  /APreset                  (borra WiFi; reinicio según ENABLE_SOFT_RESET)
 *  /reset                    (reinicia según ENABLE_SOFT_RESET)
 *  /clearResetCount
 *  /infoDevices
 ****************************************************/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WiFiManager.h>       // https://github.com/tzapu/WiFiManager
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include "esp_system.h"
#include "esp_wifi.h"

#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ===== Configuración del BOT y Canal =====
#define BOT_TOKEN        "8385145731:AAFo0sg1qxpHpMIerwlrOaTwCBf1SQ-g2S0"
#define CHANNEL_CHAT_ID  "@sextoTobar"

// ===== WiFiManager =====
#define WM_AP_PASSWORD   "12345678"   // "" si querés portal abierto (mín 8 chars si usás password)
#define WM_CP_TIMEOUT_S  180          // Timeout del portal (seg)

// ===== DHT22 =====
#define DHTPIN   4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===== Control: habilitar/deshabilitar reinicio por software =====
#define ENABLE_SOFT_RESET 0   // 0 = deshabilitado temporalmente | 1 = habilitado

// ===== Objetos globales =====
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiManager wm;

// ===== Estado / persistencia =====
unsigned long previousMillis = 0;     // último envío
long interval = 10000;                // ms (persistente)
bool autoSend = true;                 // modo auto/manual (persistente)
int resetCount = 0;                   // contador reinicios (persistente)

// ===== Antibloqueos / redes =====
unsigned long lastBotPoll = 0;
const unsigned long botPollIntervalMs = 3000;  // no consultar más seguido que esto
const int telegramLongPollSec = 10;            // long poll interno
const uint16_t tlsTimeoutMs = 12000;           // timeout TLS

// ===== Sensor interno de temperatura (NO calibrado) =====
extern "C" uint8_t temprature_sens_read();
float getInternalTempESP32() {
  // Aproximado: NO es la temperatura real de CPU, no está calibrado
  return ((temprature_sens_read() - 32) / 1.8);
}

// ===== Utilidades =====
String getResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:     return "🔌 Power On";
    case ESP_RST_EXT:         return "🔁 Pin externo";
    case ESP_RST_SW:          return "💻 Software (ESP.restart)";
    case ESP_RST_PANIC:       return "⚠️ Excepción (Crash)";
    case ESP_RST_INT_WDT:     return "⏱️ Watchdog (Interrupción)";
    case ESP_RST_TASK_WDT:    return "⏱️ Watchdog (Tarea bloqueada)";
    case ESP_RST_WDT:         return "⏱️ Watchdog general";
    case ESP_RST_DEEPSLEEP:   return "🌙 Deep Sleep";
    case ESP_RST_BROWNOUT:    return "⚡ Brownout";
    case ESP_RST_SDIO:        return "🔁 SDIO";
    default:                  return "❓ Desconocido";
  }
}

String fmtHMS(unsigned long ms) {
  unsigned long s = ms / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long ss = s % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, ss);
  return String(buf);
}

unsigned long remainingForNextSend() {
  if (!autoSend) return 0;
  unsigned long now = millis();
  unsigned long elapsed = now - previousMillis;           // overflow-safe
  if (elapsed >= (unsigned long)interval) return 0;
  return (unsigned long)interval - elapsed;
}

// ===== SPIFFS: carga/guarda config =====
const char* CFG_PATH = "/config.json";

bool saveConfig() {
  StaticJsonDocument<256> doc;
  doc["interval_ms"] = interval;
  doc["auto_send"]   = autoSend;
  doc["reset_count"] = resetCount;

  File f = SPIFFS.open(CFG_PATH, FILE_WRITE);
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}

bool loadConfig() {
  if (!SPIFFS.exists(CFG_PATH)) return false;
  File f = SPIFFS.open(CFG_PATH, FILE_READ);
  if (!f) return false;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  if (doc.containsKey("interval_ms")) interval   = doc["interval_ms"].as<long>();
  if (doc.containsKey("auto_send"))   autoSend   = doc["auto_send"].as<bool>();
  if (doc.containsKey("reset_count")) resetCount = doc["reset_count"].as<int>();
  return true;
}

// ===== AP dinámico "ESP32-XXXX" =====
void makeDynamicApName(char* outName, size_t outLen) {
  String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 4);
  String ap = "ESP32-" + suffix;
  ap.toCharArray(outName, outLen);
}

// ===== Conexión WiFi (WiFiManager) =====
bool connectWithWiFiManager() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  wm.setConfigPortalTimeout(WM_CP_TIMEOUT_S);

  char apName[24];
  makeDynamicApName(apName, sizeof(apName));
  Serial.printf("📡 Portal WiFiManager AP: %s\n", apName);

  bool ok = wm.autoConnect(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);
  if (!ok) {
    Serial.println("⚠️ autoConnect falló. Iniciando portal manual...");
    ok = wm.startConfigPortal(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);
  }
  if (ok) Serial.printf("✅ Conectado a SSID: %s\n", WiFi.SSID().c_str());
  else    Serial.println("❌ No se pudo conectar a WiFi.");
  return ok;
}

// ===== Telemetría (DHT22 + Temp Interna + 'aire' simulado) =====
void sendSensorData() {
  float t = dht.readTemperature();       // °C
  float h = dht.readHumidity();          // %
  float tempCPU = getInternalTempESP32();// °C aprox (no calibrado)

  if (isnan(t) || isnan(h)) {
    bot.sendMessage(CHANNEL_CHAT_ID, "⚠️ Error leyendo DHT22 (temp/hum). Revisar cableado y pull-up 10k.", "");
    return;
  }

  int  calidadAire        = random(100, 500); // simulado
  bool generadorEncendido = random(0, 2);     // simulado
  String estadoGenerador  = generadorEncendido ? "Encendido 🔌" : "Apagado ❌";

  String message = "📡 *Datos Sistema IoT (Reales DHT22 + Temp Interna):*\n";
  message += "🌡️ Temp Ambiente: *" + String(t, 1) + " °C*\n";
  message += "💧 Humedad: *"      + String(h, 1) + " %*\n";
  message += "🔥 Temp CPU: *"     + String(tempCPU, 1) + " °C* _(sensor interno no calibrado)_\n";
  message += "🧪 Calidad del Aire: *" + String(calidadAire) + " ppm* _(simulado)_\n";
  message += "⚙️ Generador: *"    + estadoGenerador + "*\n";
  message += "🕒 Próximo envío (auto): *" + (autoSend ? fmtHMS(remainingForNextSend()) : String("N/A (manual)")) + "*\n";
  message += "\n👨‍💻 _Dev. for: Ing. Gambino_";

  bot.sendMessage(CHANNEL_CHAT_ID, message, "Markdown");

  // Reinicia la ventana del próximo envío desde este punto
  previousMillis = millis();
}

// ===== Telegram: manejo de mensajes =====
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text    = bot.messages[i].text;
    String chat_id = bot.messages[i].chat_id;

    // Si es vacío o no es comando → sugerimos el menú
    if (text.length() == 0 || !text.startsWith("/")) {
      if (chat_id.length() > 0) {
        bot.sendMessage(chat_id, "ℹ️ Mensaje recibido.\nUsá */menu* para ver los comandos disponibles.", "Markdown");
      }
      continue;
    }

    // ---- Comandos ----
    if (text == "/menu") {
      String reply = "📋 *Menú de Comandos:*\n\n";
      reply += "📊 /DataSensores - Enviar datos actuales al canal\n";
      reply += "🧹 /APreset - Borrar WiFi y reiniciar\n";
      reply += "⏱️ /setInterval [seg] - Cambiar intervalo (≥2s)\n";
      reply += "🔀 /setModo [auto|manual] - Seleccionar modo de envío\n";
      reply += "🔎 /modo - Mostrar modo actual (auto/manual)\n";
      reply += "📈 /status - Estado general (incluye tiempo restante)\n";
      reply += "🔁 /reset - Reiniciar ESP32\n";
      reply += "🖥️ /infoDevices - Info del dispositivo\n";
      reply += "♻️ /clearResetCount - Resetear contador";
      bot.sendMessage(chat_id, reply, "Markdown");

    } else if (text == "/DataSensores") {
      if (WiFi.status() == WL_CONNECTED) sendSensorData();

    } else if (text.startsWith("/setInterval ")) {
      String arg = text.substring(13);
      long newInterval = arg.toInt() * 1000L;
      if (newInterval >= 2000) { // DHT22 mínimo 2s
        interval = newInterval;
        saveConfig();
        bot.sendMessage(chat_id, "⏱️ Intervalo cambiado a *" + String(newInterval / 1000) + "* segundos.", "Markdown");
        previousMillis = millis(); // arranca nueva ventana
      } else {
        bot.sendMessage(chat_id, "⚠️ Intervalo inválido. Debe ser ≥ 2 segundos para DHT22.", "Markdown");
      }

    } else if (text.startsWith("/setModo ")) {
      String arg = text.substring(9); arg.toLowerCase();
      if (arg == "auto") {
        autoSend = true;  saveConfig(); previousMillis = millis();
        bot.sendMessage(chat_id, "🔀 Modo de envío: *AUTO* (envío periódico activado).", "Markdown");
      } else if (arg == "manual") {
        autoSend = false; saveConfig();
        bot.sendMessage(chat_id, "🔀 Modo de envío: *MANUAL* (solo con /DataSensores).", "Markdown");
      } else {
        bot.sendMessage(chat_id, "⚠️ Valor inválido. Usá: */setModo auto* o */setModo manual*.", "Markdown");
      }

    } else if (text == "/modo") {
      bot.sendMessage(chat_id, String("🔎 Modo actual: *") + (autoSend ? "AUTO" : "MANUAL") + "*.", "Markdown");

    } else if (text == "/APreset") {
      bot.sendMessage(chat_id, "🧹 Borrando credenciales WiFi...", "Markdown");
      wm.resetSettings();
      #if ENABLE_SOFT_RESET
        bot.sendMessage(chat_id, "🔁 Reiniciando ESP32...", "Markdown");
        delay(200);
        ESP.restart();
      #else
        bot.sendMessage(chat_id, "⛔ Reinicio por software *deshabilitado temporalmente*.\n"
                                 "Reiniciá manualmente para aplicar los cambios.", "Markdown");
      #endif

    } else if (text == "/status") {
      unsigned long rem = remainingForNextSend();
      String message = "📈 *Estado General:*\n";
      message += "🔁 Reinicios (persistente): *" + String(resetCount) + "*\n";
      message += "⏱️ Intervalo: *" + String(interval / 1000) + " s*\n";
      message += "🕹️ Modo: *" + String(autoSend ? "AUTO" : "MANUAL") + "*\n";
      message += "⏳ Próximo envío: *" + (autoSend ? fmtHMS(rem) : String("N/A (manual)")) + "*\n";
      message += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "Conectado ✅" : "Desconectado ❌") + "*\n";
      message += "🌐 SSID: *" + WiFi.SSID() + "*\n";
      message += "📝 Último reinicio: *" + getResetReason() + "*\n";
      message += "🧠 Heap libre: *" + String(ESP.getFreeHeap()) + " B* (min: " + String(ESP.getMinFreeHeap()) + " B)";
      bot.sendMessage(chat_id, message, "Markdown");

    } else if (text == "/reset") {
      #if ENABLE_SOFT_RESET
        bot.sendMessage(chat_id, "🔁 Reiniciando ESP32...", "Markdown");
        delay(300);
        ESP.restart();
      #else
        bot.sendMessage(chat_id, "⛔ Reinicio por software *deshabilitado temporalmente*.", "Markdown");
      #endif

    } else if (text == "/clearResetCount") {
      resetCount = 0; saveConfig();
      bot.sendMessage(chat_id, "♻️ *Contador de reinicios reiniciado.*", "Markdown");

    } else if (text == "/infoDevices") {
      float tempCPU = getInternalTempESP32();
      String message = "🖥️ *Info del Dispositivo:*\n";
      message += "🆔 ID: *" + String((uint32_t)ESP.getEfuseMac(), HEX) + "*\n";
      message += "🔗 MAC: *" + WiFi.macAddress() + "*\n";
      message += "🌐 SSID: *" + WiFi.SSID() + "*\n";
      message += "📡 IP: *" + WiFi.localIP().toString() + "*\n";
      message += "📶 RSSI: *" + String(WiFi.RSSI()) + " dBm*\n";
      message += "🌡️ Temp CPU: *" + String(tempCPU, 1) + " °C* _(sensor interno no calibrado)_\n";
      unsigned long rem = remainingForNextSend();
      message += "⏳ Próximo envío: *" + (autoSend ? fmtHMS(rem) : String("N/A (manual)")) + "*\n";
      unsigned long up = millis();
      message += "⏱️ Uptime: *" + fmtHMS(up) + "*";
      bot.sendMessage(chat_id, message, "Markdown");

    } else {
      bot.sendMessage(chat_id, "❌ *Comando no reconocido.*\nℹ️ Usá */menu* para ver los comandos disponibles.", "Markdown");
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BOOT ===");
  Serial.printf("Reset reason: %s\n", getResetReason().c_str());

  // FS
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS no montó. (Se intentó formatear)");
  }

  // Cargar config
  if (!loadConfig()) {
    interval = 10000;
    autoSend = true;
    resetCount = 0;
    saveConfig();
  }

  // Contador de reinicios (persistente)
  resetCount++;
  saveConfig();

  // DHT22
  dht.begin();

  // WiFiManager
  bool wifiOk = connectWithWiFiManager();

  // TLS / Telegram
  secured_client.setInsecure();          // Para producción, usar setCACert() con root CA de Telegram
  secured_client.setTimeout(tlsTimeoutMs);
  bot.longPoll = telegramLongPollSec;

  // Ventana de envío
  previousMillis = millis();
  lastBotPoll = 0;

  // Mensaje de arranque
  if (wifiOk) {
    String bootMsg = "✅ *Sistema Iniciado*\n";
    bootMsg += "📅 Build: " + String(__DATE__) + " " + String(__TIME__) + "\n";
    bootMsg += "📝 Motivo: *" + getResetReason() + "*\n";
    bootMsg += "🔁 Reinicios (persistente): *" + String(resetCount) + "*\n";
    bootMsg += "🕹️ Modo: *" + String(autoSend ? "AUTO" : "MANUAL") + "* | ⏱️ Intervalo: *" + String(interval / 1000) + " s*\n";
    bootMsg += "🌐 SSID: *" + WiFi.SSID() + "*\n";
    bootMsg += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + "*\n";
    bootMsg += "🧠 Heap libre: *" + String(ESP.getFreeHeap()) + " B*"; // 
    bot.sendMessage(CHANNEL_CHAT_ID, bootMsg, "Markdown");
  } else {
    Serial.println("📶 Configurá WiFi desde el portal (AP) para habilitar Telegram.");
  }

  randomSeed(esp_random());
}

// ===== LOOP =====
void loop() {
  unsigned long now = millis();

  // Envío automático
  if (autoSend && (now - previousMillis >= (unsigned long)interval)) {
    if (WiFi.status() == WL_CONNECTED) {
      sendSensorData();
    } else {
      previousMillis = now; // evita saturar si no hay WiFi
    }
  }

  // Polling de Telegram: no más frecuente que botPollIntervalMs
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastBotPoll >= botPollIntervalMs) {
      lastBotPoll = now;
      int numMessages = bot.getUpdates(bot.last_message_received + 1);
      if (numMessages > 0) {
        handleNewMessages(numMessages);
      }
    }
  }

  // Reintento básico de WiFi (cada 30s)
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (now - lastTry > 30000UL) {
      lastTry = now;
      WiFi.reconnect();
    }
  }

  delay(50); // cede tiempo al scheduler; ayuda contra WDT
}
