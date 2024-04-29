#include <YuboxSimple.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

void setupAsyncServerHTTP(void);

#if CONFIG_IDF_TARGET_ESP32S3
#define   LED             LED_BUILTIN
#else
#define   LED             GPIO_NUM_4
#endif

void setup()
{
  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  setupAsyncServerHTTP();

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
#if ARDUINOJSON_VERSION_MAJOR <= 6
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));
#else
  JsonDocument json_doc;
#endif
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

void setupAsyncServerHTTP(void)
{
  yubox_HTTPServer.on("/yubox-api/blinktest/conf.json", HTTP_POST, routeHandler_configjson_POST);
  yuboxSimpleSetup();
}
