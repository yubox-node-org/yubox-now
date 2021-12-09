#include <WiFi.h>
#include "SPIFFS.h"
#include <ESPAsyncWebServer.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include "YuboxWiFiClass.h"
#include "YuboxOTAClass.h"

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *);
void setupAsyncServerHTTP(void);

#define   LED             GPIO_NUM_4

void setup()
{
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: ha ocurrido un error al montar SPIFFS");
    while (true) delay(1000);
  }

  // Limpiar archivos que queden de actualización fallida
  YuboxOTA.cleanupFailedUpdateFiles();

  setupAsyncServerHTTP();

  YuboxWiFi.beginServerOnWiFiReady(&server);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, false);
}

uint32_t blink_ms = 1000;
bool blink_on = false;
uint32_t blink_lastms = 0;

void loop()
{
    uint32_t t = millis();
    if (t - blink_lastms >= blink_ms) {
        blink_on = !blink_on;
        blink_lastms = t;
        digitalWrite(LED, blink_on);
    }
    delay(100);
}

void routeHandler_configjson_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  uint32_t n_blinkms = blink_ms;

  if (!request->hasParam("blinkms", true)) {
    clientError = false;
    responseMsg = "Parámetro de intervalo es obligatorio";
  } else {
    p = request->getParam("blinkms", true);
    int n = sscanf(p->value().c_str(), "%u", &n_blinkms);
    if (n <= 0) {
      clientError = false;
      responseMsg = "Parámetro no es numérico";
    }
  }

    // ...

  if (!clientError) {
    blink_ms = n_blinkms;
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }
  unsigned int httpCode = 200;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

void setupAsyncServerHTTP(void)
{
  // Activar y agregar todas las rutas que requieren autenticación
  YuboxWebAuth.setEnabled(true);	// <-- activar explícitamente la autenticación

  YuboxWiFi.begin(server);
  YuboxWebAuth.begin(server);
  YuboxOTA.begin(server);
  server.onNotFound(notFound);

  server.on("/yubox-api/blinktest/conf.json", HTTP_POST, routeHandler_configjson_POST);

  AsyncWebHandler &h = server.serveStatic("/", SPIFFS, "/");
  YuboxWebAuth.addManagedHandler(&h);
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"success\":false,\"msg\":\"El recurso indicado no existe o no ha sido implementado\"}");
}
