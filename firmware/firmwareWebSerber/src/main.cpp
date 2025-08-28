#include <WiFi.h>

// Credenciales de tu red WiFi
const char* WIFI_SSID = "electronicagambino.com";
const char* WIFI_PASS = "tesla0381";

void setup() {
  Serial.begin(115200);
  delay(200);

  // Configurar ESP32 en modo estación
  WiFi.mode(WIFI_STA);

  // Iniciar conexión
  Serial.printf("Conectando a %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Esperar hasta conectarse
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Mostrar información
  Serial.println("\n¡Conectado!");
  Serial.print("SSID: "); Serial.println(WiFi.SSID());
  Serial.print("IP: ");   Serial.println(WiFi.localIP());
}

void loop() {
  // Aquí tu lógica de programa
}
