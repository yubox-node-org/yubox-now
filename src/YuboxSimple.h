#ifndef _YUBOX_SIMPLE_WRAPPER_H_
#define _YUBOX_SIMPLE_WRAPPER_H_

#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#include <YuboxWiFiClass.h>
#include <YuboxNTPConfigClass.h>
#include <YuboxOTAClass.h>

void yuboxSimpleSetup(void);
void yuboxSimpleLoopTask(void);
void yuboxAddManagedHandler(AsyncWebHandler* handler);

extern AsyncWebServer yubox_HTTPServer;

#endif
