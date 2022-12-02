#include "YuboxWiFiClass.h"
#include "YuboxDebugClass.h"
#include "YuboxEspNowClass.h"
#include <Preferences.h>
#include <ArduinoJson.h>

const char *YuboxDebugClass::_ns_nvram_yuboxframework_debug = "YUBOX/DEBUG";

typedef struct msgData
{
    char debug[200];
};

uint8_t peerAddress[] = {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00};

int YuboxDebugClass::_debugOutputHandler(const char *format, va_list args)
{
    char buffer[200];
    vsnprintf(buffer, 200, format, args);

    msgData msg;
    strcpy(msg.debug, buffer);

    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.send(peerAddress, ((uint8_t *)&msg), sizeof(msg));

    return Serial.print(buffer);
}

void YuboxDebugClass::begin(AsyncWebServer &srv)
{
    // Iniciamos el ESP-NOW
    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.setMode(ESP_COMBO);

    // Revisamos el debug guardado
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_debug, true);
    nvram.end();

    _localLevel = nvram.getInt("level", 0);
    esp_log_level_set("*", _levels[_localLevel]);

    // Configuramos la funcion callbaks
    esp_log_set_vprintf(this->_debugOutputHandler);

    // Setup Rut
    srv.on("/yubox-api/debug/conf.json", HTTP_GET, std::bind(&YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_GET, this, std::placeholders::_1));
    srv.on("/yubox-api/debug/conf.json", HTTP_POST, std::bind(&YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_POST, this, std::placeholders::_1));
}

void YuboxDebugClass::setDebugLevel(int _intLevel)
{
    esp_log_level_set("*", _levels[_intLevel]);
    _localLevel = _intLevel;

    // Revisamos el debug guardado
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_debug, true);
    nvram.putInt("level", _intLevel);
    nvram.end();
}

void YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_POST(AsyncWebServerRequest *request)
{
    YUBOX_RUN_AUTH(request);
    uint16_t n_level;

    bool clientError = false;
    bool serverError = false;
    String responseMsg = "";
    AsyncWebParameter *p;

#define ASSIGN_FROM_POST(TAG, FMT)                             \
    if (!clientError && request->hasParam(#TAG, true))         \
    {                                                          \
        p = request->getParam(#TAG, true);                     \
        int n = sscanf(p->value().c_str(), FMT, &(n_##TAG));   \
        if (n <= 0)                                            \
        {                                                      \
            clientError = true;                                \
            responseMsg = "Formato numérico incorrecto para "; \
            responseMsg += #TAG;                               \
        }                                                      \
    }

    ASSIGN_FROM_POST(level, "%hu")
    setDebugLevel(n_level);

    if (!clientError && !serverError)
    {
        responseMsg = "Parámetros actualizados correctamente";
    }
    unsigned int httpCode = 200;
    if (clientError)
        httpCode = 400;
    if (serverError)
        httpCode = 500;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->setCode(httpCode);
    StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
    json_doc["success"] = !(clientError || serverError);
    json_doc["msg"] = responseMsg.c_str();

    serializeJson(json_doc, *response);
    request->send(response);
}

void YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_GET(AsyncWebServerRequest *request)
{
    YUBOX_RUN_AUTH(request);

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));

    // Valores informativos, no pueden cambiarse vía web
    json_doc["level"] = _localLevel;
    serializeJson(json_doc, *response);
    request->send(response);
}

YuboxDebugClass YuboxDebugConfig;