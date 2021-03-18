#include <Arduino.h>
#include "YuboxSimple.h"

AsyncWebServer yubox_HTTPServer(80);

static void yubox_json_notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"msg\":\"El recurso indicado no existe o no ha sido implementado\"}");
}

void yuboxSimpleSetup(void)
{
  if (!SPIFFS.begin(true)) {
    Serial.println("ERR: ha ocurrido un error al montar SPIFFS");
    return;
  }

  // Limpiar archivos que queden de actualización fallida
  YuboxOTA.cleanupFailedUpdateFiles();

  // Activar y agregar todas las rutas que requieren autenticación
  YuboxWebAuth.setEnabled(true);	// <-- activar explícitamente la autenticación

  YuboxWiFi.begin(yubox_HTTPServer);
  YuboxWebAuth.begin(yubox_HTTPServer);
  YuboxNTPConf.begin(yubox_HTTPServer);
  YuboxOTA.begin(yubox_HTTPServer);

  AsyncWebHandler &h = yubox_HTTPServer.serveStatic("/", SPIFFS, "/");
  YuboxWebAuth.addManagedHandler(&h);
  yubox_HTTPServer.onNotFound(yubox_json_notFound);

  YuboxWiFi.beginServerOnWiFiReady(&yubox_HTTPServer);
}

void yuboxSimpleLoopTask(void)
{
  YuboxNTPConf.update();
  if (!YuboxNTPConf.isNTPValid()) {
    if (WiFi.isConnected()) Serial.println("ERR: fallo al obtener hora de red");
  }
}