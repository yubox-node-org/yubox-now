#include <Arduino.h>
#include "YuboxSimple.h"

AsyncWebServer yubox_HTTPServer(80);

static void yubox_json_notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"success\":false,\"msg\":\"El recurso indicado no existe o no ha sido implementado\"}");
}

void yuboxSimpleSetup(void)
{
  if (!SPIFFS.begin(true)) {
    log_e("ha ocurrido un error al montar SPIFFS");
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

void yuboxAddManagedHandler(AsyncWebHandler* handler)
{
  YuboxWebAuth.addManagedHandler(handler);
  yubox_HTTPServer.addHandler(handler);
}

void yuboxSimpleLoopTask(void)
{
  static uint32_t last_check = 0;

  uint32_t t = millis();
  if (last_check == 0 || t - last_check >= 5000) {
    last_check = t;
    if (!YuboxNTPConf.update(0)) {
      if (WiFi.isConnected()) log_w("fallo al obtener hora de red");
    }
  }
}