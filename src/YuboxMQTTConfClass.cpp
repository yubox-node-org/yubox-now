#include "YuboxWiFiClass.h"
#include "YuboxMQTTConfClass.h"
#include "YuboxWebAuthClass.h"

#include <functional>

#include <Preferences.h>
#include <AsyncMqttClient.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#if ASYNC_TCP_SSL_ENABLED

// Los archivos de certificados se guardan en la partición SPIFFS
#include <SPIFFS.h>

// Rutas en SPIFFS de archivos de certificados
#define MQTT_SERVER_CERT "/mqtt-server.crt"
#define MQTT_CLIENT_CERT "/mqtt-client.crt"
#define MQTT_CLIENT_KEY  "/mqtt-client.key"

#endif

#define MQTT_PORT 1883

const char * YuboxMQTTConfClass::_ns_nvram_yuboxframework_mqtt = "YUBOX/MQTT";

void _cb_YuboxMQTTConfClass_connectMQTT(TimerHandle_t);
YuboxMQTTConfClass::YuboxMQTTConfClass(void)
{
  _autoConnect = false;
  _lastdisconnect = AsyncMqttClientDisconnectReason::TCP_DISCONNECTED;
  _yuboxMQTT_port = MQTT_PORT;

  _mqttReconnectTimer = xTimerCreate(
    "YuboxMQTTConfClass_mqttTimer",
    pdMS_TO_TICKS(2000),
    pdFALSE,
    (void*)this,
    &_cb_YuboxMQTTConfClass_connectMQTT);
  _mqttClient.onDisconnect(std::bind(&YuboxMQTTConfClass::_cbHandler_onMqttDisconnect, this, std::placeholders::_1));

#if ASYNC_TCP_SSL_ENABLED
  _rootCA = NULL; _rootCA_len = 0;
  _clientCert = NULL; _clientCert_len = 0;
  _clientKey = NULL; _clientKey_len = 0;
#endif
}

