/****************************************************
 * ESP32 + WiFiManager + DHT22 + Temp CPU + Telegram
 * Canal: @sextoTobar | Token integrado
 *
 * - Portal cautivo WiFiManager con AP dinámico: ESP32-XXXX
 * - Lecturas REALES de DHT22 (Temp/Humedad)
 * - Temperatura interna del chip (no calibrada) como "Temp CPU"
 * - "Calidad de aire" simulada (puedes reemplazarla luego)
 * - Comandos Telegram: /menu /DataSensores /APreset /setInterval /status /reset /infoDevices /clearResetCount
 *
 * Requiere librerías:
 *  - WiFiManager (tzapu)
 *  - UniversalTelegramBot
 *  - DHT sensor library (Adafruit)
 *  - Adafruit Unified Sensor
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

// ====== CONFIG WiFiManager ======
#define WM_AP_PASSWORD   "12345678"   // Deja "" si querés portal abierto (mín 8 chars si usás password)
#define WM_CP_TIMEOUT_S  180          // Timeout del portal (segundos)

// ====== TELEGRAM ======
#define BOT_TOKEN        "8385145731:AAFo0sg1qxpHpMIerwlrOaTwCBf1SQ-g2S0"
#define CHANNEL_CHAT_ID  "@sextoTobar"

// ====== DHT22 ======
#define DHTPIN   4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// ====== Objetos globales ======
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiManager wm;

// ====== Control ======
long interval = 10000;               // ms entre envíos automáticos
unsigned long previousMillis = 0;
int resetCount = 0;

// ====== Sensor interno de temperatura (NO calibrado) ======
extern "C" uint8_t temprature_sens_read();
float getInternalTempESP32() {
  // Lectura interna aproximada (no es la temperatura real del CPU)
  return ((temprature_sens_read() - 32) / 1.8);
}

// ====== Utilidades ======
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

String fmtUptime() {
  unsigned long s = millis() / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long ss = s % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, ss);
  return String(buf);
}

// Genera el nombre del AP de WiFiManager: "ESP32-XXXX" (últimos 4 dígitos HEX sin ":")
void makeDynamicApName(char* outName, size_t outLen) {
  String mac = WiFi.macAddress();     // p.ej. "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 4);   // "EEFF"
  String ap = "ESP32-" + suffix;                     // "ESP32-EEFF"
  ap.toCharArray(outName, outLen);
}

// ====== Conexión WiFi (WiFiManager) ======
bool connectWithWiFiManager() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  wm.setConfigPortalTimeout(WM_CP_TIMEOUT_S);

  char apName[24];
  makeDynamicApName(apName, sizeof(apName));  // Genera nombre AP dinámico

  Serial.print("📡 Portal WiFiManager AP: ");
  Serial.println(apName);

  // Intenta conectar con credenciales guardadas; si falla, abre portal
  bool ok = wm.autoConnect(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);

  if (!ok) {
    Serial.println("⚠️ autoConnect falló. Iniciando portal manual...");
    ok = wm.startConfigPortal(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);
  }

  if (ok) {
    Serial.print("✅ Conectado a SSID: ");
    Serial.println(WiFi.SSID());
  } else {
    Serial.println("❌ No se pudo conectar a WiFi.");
  }
  return ok;
}

// ====== Telemetría (DHT22 real + Temp interna + Calidad aire simulada) ======
void sendSensorData() {
  // DHT22 necesita ≥2s entre lecturas; interval por defecto = 10s
  float temperatura = dht.readTemperature();     // °C (DHT22)
  float humedad     = dht.readHumidity();        // %  (DHT22)
  float tempCPU     = getInternalTempESP32();    // °C aprox (sensor interno no calibrado)

  if (isnan(temperatura) || isnan(humedad)) {
    bot.sendMessage(CHANNEL_CHAT_ID, "⚠️ Error leyendo DHT22 (temp/hum). Revisar cableado y pull-up 10k.", "");
    return;
  }

  int  calidadAire        = random(100, 500);    // Simulado
  bool generadorEncendido = random(0, 2);        // Simulado
  String estadoGenerador  = generadorEncendido ? "Encendido 🔌" : "Apagado ❌";

  String message = "📡 *Datos Sistema IoT (DHT22 + Temp Interna):*\n";
  message += "🌡️ Temp Ambiente: *" + String(temperatura, 1) + " °C*\n";
  message += "💧 Humedad: *"      + String(humedad, 1)     + " %*\n";
  message += "🔥 Temp CPU: *"     + String(tempCPU, 1)     + " °C* _(sensor interno no calibrado)_\n";
  message += "🧪 Calidad del Aire: *" + String(calidadAire) + " ppm* _(simulado)_\n";
  message += "⚙️ Generador: *"    + estadoGenerador + "*\n";
  message += "\n👨‍💻 _Dev. for: Ing. Gambino_";

  bot.sendMessage(CHANNEL_CHAT_ID, message, "Markdown");
}

// ====== Manejo de comandos Telegram ======
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text    = bot.messages[i].text;
    String chat_id = bot.messages[i].chat_id;

    if (text.length() == 0 || chat_id.length() == 0 || !text.startsWith("/")) continue;

    if (text == "/menu") {
      String reply = "📋 *Menú de Comandos:*\n\n";
      reply += "📊 /DataSensores - Datos actuales\n";
      reply += "🧹 /APreset - Borrar WiFi y reiniciar\n";
      reply += "⏱️ /setInterval [seg] - Cambiar intervalo (≥2s)\n";
      reply += "📈 /status - Estado general\n";
      reply += "🔁 /reset - Reiniciar ESP32\n";
      reply += "🖥️ /infoDevices - Info del dispositivo\n";
      reply += "♻️ /clearResetCount - Resetear contador\n";
      bot.sendMessage(chat_id, reply, "Markdown");

    } else if (text == "/DataSensores") {
      if (WiFi.status() == WL_CONNECTED) sendSensorData();

    } else if (text == "/APreset") {
      // Borra credenciales WiFi guardadas y reinicia (para reabrir portal)
      bot.sendMessage(chat_id, "🧹 Borrando credenciales WiFi y reiniciando...", "Markdown");
      delay(400);
      wm.resetSettings();
      delay(200);
      ESP.restart();

    } else if (text.startsWith("/setInterval ")) {
      // Cambia el intervalo de envío automático (en segundos)
      String arg = text.substring(13);
      int newInterval = arg.toInt() * 1000;
      if (newInterval >= 2000) { // DHT22 mínimo 2s
        interval = newInterval;
        bot.sendMessage(chat_id, "⏱️ Intervalo cambiado a *" + String(newInterval / 1000) + "* segundos.", "Markdown");
      } else {
        bot.sendMessage(chat_id, "⚠️ Intervalo inválido. Debe ser ≥ 2 segundos para DHT22.", "Markdown");
      }

    } else if (text == "/status") {
      // Resumen de estado
      String message = "📈 *Estado General:*\n";
      message += "🔁 Reinicios: *" + String(resetCount) + "*\n";
      message += "⏱️ Intervalo: *" + String(interval / 1000) + " s*\n";
      message += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "Conectado ✅" : "Desconectado ❌") + "*\n";
      message += "🌐 SSID: *" + WiFi.SSID() + "*\n";
      message += "📝 Último reinicio: *" + getResetReason() + "*";
      bot.sendMessage(chat_id, message, "Markdown");

    } else if (text == "/reset") {
      bot.sendMessage(chat_id, "🔁 Reiniciando ESP32...", "Markdown");
      delay(300);
      ESP.restart();

    } else if (text == "/clearResetCount") {
      resetCount = 0;
      bot.sendMessage(chat_id, "♻️ *Contador de reinicios reiniciado.*", "Markdown");

    } else if (text == "/infoDevices") {
      // Info del dispositivo (incluye Temp CPU en el momento)
      float tempCPU = getInternalTempESP32();

      String message = "🖥️ *Info del Dispositivo:*\n";
      message += "🆔 ID: *" + String((uint32_t)ESP.getEfuseMac(), HEX) + "*\n";
      message += "🔗 MAC: *" + WiFi.macAddress() + "*\n";
      message += "🌐 SSID: *" + WiFi.SSID() + "*\n";
      message += "📡 IP: *" + WiFi.localIP().toString() + "*\n";
      message += "📶 RSSI: *" + String(WiFi.RSSI()) + " dBm*\n";
      message += "🌡️ Temp CPU: *" + String(tempCPU, 1) + " °C* _(sensor interno no calibrado)_\n";
      unsigned long uptime = millis() / 1000;
      char buffer[10];
      sprintf(buffer, "%02lu:%02lu:%02lu", uptime / 3600, (uptime % 3600) / 60, uptime % 60);
      message += "⏱️ Uptime: *" + String(buffer) + "*";
      bot.sendMessage(chat_id, message, "Markdown");

    } else {
      bot.sendMessage(chat_id,
        "❌ *Comando no reconocido.*\n"
        "ℹ️ Usa */menu* para ver los comandos disponibles.",
        "Markdown");
    }
  }
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);

  // DHT22
  dht.begin();

  // Contador de reinicios simple (sin persistencia)
  resetCount++;

  // Conexión WiFi por WiFiManager (abre portal si hace falta)
  bool wifiOk = connectWithWiFiManager();

  // TLS para Telegram:
  //  - Para producción, usar setCACert() con el root CA de api.telegram.org
  //  - Para pruebas, usamos setInsecure() (NO valida certificados)
  secured_client.setInsecure();

  // Mensaje de arranque (si hay WiFi)
  if (wifiOk) {
    String bootMsg = "✅ *Sistema Iniciado*\n";
    bootMsg += "📅 Build: " + String(__DATE__) + " " + String(__TIME__) + "\n";
    bootMsg += "📝 Motivo: *" + getResetReason() + "*\n";
    bootMsg += "🔁 Reinicios: *" + String(resetCount) + "*\n";
    bootMsg += "🌐 SSID: *" + WiFi.SSID() + "*\n";
    bootMsg += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + "*";
    bot.sendMessage(CHANNEL_CHAT_ID, bootMsg, "Markdown");
  } else {
    Serial.println("📶 Configurá WiFi desde el portal (AP) para habilitar Telegram.");
  }

  // Semilla para valores simulados (calidad de aire / generador)
  randomSeed(esp_random());
}

// ====== Loop ======
void loop() {
  // Reintento básico si se pierde WiFi
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 30000) { // cada 30s
      lastTry = millis();
      WiFi.reconnect();
      // Alternativa: reabrir portal manual por tiempo limitado:
      // wm.setConfigPortalTimeout(60);
      // char apName[24]; makeDynamicApName(apName, sizeof(apName));
      // wm.startConfigPortal(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);
    }
  }

  // Envío periódico automático al canal
  unsigned long now = millis();
  if (now - previousMillis >= interval) {
    previousMillis = now;
    if (WiFi.status() == WL_CONNECTED) {
      sendSensorData();
    }
  }

  // Atender comandos entrantes por Telegram
  if (WiFi.status() == WL_CONNECTED) {
    int numMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numMessages > 0) {
      handleNewMessages(numMessages);
    }
  }

  delay(100);
}
