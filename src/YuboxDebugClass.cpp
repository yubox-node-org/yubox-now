#include "YuboxWiFiClass.h"
#include "YuboxDebugClass.h"
#include "YuboxEspNowClass.h"
#include <Preferences.h>
#include <ArduinoJson.h>

const char *YuboxDebugClass::_ns_nvram_yuboxframework_debug = "YUBOX/DEBUG";
SemaphoreHandle_t xSemaphoreDebug;

typedef struct structBuffer
{
    char debug[ESP_NOW_MAX_DATA_LEN];
};

const esp_log_level_t _levels[5] = {ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE};
char *_debugLevelLetter = "EWIDV";

char bufferLog[ESP_NOW_MAX_DATA_LEN];
int indiceBuffer = 0;

void _taskLogger(void *pvParameters) // This is a task.
{
    char _levelLog = bufferLog[9];
    const char *ptr = strchr(_debugLevelLetter, _levelLog);

    int index;
    if (ptr)
        index = ptr - _debugLevelLetter;

    esp_log_write(_levels[index], "%s", bufferLog);

    // Limpio el buffer
    indiceBuffer = 0;
    memset(bufferLog, 0, ESP_NOW_MAX_DATA_LEN);

    xSemaphoreGive(xSemaphoreDebug);
    vTaskDelete(NULL);
}

void ARDUINO_ISR_ATTR _debugOutputHandler(char c)
{
    if (c == '\n' || c == '\r')
    {
        // La API de esspressif nos indica que no podemos usarla en una interrupcion, por ello utilizamos un task
        if (xSemaphoreTake(xSemaphoreDebug, (TickType_t)20) == pdTRUE)
        {
            xTaskCreate(_taskLogger, "TaskLogger", 3024, NULL, 2, NULL);
        }
    }
    else
    {
        bufferLog[indiceBuffer] = c;
        indiceBuffer++;
        if (indiceBuffer >= ESP_NOW_MAX_DATA_LEN)
            indiceBuffer = 0;
    }
}

int YuboxDebugClass::_debugEspressifHandler(const char *format, va_list args)
{
    char buffer[ESP_NOW_MAX_DATA_LEN];
    vsnprintf(buffer, ESP_NOW_MAX_DATA_LEN, format, args);

    structBuffer msg;
    strcpy(msg.debug, buffer);
    uint8_t peerAddress[] = {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00};

    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.send(peerAddress, ((uint8_t *)&msg), sizeof(msg));

    return Serial.print(buffer);
}

void YuboxDebugClass::begin(AsyncWebServer &srv)
{
    delay(500);

    // Iniciamos el ESP-NOW
    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.setMode(ESP_COMBO);

    // Iniciamos el Xemaphore
    xSemaphoreDebug = xSemaphoreCreateBinary();
    if (xSemaphoreDebug != NULL)
        xSemaphoreGive(xSemaphoreDebug);

    // Revisamos el debug guardado
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_debug, true);
    nvram.end();

    _localLevel = nvram.getInt("level", 3);
    esp_log_level_set("*", _levels[_localLevel]);

    // Redirigimos todo el flujo debug a una funcion nuestra
    ets_install_putc1((void (*)(char)) & _debugOutputHandler);

    // Configuramos la funcion callbaks
    esp_log_set_vprintf(this->_debugEspressifHandler);

    // Setup Rut
    srv.on("/yubox-api/debug/conf.json", HTTP_GET, std::bind(&YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_GET, this, std::placeholders::_1));
    srv.on("/yubox-api/debug/conf.json", HTTP_POST, std::bind(&YuboxDebugClass::_routeHandler_yuboxAPI_debugconfjson_POST, this, std::placeholders::_1));

    log_d("Debug Yubox Inicializado");
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