#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
// #include <Preferences.h>  // COMENTADO para evitar uso en simulador
#include "esp_system.h"
#include <esp_chip_info.h>
#include <esp_spi_flash.h>
#include "esp_wifi.h"
#include <WiFiManager.h> //Libnria para gestionar el WiFi fácilmente
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// === CONFIGURACIÓN WiFi Manager==
#define WM_AP_PASSWORD   "12345678"   // Deja "" si querés portal abierto (8+ chars si usas password)
#define WM_CP_TIMEOUT_S  180          // Timeout del portal (segundos)

// === Generar nombre AP dinámico: ESP32-XXXX ===
String chipMac = WiFi.macAddress(); // Obtener la MAC del ESP32. ej: "AA:BB:CC:11:22:33"
String apName = "ESP32-" + chipMac.substring(8); // Crear nombre AP con últimos 4 dígitos de la MAC

// === CONFIGURACIÓN BOT DE TELEGRAM===
#define BOT_TOKEN "8385145731:AAFo0sg1qxpHpMIerwlrOaTwCBf1SQ-g2S0"
#define CHANNEL_CHAT_ID "@sextoTobar"

// === OBJETOS GLOBALES ===
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
WiFiManager wm; // Objeto WiFiManager

// === VARIABLES GLOBALES DE CONTROL===
long interval = 10000; // Intervalo entre mensajes (10 segundos)
unsigned long previousMillis = 0; // Almacena el tiempo del último mensaje enviado  
int resetCount = 0; // Contador de resets por falla de WiFi
float tempCPU = 0.0; // Variable para almacenar la temperatura del CPU  

// === CONFIGURACIÓN SENSOR DHT22 ===
#define DHTPIN 4     // Pin donde está conectado el DHT22
#define DHTTYPE DHT22   // Definición del tipo de sensor DHT
DHT dht(DHTPIN, DHTTYPE); // Objeto DHT

// === SENSOR INTERNO DE TEMPERATURA DEL ESP32 (NO CALIBRADO) ===
extern "C" uint8_t temprature_sens_read();
float getInternalTempESP32() {
  return ((temprature_sens_read() - 32) / 1.8);  // Convierte a °C aprox
}


// === FUNCIÓN: MOTIVO DEL REINICIO (no afecta en Wokwi) ===
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

// === ENVÍA DATOS SIMULADOS AL CANAL TELEGRAM ===
void sendSensorData() {
  float temperatura = dht.readTemperature(); // Leer temperatura en °C
  float humedad = dht.readHumidity();    // Leer humedad en %
  float tempCPU = getInternalTempESP32(); // Leer temperatura interna del ESP32
  int calidadAire = random(100, 500); // Simular calidad del aire en ppm
  bool generadorEncendido = random(0, 2); // Simular estado del generador (encendido/apagado)

  String estadoGenerador = generadorEncendido ? "Encendido 🔌" : "Apagado ❌";
  String message = "📡 *Datos Sistema IoT San Miguel (Simulados):*\n";
  message += "🌡️ Temperatura: *" + String(temperatura, 1) + " °C*\n";
  message += "💧 Humedad: *" + String(humedad, 1) + " %*\n";
  message += "🧪 Calidad del Aire: *" + String(calidadAire) + " ppm*\n";
  message += "⚙️ Generador: *" + estadoGenerador + "*\n";
  message += "\n👨‍💻 _Dev. for: Ing. Gambino_";

  bot.sendMessage(CHANNEL_CHAT_ID, message, "Markdown");
}

// === MANEJO DE MENSAJES DE TELEGRAM ===
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String text = bot.messages[i].text;
    String chat_id = bot.messages[i].chat_id;

    // Ignora mensajes vacíos o que no sean comandos
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
      sendSensorData();

    } else if (text == "/APreset") {
      bot.sendMessage(chat_id, "⚠️ *APreset deshabilitado en simulador.*", "Markdown");

    } else if (text.startsWith("/setInterval ")) {
      String arg = text.substring(13);
      int newInterval = arg.toInt() * 1000;
      if (newInterval > 0) {
        interval = newInterval;
        // prefs.begin("system", false);  // COMENTADO
        // prefs.putInt("interval_ms", interval);
        // prefs.end();
        bot.sendMessage(chat_id, "⏱️ Intervalo cambiado a *" + arg + "* segundos.", "Markdown");
      } else {
        bot.sendMessage(chat_id, "⚠️ Intervalo inválido. Usa un número positivo.", "Markdown");
      }

    } else if (text == "/status") {
      String message = "📈 *Estado General:*\n";
      message += "🔁 Reinicios: *" + String(resetCount) + "*\n";
      message += "⏱️ Intervalo: *" + String(interval / 1000) + " s*\n";
      message += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "Conectado ✅" : "Desconectado ❌") + "*\n";
      message += "📝 Último reinicio: *" + getResetReason() + "*";
      bot.sendMessage(chat_id, message, "Markdown");

    } else if (text == "/reset") {
      bot.sendMessage(chat_id, "⚠️ *Reset deshabilitado en simulador.*", "Markdown");

    } else if (text == "/clearResetCount") {
      resetCount = 0;
      // prefs.begin("system", false);  // COMENTADO
      // prefs.putInt("reset_count", resetCount);
      // prefs.end();
      bot.sendMessage(chat_id, "♻️ *Contador de reinicios reiniciado.*", "Markdown");

    } else if (text == "/infoDevices") {
      String message = "🖥️ *Info del Dispositivo:*\n";
      message += "📍 Ubicación: *Casa*\n";
      message += "🆔 ID: *" + String((uint32_t)ESP.getEfuseMac(), HEX) + "*\n";
      message += "🔗 MAC: *" + WiFi.macAddress() + "*\n";
      message += "🌐 Red: *" + String(WiFi.SSID()) + "*\n";
      message += "📡 IP: *" + WiFi.localIP().toString() + "*\n";
      message += "📶 RSSI: *" + String(WiFi.RSSI()) + " dBm*\n";
      message += "📡 MQTT: *ON* (simulado)\n";
      message += "🌡️ Temp CPU: *" + String(tempCPU, 1) + " °C* _(sensor interno)_\n"; // 
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

// === SETUP INICIAL DEL SISTEMA ===
void setup() {
  Serial.begin(115200); // Inicializar comunicación serial
  dht.begin(); // Inicializar sensor DHT22
  resetCount++; // Incrementar contador de resets

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n✅ Conectado");

  secured_client.setInsecure();  // Para certificados en Wokwi
  randomSeed(analogRead(0));

  String bootMsg = "⚠️ *Sistema Iniciado (modo Wokwi)*\n";
  bootMsg += "📅 Fecha/Hora: " + String(__DATE__) + " " + String(__TIME__) + "\n";
  bootMsg += "📝 Motivo: *" + getResetReason() + "*\n";
  bootMsg += "🔁 Reinicios: *" + String(resetCount) + "*\n";
  bootMsg += "📶 WiFi: *" + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + "*";
  bot.sendMessage(CHANNEL_CHAT_ID, bootMsg, "Markdown");
}

// === LOOP PRINCIPAL ===
void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    sendSensorData();
  }

  int numMessages = bot.getUpdates(bot.last_message_received + 1);
  if (numMessages > 0) {
    handleNewMessages(numMessages);
  }

  delay(100);
}
