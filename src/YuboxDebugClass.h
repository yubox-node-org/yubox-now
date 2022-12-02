#pragma once
#include "esp32-hal-log.h"
#include <ESPAsyncWebServer.h>

class YuboxDebugClass
{
private:
    static const char *_ns_nvram_yuboxframework_debug;
    static int _debugOutputHandler(const char *format, va_list args);

    // Constants
    esp_log_level_t _levels[5] = {ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE};
    int _localLevel;

    // Yubox Framework
    void _routeHandler_yuboxAPI_debugconfjson_GET(AsyncWebServerRequest *);
    void _routeHandler_yuboxAPI_debugconfjson_POST(AsyncWebServerRequest *);

public:
    void begin(AsyncWebServer &srv);
    void setDebugLevel(int _intLevel);
};

extern YuboxDebugClass YuboxDebugConfig;
