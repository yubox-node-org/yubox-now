#include "YuboxWiFiClass.h"
#include "YuboxNTPConfigClass.h"
#include "YuboxWebAuthClass.h"

#include <functional>

#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include "lwip/apps/sntp.h"

static const char * yubox_default_ntpserver = "pool.ntp.org";

const char * YuboxNTPConfigClass::_ns_nvram_yuboxframework_ntpclient = "YUBOX/NTP";
YuboxNTPConfigClass::YuboxNTPConfigClass(void)
{
    _ntpStart = false;
    _ntpValid = false;
    _ntpFirst = true;
    _ntpServerName = yubox_default_ntpserver;
    _ntpOffset = 0;
}

void YuboxNTPConfigClass::begin(AsyncWebServer & srv)
{
  WiFi.onEvent(std::bind(&YuboxNTPConfigClass::_cbHandler_WiFiEvent, this, std::placeholders::_1));
  _loadSavedCredentialsFromNVRAM();
  _setupHTTPRoutes(srv);
}

void YuboxNTPConfigClass::_loadSavedCredentialsFromNVRAM(void)
{
  Preferences nvram;

  if (sntp_enabled()) sntp_stop();

  // Para cada una de las preferencias, si no está seteada se obtendrá cadena vacía
  nvram.begin(_ns_nvram_yuboxframework_ntpclient, true);

  _ntpServerName = nvram.getString("ntphost", _ntpServerName);
  _ntpOffset = nvram.getLong("ntptz", _ntpOffset);
  _configTime();
}

void YuboxNTPConfigClass::_configTime(void)
{
  // Arduino ESP32 no acepta más de 1 servidor NTP a pesar de pasarle hasta 3
  if (_ntpServerName == yubox_default_ntpserver) {
    configTime(_ntpOffset, 0, _ntpServerName.c_str());
  } else {
    configTime(_ntpOffset, 0, _ntpServerName.c_str(), yubox_default_ntpserver);
  }
}

void YuboxNTPConfigClass::_cbHandler_WiFiEvent(WiFiEvent_t event)
{
  //Serial.printf("DEBUG: YuboxNTPConfigClass::_cbHandler_WiFiEvent [WiFi-event] event: %d\r\n", event);
  switch(event) {
  case SYSTEM_EVENT_STA_GOT_IP:
    if (!_ntpStart) {
      //Serial.println("DEBUG: YuboxNTPConfigClass::_cbHandler_WiFiEvent - estableciendo conexión UDP para NTP...");
      _configTime();
      _ntpStart = true;
    }
    break;
  }
}

void YuboxNTPConfigClass::_setupHTTPRoutes(AsyncWebServer & srv)
{
  srv.on("/yubox-api/ntpconfig/conf.json", HTTP_GET, std::bind(&YuboxNTPConfigClass::_routeHandler_yuboxAPI_ntpconfjson_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/ntpconfig/conf.json", HTTP_POST, std::bind(&YuboxNTPConfigClass::_routeHandler_yuboxAPI_ntpconfjson_POST, this, std::placeholders::_1));
}

void YuboxNTPConfigClass::_routeHandler_yuboxAPI_ntpconfjson_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));

  // Valores informativos, no pueden cambiarse vía web
  json_doc["ntpsync"] = isNTPValid();

  // Valores a cambiar vía web
  json_doc["ntphost"] = _ntpServerName.c_str();
  json_doc["ntptz"] = _ntpOffset;

  serializeJson(json_doc, *response);
  request->send(response);
}

#define NVRAM_PUTSTRING(nvram, k, s) \
    (((s).length() > 0) ? ((nvram).putString((k), (s)) != 0) : (nvram).remove((k)))
