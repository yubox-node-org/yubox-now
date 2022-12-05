#pragma once
#include "esp32-hal-log.h"
#include <ESPAsyncWebServer.h>

class YuboxDebugClass
{
private:
    static const char *_ns_nvram_yuboxframework_debug;
    static int _debugEspressifHandler(const char *format, va_list args);

    int _localLevel;

    // Yubox Framework
    void _routeHandler_yuboxAPI_debugconfjson_GET(AsyncWebServerRequest *);
    void _routeHandler_yuboxAPI_debugconfjson_POST(AsyncWebServerRequest *);

public:
    void begin(AsyncWebServer &srv);
    void setDebugLevel(int _intLevel);
};

extern YuboxDebugClass YuboxDebugConfig;