void YuboxMQTTConfClass::begin(AsyncWebServer & srv)
{
  WiFi.onEvent(std::bind(&YuboxMQTTConfClass::_cbHandler_WiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
  _loadSavedCredentialsFromNVRAM();
  _setupHTTPRoutes(srv);
}

void YuboxMQTTConfClass::_loadSavedCredentialsFromNVRAM(void)
{
  Preferences nvram;

  // Para cada una de las preferencias, si no está seteada se obtendrá cadena vacía
  nvram.begin(_ns_nvram_yuboxframework_mqtt, true);
  _yuboxMQTT_host = nvram.getString("host");
  _yuboxMQTT_port = nvram.getUShort("port", MQTT_PORT);
  _yuboxMQTT_user = nvram.getString("user");
  _yuboxMQTT_pass = nvram.getString("pass");

  log_d("Host de broker MQTT......: %s:%u", _yuboxMQTT_host.c_str(), _yuboxMQTT_port);
  log_d("Usuario de broker MQTT...: %s", _yuboxMQTT_user.c_str());
  log_d("Clave de broker MQTT.....: %s", _yuboxMQTT_pass.c_str());

  _mqttClient.setServer(_yuboxMQTT_host.c_str(), _yuboxMQTT_port);
  if (_yuboxMQTT_user.length() > 0) {
    _mqttClient.setCredentials(_yuboxMQTT_user.c_str(), _yuboxMQTT_pass.c_str());
  } else {
    _mqttClient.setCredentials(NULL, NULL);
  }
  _yuboxMQTT_default_clientid = YuboxWiFi.getMDNSHostname();
  _mqttClient.setClientId(_yuboxMQTT_default_clientid.c_str());

#if ASYNC_TCP_SSL_ENABLED
  // Anular cualquier certificado previamente establecido
  _mqttClient.setRootCa(NULL, 0);
  _mqttClient.setClientCert(NULL, 0, NULL, 0);
  _mqttClient.setPsk(NULL, NULL);
  _mqttClient.setSecure(false);

#define COND_FREE(TAG) \
  if ( TAG != NULL) free( TAG );\
  TAG = NULL;\
  TAG##_len = 0;

  COND_FREE(_rootCA)
  COND_FREE(_clientCert)
  COND_FREE(_clientKey)

  // TODO: implementar realmente mecanismo para seleccionar TLS
  if (_yuboxMQTT_port >= 8000) {
    log_i("Se negociará conexión ENCRIPTADA...");
    _mqttClient.setSecure(true);  // Encriptado, pero NO verificado hasta establecer certificados

    if (!_loadFile(MQTT_SERVER_CERT, _rootCA_len, _rootCA)) {
      log_w("No se ha cargado certificado servidor %s, conexión encriptada no verificará servidor!", MQTT_SERVER_CERT);
    } else {
      log_i("Cargado certificado de servidor %s", MQTT_SERVER_CERT);
      _mqttClient.setRootCa((const char *)_rootCA, _rootCA_len);

      if (_loadFile(MQTT_CLIENT_CERT, _clientCert_len, _clientCert) &&
          _loadFile(MQTT_CLIENT_KEY, _clientKey_len, _clientKey)) {
        log_w("Cargado certificado cliente %s y llave cliente %s",
          MQTT_CLIENT_CERT, MQTT_CLIENT_KEY);
        _mqttClient.setClientCert((const char *)_clientCert, _clientCert_len, (const char *)_clientKey, _clientKey_len);
      } else {
        log_w("No se ha cargado certificado cliente %s O llave cliente %s, conexión encriptada podría fallar en autenticarse!",
          MQTT_CLIENT_CERT, MQTT_CLIENT_KEY);
        COND_FREE(_clientCert)
        COND_FREE(_clientKey)
      }
    }
  } else {
    log_i("Se negociará conexión PLAINTEXT...");
  }
#endif
}

#if ASYNC_TCP_SSL_ENABLED
bool YuboxMQTTConfClass::_loadFile(const char * path, uint32_t &datalen, uint8_t * &dataptr)
{
  if (!SPIFFS.exists(path)) {
    log_w("Archivo %s no se encuentra, no se hace nada...", path);
    return false;
  }

  File f = SPIFFS.open(path);
  if (!f) {
    log_e("Archivo %s no puede abrirse!", path);
    return false;
  }

  auto readlen = f.size();
  log_i("Archivo %s tiene %u bytes, se intenta asignar memoria...", path, readlen);
  dataptr = (uint8_t *)malloc(readlen + 1); // Se requiere 1 caracter más para \0
  if (dataptr == NULL) {
    log_e("No hay memoria suficiente para leer archivo en memoria!");
    return false;
  }

  datalen = 0;
  uint8_t * p = dataptr;
  while (readlen > 0) {
    auto blocklen = (readlen > 512) ? 512 : readlen;
    auto r = f.read(p, blocklen);
    if (r < blocklen) {
      log_e("No se pudo leer bloque, pedido %d bytes, leídos %d bytes!", blocklen, r);
      f.close();
      free(dataptr); dataptr = NULL;
      datalen = 0;
      return false;
    }

    p += r;
    readlen -= r;
    datalen += r;
  }
  f.close();

  // Se requiere terminar el búfer con \0 para que mbedtls lo reconozca como PEM
  dataptr[datalen] = '\0';
  datalen++;

  return true;
}
#endif

void YuboxMQTTConfClass::_cbHandler_WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t)
{
  log_d("event: %d", event);
  switch(event) {
#if ESP_IDF_VERSION_MAJOR > 3
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#else
  case SYSTEM_EVENT_STA_GOT_IP:
#endif
      _connectMQTT();
      break;
#if ESP_IDF_VERSION_MAJOR > 3
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
#else
  case SYSTEM_EVENT_STA_DISCONNECTED:
#endif
      xTimerStop(_mqttReconnectTimer, 0);
      break;
    }
}

void _cb_YuboxMQTTConfClass_connectMQTT(TimerHandle_t timer)
{
  YuboxMQTTConfClass *self = (YuboxMQTTConfClass *)pvTimerGetTimerID(timer);
  return self->_connectMQTT();
}

void YuboxMQTTConfClass::_connectMQTT(void)
{
  if (_mqttClient.connected()) {
    log_i("Ya se está conectado a MQTT, no se hace nada.");
    return;
  }
  if (!_autoConnect) {
    log_i("Autoconexión desactivada, no se requiere conexión MQTT.");
    return;
  }
  if (_yuboxMQTT_host.length() <= 0) {
    log_e("No se puede conectar a MQTT - no se dispone de host broker MQTT.");
    return;
  }

  const char *p = _mqttClient.getClientId();
  log_i("Iniciando conexión a MQTT en %s:%u client-id %p[%s] ...",
    _yuboxMQTT_host.c_str(), _yuboxMQTT_port, p, p);
  _mqttClient.connect();
}

void YuboxMQTTConfClass::setAutoConnect(bool c)
{
  _autoConnect = c;
  if (WiFi.isConnected() && _autoConnect && !_mqttClient.connected()) {
    log_i("se intenta arrancar MQTT...");
    _connectMQTT();
  }
}

void YuboxMQTTConfClass::_cbHandler_onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  _lastdisconnect = reason;
  log_i("Disconnected from MQTT reason=%d", reason);

  if (WiFi.isConnected()) {
    log_i("Todavía hay WiFi conectado, se intenta reconectar...");
    if (!xTimerIsTimerActive(_mqttReconnectTimer) && pdPASS != xTimerStart(_mqttReconnectTimer, 0)) {
      log_e("No se puede iniciar el timer para reconexión de MQTT!");
    }
  }
}