void YuboxNTPConfigClass::_routeHandler_yuboxAPI_ntpconfjson_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  /* Se esperan en el POST los siguientes escenarios de parámetros:
    - Parámetro "ntphost" siempre debe estar presente. El host debe consistir únicamente
      de componentes no-vacíos separados por puntos, que consistan únicamente de
      caracteres a-z o 0-9 y guión, y que no pueden empezar o terminar con guión.
    - Parámetro "ntptz" es requerido, numérico positivo o negativo.
  */
  String n_host;
  int32_t n_tz;

  n_host = _ntpServerName;
  n_tz = _ntpOffset;

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  if (!clientError && !request->hasParam("ntphost", true)) {
    clientError = true;
    responseMsg = "Se requiere host de servidor NTP";
  }
  if (!clientError) {
    p = request->getParam("ntphost", true);
    n_host = p->value();
    n_host.toLowerCase();
    n_host.trim();
    if (n_host.length() <= 0) {
      clientError = true;
      responseMsg = "Se requiere host de servidor NTP";
    } else if (!_isValidHostname(n_host)) {
      clientError = true;
      responseMsg = "Nombre de host de servidor NTP no es válido";
    }
  }
  if (!clientError) {
    if (request->hasParam("ntptz", true)) {
      p = request->getParam("ntptz", true);

      if (0 >= sscanf(p->value().c_str(), "%ld", &n_tz)) {
        clientError = true;
        responseMsg = "Formato de zona horaria no es válido";
      }
    }
  }

  // Si todos los parámetros son válidos, se intenta guardar en NVRAM
  if (!clientError) {
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_ntpclient, false);

    if (!serverError && !nvram.putString("ntphost", n_host)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: host";
    }
    if (!serverError && !nvram.putLong("ntptz", n_tz)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: pass";
    }
    nvram.end();

    if (!serverError) {
      _ntpFirst = true;
      _ntpValid = false;
      _loadSavedCredentialsFromNVRAM();
      if (WiFi.isConnected()) _configTime();
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }
  unsigned int httpCode = 200;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

bool YuboxNTPConfigClass::update(uint32_t ms_timeout)
{
  if (!_ntpStart) return false;
  if (!WiFi.isConnected()) return _ntpValid;
  if (_ntpFirst) {
    ESP_LOGD(__FILE__, "YuboxNTPConfigClass::update - conexión establecida, pidiendo hora de red vía NTP...");
    _ntpFirst = false;
  }
  _ntpValid = isNTPValid(ms_timeout);
  return _ntpValid;
}

bool YuboxNTPConfigClass::isNTPValid(uint32_t ms_timeout)
{
  if (_ntpValid) return true;

  uint32_t start = millis();
  time_t now;
  struct tm info;
  do {
    time(&now);
    gmtime_r(&now, &info);
    if (info.tm_year > (2016 - 1900)) {
      _ntpValid = true;
      break;
    }
    if (ms_timeout > 0) delay(10);
  } while (millis() - start <= ms_timeout);

  return _ntpValid;
}

unsigned long YuboxNTPConfigClass::getLocalTime(void)
{
  return getUTCTime() + _ntpOffset;
}

unsigned long YuboxNTPConfigClass::getUTCTime(void)
{
  time_t now;
  time(&now);
  return now;
}

bool YuboxNTPConfigClass::_isValidHostname(String & h)
{
  int tok = 0;

  while (tok != -1) {
    int dot = h.indexOf('.', tok);
    if (dot == tok) {
      // Se ha encontrado dos puntos sucesivos
      return false;
    }
    String hc;
    if (dot != -1) {
      hc = h.substring(tok, dot);

      tok = dot + 1;
      if (tok >= h.length()) {
        // El punto encontrado era el último caracter de la cadena
        return false;
      }
    } else {
      hc = h.substring(tok);
      tok = -1;
    }

    // Validación del componente de host
    if (hc[0] == '-' || hc[hc.length() - 1] == '-') {
      return false;
    }
    for (int i = 0; i < hc.length(); i++) {
      if (!isalnum(hc[i]) && hc[i] != '-') return false;
    }
  }
  return true;
}

YuboxNTPConfigClass YuboxNTPConf;
