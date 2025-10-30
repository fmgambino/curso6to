/****************************************************
 * ESP32 + WiFiManager + DHT22 + Temp Interna + Telegram
 * Basado en tu código original, adaptado a:
 * - Portal cautivo WiFiManager (AP: ESP32-XXXX)
 * - DHT22 real (Temp y Humedad)
 * - Temp interna del chip (no calibrada) como "Temp CPU"
 * - /APreset ahora borra credenciales y reinicia
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

// === TELEGRAM (se mantienen tus valores originales) ===
#define BOT_TOKEN        "8385145731:AAFo0sg1qxpHpMIerwlrOaTwCBf1SQ-g2S0"
#define CHANNEL_CHAT_ID  "@sextoTobar"

// === WiFiManager ===
#define WM_AP_PASSWORD   "12345678"   // Deja "" si querés portal abierto (mín. 8 chars si usás password)
#define WM_CP_TIMEOUT_S  180          // segundos de timeout del portal

// === DHT22 REAL ===
#define DHTPIN   4
#define DHTTYPE  DHT22
DHT dht(DHTPIN, DHTTYPE);

// === OBJETOS GLOBALES ===
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiManager wm;

// === VARIABLES DE CONTROL ===
long interval = 10000;
unsigned long previousMillis = 0;
int resetCount = 0;

// === SENSOR INTERNO DE TEMPERATURA (NO CALIBRADO) ===
extern "C" uint8_t temprature_sens_read();
float getInternalTempESP32() {
  // Lectura aproximada (no es la temperatura real del CPU y NO está calibrada)
  return ((temprature_sens_read() - 32) / 1.8);
}

// === FUNCIÓN: MOTIVO DEL REINICIO ===
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
    case ESP_RST_BROWNOUT:    return "⚡ Corte de tensión (Brownout)";
    case ESP_RST_SDIO:        return "🔁 SDIO";
    default:                  return "❓ Desconocido";
  }
}

// === UTIL: nombre AP dinámico "ESP32-XXXX" ===
void makeDynamicApName(char* outName, size_t outLen) {
  String mac = WiFi.macAddress();     // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 4); // "EEFF"
  String ap = "ESP32-" + suffix;                   // "ESP32-EEFF"
  ap.toCharArray(outName, outLen);
}

// === CONEXIÓN WiFi con WiFiManager ===
bool connectWithWiFiManager() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  wm.setConfigPortalTimeout(WM_CP_TIMEOUT_S);

  char apName[24];
  makeDynamicApName(apName, sizeof(apName));

  Serial.print("📡 Portal WiFiManager AP: ");
  Serial.println(apName);

  // Conecta con credenciales guardadas o abre portal si no hay/falla
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

// === ENVÍA DATOS (DHT22 real + Temp interna + 'aire' simulado) AL CANAL ===
void sendSensorData() {
  // DHT22 necesita lecturas separadas por ≥ 2 s; nuestro intervalo es 10 s
  float temperatura = dht.readTemperature();   // °C (real)
  float humedad     = dht.readHumidity();      // %  (real)
  float tempCPU     = getInternalTempESP32();  // °C aprox (interno, no calibrado)

  if (isnan(temperatura) || isnan(humedad)) {
    bot.sendMessage(CHANNEL_CHAT_ID, "⚠️ Error leyendo DHT22 (temp/hum). Revisar cableado y pull-up 10k.", "");
    return;
  }

  // Aún simulado (puedes reemplazar con sensor real cuando quieras)
  int  calidadAire        = random(100, 500);
  bool generadorEncendido = random(0, 2);
  String estadoGenerador  = generadorEncendido ? "Encendido 🔌" : "Apagado ❌";

  String message = "📡 *Datos Sistema IoT (Reales DHT22 + Temp Interna):*\n";
  message += "🌡️ Temp Ambiente: *" + String(temperatura, 1) + " °C*\n";
  message += "💧 Humedad: *"      + String(humedad, 1)     + " %*\n";
  message += "🔥 Temp CPU: *"     + String(tempCPU, 1)     + " °C* _(sensor interno no calibrado)_\n";
  message += "🧪 Calidad del Aire: *" + String(calidadAire) + " ppm* _(simulado)_\n";
  message += "⚙️ Generador: *"    + estadoGenerador + "*\n";
  message += "\n👨‍💻 _Dev. for: Ing. Gambino_";

  bot.sendMessage(CHANNEL_CHAT_ID, message, "Markdown");
}

// === MANEJO DE MENSAJES DE TELEGRAM ===
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text = bot.messages[i].text;
    String chat_id = bot.messages[i].chat_id;

    if (text.length() == 0 || chat_id.length() == 0 || !text.startsWith("/")) {
      continue;
    }

    if (text == "/menu") {
      String reply = "📋 *Menú de Comandos:*\n\n";
      reply += "📊 /DataSensores - Datos actuales\n";
      reply += "🧹 /APreset - Borrar WiFi y reiniciar\n";
      reply += "⏱️ /setInterval [seg] - Cambiar intervalo\n";
      reply += "📈 /status - Estado general\n";
      reply += "🔁 /reset - Reiniciar ESP32\n";
      reply += "🖥️ /infoDevices - Info del dispositivo\n";
      reply += "♻️ /clearResetCount - Resetear contador";
      bot.sendMessage(chat_id, reply, "Markdown");

    } else if (text == "/DataSensores") {
      if (WiFi.status() == WL_CONNECTED) sendSensorData();

    } else if (text == "/APreset") {
      // Ahora es funcional: borra credenciales WiFi y reinicia
      bot.sendMessage(chat_id, "🧹 Borrando credenciales WiFi y reiniciando...", "Markdown");
      delay(400);
      wm.resetSettings();
      delay(200);
      ESP.restart();

    } else if (text.startsWith("/setInterval ")) {
      String arg = text.substring(13);
      int newInterval = arg.toInt() * 1000;
      if (newInterval >= 2000) { // DHT22 necesita ≥ 2s
        interval = newInterval;
        bot.sendMessage(chat_id, "⏱️ Intervalo cambiado a *" + arg + "* segundos.", "Markdown");
      } else {
        bot.sendMessage(chat_id, "⚠️ Intervalo inválido. Debe ser ≥ 2 segundos para DHT22.", "Markdown");
      }

    } else if (text == "/status") {
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
      float tempCPU = getInternalTempESP32(); // lectura actual

      String message = "🖥️ *Info del Dispositivo:*\n";
      message += "🆔 ID: *" + String((uint32_t)ESP.getEfuseMac(), HEX) + "*\n";
      message += "🔗 MAC: *" + WiFi.macAddress() + "*\n";
      message += "🌐 SSID: *" + WiFi.SSID() + "*\n";
      message += "📡 IP: *" + WiFi.localIP().toString() + "*\n";
      message += "📶 RSSI: *" + String(WiFi.RSSI()) + " dBm*\n";
      message += "📡 MQTT: *ON* (simulado)\n";
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

// === SETUP PRINCIPAL ===
void setup() {
  Serial.begin(115200);

  // DHT22
  dht.begin();

  // Contador de reinicios (sin persistencia)
  resetCount++;

  // Conexión por WiFiManager (abre portal si es necesario)
  bool wifiOk = connectWithWiFiManager();

  // TLS: para pruebas simplificadas; en producción usar setCACert() con root CA de Telegram
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

  // Semilla para simulados
  randomSeed(esp_random());
}

// === LOOP PRINCIPAL ===
void loop() {
  // Envío periódico
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      sendSensorData();
    }
  }

  // Atender comandos
  if (WiFi.status() == WL_CONNECTED) {
    int numMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numMessages > 0) {
      handleNewMessages(numMessages);
    }
  }

  // Reintento básico de WiFi
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 30000) {
      lastTry = millis();
      WiFi.reconnect();
      // Alternativa para reabrir portal manual (si quisieras):
      // wm.setConfigPortalTimeout(60);
      // char apName[24]; makeDynamicApName(apName, sizeof(apName));
      // wm.startConfigPortal(apName, (strlen(WM_AP_PASSWORD) >= 8) ? WM_AP_PASSWORD : nullptr);
    }
  }

  delay(100);
}
