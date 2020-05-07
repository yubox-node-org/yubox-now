#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include "YuboxWiFiClass.h"
#include "YuboxOTAClass.h"

#include "YuboxAsyncNTPClient.h"

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *);

// El modelo viejo de YUBOX tiene este sensor integrado en el board
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 sensor_bmp280;
YuboxAsyncNTPClient ntp;
bool ntpStart = false;

void WiFiEvent(WiFiEvent_t event);
AsyncEventSource eventosLector("/yubox-api/lectura/events");

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  Serial.println("Inicializando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("Ha ocurrido un error al montar SPIFFS");
    return;
  }

  // Activar y agregar todas las rutas que requieren autenticación
  YuboxWebAuth.setEnabled(true);	// <-- activar explícitamente la autenticación
  AsyncWebHandler &h = server.serveStatic("/", SPIFFS, "/");
  YuboxWebAuth.addManagedHandler(&h);
  YuboxWebAuth.addManagedHandler(&eventosLector);
  server.addHandler(&eventosLector);

  YuboxWiFi.begin(server);
  YuboxWebAuth.begin(server);
  YuboxOTA.begin(server);
  server.onNotFound(notFound);
  server.begin();

  WiFi.onEvent(WiFiEvent);

  if (!sensor_bmp280.begin(BMP280_ADDRESS_ALT)) {
    Serial.println("ERR: no puede inicializarse el sensor BMP280!");
  }
}

bool ntpFirst = true;
void loop()
{
  if (WiFi.isConnected()) {
    if (ntpFirst) Serial.println("DEBUG: Conexión establecida, pidiendo hora de red vía NTP...");
    if (ntp.isTimeValid()) {
      if (ntpFirst) {
        ntpFirst = false;
        Serial.println("DEBUG: hora de red obtenida!");
      }
      DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
      json_doc["ts"] = ntp.time_ms();
      json_doc["temperature"] = sensor_bmp280.readTemperature();
      json_doc["pressure"] = sensor_bmp280.readPressure();

      String json_output;
      serializeJson(json_doc, json_output);

      if (eventosLector.count() > 0) {
        eventosLector.send(json_output.c_str());
      }
    } else {
      Serial.println("ERR: todavía no es válida la hora de red");
    }
  } else {
    Serial.println("WARN: red desconectada");
  }
  delay(3000);
}

void WiFiEvent(WiFiEvent_t event)
{
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      if (!ntpStart) {
        Serial.println("DEBUG: Estableciendo conexión UDP para NTP...");
        ntp.begin();
        ntpStart = true;
      }
      break;
    }
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"msg\":\"El recurso indicado no existe o no ha sido implementado\"}");
}