void YuboxMQTTConfClass::_setupHTTPRoutes(AsyncWebServer & srv)
{
  srv.on("/yubox-api/mqtt/conf.json", HTTP_GET, std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/mqtt/conf.json", HTTP_POST, std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_POST, this, std::placeholders::_1));
}

void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(8));

  // Valores informativos, no pueden cambiarse vía web
  json_doc["want2connect"] = _autoConnect;
  if (_mqttClient.connected()) {
    json_doc["connected"] = true;
    json_doc["disconnected_reason"] = (char *)NULL;
  } else {
    json_doc["connected"] = false;
    json_doc["disconnected_reason"] = (uint8_t)_lastdisconnect;
  }
  json_doc["clientid"] = _mqttClient.getClientId();

  // Valores a cambiar vía web
  if (_yuboxMQTT_host.length() > 0) {
    json_doc["host"] = _yuboxMQTT_host.c_str();
    if (_yuboxMQTT_user.length() > 0) {
      json_doc["user"] = _yuboxMQTT_user.c_str();
      json_doc["pass"] = _yuboxMQTT_pass.c_str();
    } else {
      json_doc["user"] = (char *)NULL;
      json_doc["pass"] = (char *)NULL;
    }
  } else {
    json_doc["host"] = (char *)NULL;
    json_doc["user"] = (char *)NULL;
    json_doc["pass"] = (char *)NULL;
  }
  json_doc["port"] = _yuboxMQTT_port;

  serializeJson(json_doc, *response);
  request->send(response);
}

#define NVRAM_PUTSTRING(nvram, k, s) \
    (((s).length() > 0) ? ((nvram).putString((k), (s)) != 0) : ( (nvram).isKey((k)) ? (nvram).remove((k)) : true ))
void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  /* Se esperan en el POST los siguientes escenarios de parámetros:
    - Parámetro "host" siempre debe estar presente. El host debe consistir únicamente
      de componentes no-vacíos separados por puntos, que consistan únicamente de
      caracteres a-z o 0-9 y guión, y que no pueden empezar o terminar con guión.
    - Parámetro "user" es opcional. Si se pasa vacío o no está presente, se asume
      que el broker no tiene autenticación.
    - Parámetro "pass" es requerido ÚNICAMENTE si "user" está presente.
    - Parámetro "topic_location" siempre debe estar presente. Esta cadena debe ser
      no vacía, no debe empezar o terminar con espacios, y no debe contener el
      caracter "/". Pero se permite cualquier otro caracter (de lo que conozco del
      protocolo MQTT).
  */
  String n_host;
  String n_user;
  String n_pass;
  uint16_t n_port;

  n_host = _yuboxMQTT_host;
  n_user = _yuboxMQTT_user;
  n_pass = _yuboxMQTT_pass;
  n_port = _yuboxMQTT_port;

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  if (!clientError && !request->hasParam("host", true)) {
    clientError = true;
    responseMsg = "Se requiere host de broker MQTT";
  }
  if (!clientError) {
    p = request->getParam("host", true);
    n_host = p->value();
    n_host.toLowerCase();
    n_host.trim();
    if (n_host.length() <= 0) {
      clientError = true;
      responseMsg = "Se requiere host de broker MQTT";
    } else if (!_isValidHostname(n_host)) {
      clientError = true;
      responseMsg = "Nombre de host de broker MQTT no es válido";
    }
  }
  if (!clientError) {
    if (!request->hasParam("user", true)) {
      n_user = "";
    } else {
      p = request->getParam("user", true);
      n_user = p->value();
    }
    if (!request->hasParam("pass", true)) {
      n_pass = "";
    } else {
      p = request->getParam("pass", true);
      n_pass = p->value();
    }

    if (n_user.length() <= 0) {
      n_pass = "";
    } else if (n_pass.length() <= 0) {
      clientError = true;
      responseMsg = "Credenciales requieren contraseña no vacía";
    }
  }
  if (!clientError && request->hasParam("port", true)) {
    p = request->getParam("port", true);
    int n = sscanf(p->value().c_str(), "%hu", &n_port);\
    if (n <= 0) {
      clientError = true;
      responseMsg = "Formato numérico incorrecto para ";
      responseMsg += "port" ;
    }
  }

  // Si todos los parámetros son válidos, se intenta guardar en NVRAM
  if (!clientError) {
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_mqtt, false);

    if (!serverError && !nvram.putString("host", n_host)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: host";
    }
    if (!serverError && !nvram.putUShort("port", n_port)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: port";
    }
    if (!serverError && !NVRAM_PUTSTRING(nvram, "user", n_user)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: user";
    }
    if (!serverError && !NVRAM_PUTSTRING(nvram, "pass", n_pass)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: pass";
    }
    nvram.end();

    if (!serverError) {
      // Cerrar la conexión MQTT, si hay una previa, y volver a abrir
      if (_mqttClient.connected()) {
        log_i("Cerrando conexión previa de MQTT para refresco de credenciales...");
        _mqttClient.disconnect();
      }
      _loadSavedCredentialsFromNVRAM();
      if (WiFi.isConnected()) _connectMQTT();
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
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

bool YuboxMQTTConfClass::_isValidHostname(String & h)
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

YuboxMQTTConfClass YuboxMQTTConf;
