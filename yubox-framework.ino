#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "YuboxWiFiClass.h"

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *);

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  Serial.println("Inicializando SPIFFS...");
  if (!SPIFFS.begin(true)){
    Serial.println("Ha ocurrido un error al montar SPIFFS");
    return;
  }

  YuboxWiFi.begin(server);
  server.serveStatic("/", SPIFFS, "/");
  server.onNotFound(notFound);
  server.begin();
}

void loop()
{
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{msg:\"El recurso indicado no existe o no ha sido implementado\"}");
}

