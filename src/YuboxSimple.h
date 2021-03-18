#ifndef _YUBOX_SIMPLE_WRAPPER_H_
#define _YUBOX_SIMPLE_WRAPPER_H_

#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <AsyncJson.h>
#include <ArduinoJson.h>

#include <YuboxWiFiClass.h>
#include <YuboxNTPConfigClass.h>
#include <YuboxOTAClass.h>

void yuboxSimpleSetup(void);
void yuboxSimpleLoopTask(void);

extern AsyncWebServer yubox_HTTPServer;

#endif
