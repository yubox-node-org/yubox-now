#ifndef YUBOX_DISABLE_INTERNAL_MQTT

#include "YuboxWiFiClass.h"
#include "YuboxMQTTConfClass.h"
#include "YuboxWebAuthClass.h"
#include "YuboxParamPOST.h"

#include <functional>

#include <Preferences.h>
#include <AsyncMqttClient.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#define YUBOX_MQTT_DEFAULT_INTERVAL_MSEC 30000

#if ASYNC_TCP_SSL_ENABLED

// Los archivos de certificados se guardan en la partición SPIFFS
#include <SPIFFS.h>

// Rutas en SPIFFS de archivos de certificados
static const char * MQTT_SERVER_CERT = "/mqtt-server.crt";
static const char * MQTT_CLIENT_CERT = "/mqtt-client.crt";
static const char * MQTT_CLIENT_KEY = "/mqtt-client.key";

static const char * MQTT_TMP_CERT = "/_tmp_tls_cert";
#define NUM_MQTT_CERTROLES 3
static const char * mqtt_certroles[NUM_MQTT_CERTROLES] = {
  "tls_servercert",
  "tls_clientcert",
  "tls_clientkey"
};

#endif

#define MQTT_PORT 1883

// Niveles de encriptación negociados vía MQTT
#define YUBOX_MQTT_SSL_NONE           0 // Sin encriptación (por omisión)
#define YUBOX_MQTT_SSL_INSECURE       1 // Conexión encriptada, pero sin verificación de certificados
#define YUBOX_MQTT_SSL_VERIFY_SERVER  2 // Conexión encriptada, verificar identidad de servidor
#define YUBOX_MQTT_SSL_VERIFY_CLIENT  3 // Conexión encriptada, verificar servidor, ofrecer identidad cliente

const char * YuboxMQTTConfClass::_ns_nvram_yuboxframework_mqtt = "YUBOX/MQTT";

void _cb_YuboxMQTTConfClass_connectMQTT(TimerHandle_t);
YuboxMQTTConfClass::YuboxMQTTConfClass(void)
{
  _autoConnect = false;
  _lastdisconnect = AsyncMqttClientDisconnectReason::TCP_DISCONNECTED;
  _yuboxMQTT_port = MQTT_PORT;
  _yuboxMQTT_ws = false;
  _yuboxMQTT_wsUri = "/";

  _mqtt_msec = YUBOX_MQTT_DEFAULT_INTERVAL_MSEC;
  _mqtt_min = 50;
  _mqtt_msec_changed = false;
  _mqtt_msec_changed_cb = NULL;

  _appDefaultPrefix = NULL;

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
  _tls_verifylevel = YUBOX_MQTT_SSL_NONE;

  _uploadRejected = false;
  _cert_clientError = false;
  _cert_serverError = false;
  _cert_responseMsg = "";
#endif
}

void YuboxMQTTConfClass::begin(AsyncWebServer & srv)
{
  WiFi.onEvent(std::bind(&YuboxMQTTConfClass::_cbHandler_WiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
  _loadSavedCredentialsFromNVRAM();
  _setupHTTPRoutes(srv);
}

void YuboxMQTTConfClass::setDefaultPublishPrefix(const char * p)
{
  _appDefaultPrefix = p;
}

String YuboxMQTTConfClass::buildPublishPrefix(void)
{
  return buildPublishPrefix(YuboxWiFi.getMDNSHostname());
}

String YuboxMQTTConfClass::buildPublishPrefix(String devId)
{
  String yuboxMQTT_topic_full;

  if (_yuboxMQTT_customPrefixSet) {
    yuboxMQTT_topic_full = _yuboxMQTT_customPrefix;
    yuboxMQTT_topic_full.replace("{YBXID}", devId);

    // Esto no debería pasar, pero si el prefijo personalizado está vacío,
    // se asigna el ID de dispositivo para que el tópico MQTT no esté vacío.
    if (yuboxMQTT_topic_full.isEmpty()) {
      yuboxMQTT_topic_full = devId;
    }
  } else {
    yuboxMQTT_topic_full = (_appDefaultPrefix == NULL) ? "" : _appDefaultPrefix;
    yuboxMQTT_topic_full += devId;
  }

  return yuboxMQTT_topic_full;
}

void YuboxMQTTConfClass::onMQTTInterval(YuboxMQTT_intervalchange_cb cb)
{
  _mqtt_msec_changed_cb = cb;
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
  _yuboxMQTT_ws = nvram.getBool("ws", false);
  _yuboxMQTT_wsUri = nvram.getString("wsuri", "/");
  _mqtt_msec = nvram.getUInt("mqttmsec", YUBOX_MQTT_DEFAULT_INTERVAL_MSEC);

  log_d("Host de broker MQTT......: %s:%u", _yuboxMQTT_host.c_str(), _yuboxMQTT_port);
  log_d("Usuario de broker MQTT...: %s", _yuboxMQTT_user.c_str());
  log_d("Clave de broker MQTT.....: %s", _yuboxMQTT_pass.c_str());
  if (!_yuboxMQTT_ws) {
    log_d("MQTT vía Websockets......: NO");
  } else {
    log_d("MQTT vía Websockets......: SÍ, %s", _yuboxMQTT_wsUri.c_str());
  }
  log_d("Intervalo recomendado envío payload MQTT: %u", _mqtt_msec);

  _mqttClient.setServer(_yuboxMQTT_host.c_str(), _yuboxMQTT_port);
  if (_yuboxMQTT_user.length() > 0) {
    _mqttClient.setCredentials(_yuboxMQTT_user.c_str(), _yuboxMQTT_pass.c_str());
  } else {
    _mqttClient.setCredentials(NULL, NULL);
  }
  _yuboxMQTT_default_clientid = YuboxWiFi.getMDNSHostname();
  _mqttClient.setClientId(_yuboxMQTT_default_clientid.c_str());
  _mqttClient.setWsEnabled(_yuboxMQTT_ws);
  if (_yuboxMQTT_ws) _mqttClient.setWsUri(_yuboxMQTT_wsUri.c_str());

  // Aquí se debe distinguir entre clave vacía y clave no asignada
  if (nvram.isKey("topic")) {
    _yuboxMQTT_customPrefix = nvram.getString("topic");
    _yuboxMQTT_customPrefixSet = true;
    log_d("Plantilla personalizada para tópico MQTT: [%s]", _yuboxMQTT_customPrefix.c_str());
  } else {
    log_d("No hay plantilla personalizada para tópico MQTT");
    _yuboxMQTT_customPrefixSet = false;
    _yuboxMQTT_customPrefix.clear();
  }

#if ASYNC_TCP_SSL_ENABLED
  _tls_verifylevel = nvram.getUChar("tlslevel", YUBOX_MQTT_SSL_NONE);

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

  if (_tls_verifylevel != YUBOX_MQTT_SSL_NONE) {
    log_i("Se negociará conexión ENCRIPTADA, nivel %hhu...", _tls_verifylevel);
    _mqttClient.setSecure(true);  // Encriptado, pero NO verificado hasta establecer certificados

    if (_tls_verifylevel == YUBOX_MQTT_SSL_INSECURE) {
      log_i("No se intentará cargar certificado alguno, conexión encriptada, no verificada...");
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

#if ASYNC_TCP_SSL_ENABLED
  if (_tls_verifylevel >= YUBOX_MQTT_SSL_VERIFY_SERVER) {
    // Es necesario cargar certificados. Está el sistema de archivos montado?
    if (!SPIFFS.exists("/manifest.txt")) {
      log_w("No se puede conectar a MQTT - sistema archivos no está montado para leer certificados.");

      if (WiFi.isConnected()) {
        log_i("Todavía hay WiFi conectado, se intenta reconectar...");
        if (!xTimerIsTimerActive(_mqttReconnectTimer) && pdPASS != xTimerStart(_mqttReconnectTimer, 0)) {
          log_e("No se puede iniciar el timer para reconexión de MQTT!");
        }
      }
      return;
    }

    if (_rootCA == NULL) {
      if (!_loadFile(MQTT_SERVER_CERT, _rootCA_len, _rootCA)) {
        log_w("No se ha cargado certificado servidor %s, conexión encriptada no verificará servidor!", MQTT_SERVER_CERT);
      } else {
        log_i("Cargado certificado de servidor %s", MQTT_SERVER_CERT);
        _mqttClient.setRootCa((const char *)_rootCA, _rootCA_len);

        if (_tls_verifylevel == YUBOX_MQTT_SSL_VERIFY_SERVER) {
          log_i("No se intentará cargar certificado de cliente, conexión encriptada, servidor verificado...");
        } else if (_clientCert == NULL || _clientKey == NULL) {
          if (_loadFile(MQTT_CLIENT_CERT, _clientCert_len, _clientCert) &&
              _loadFile(MQTT_CLIENT_KEY, _clientKey_len, _clientKey)) {
            log_i("Cargado certificado cliente %s y llave cliente %s, servidor+cliente verificado...",
              MQTT_CLIENT_CERT, MQTT_CLIENT_KEY);
            _mqttClient.setClientCert((const char *)_clientCert, _clientCert_len, (const char *)_clientKey, _clientKey_len);
          } else {
            log_w("No se ha cargado certificado cliente %s O llave cliente %s, conexión encriptada podría fallar en autenticarse!",
              MQTT_CLIENT_CERT, MQTT_CLIENT_KEY);
            COND_FREE(_clientCert)
            COND_FREE(_clientKey)
          }
        }
      }
    }
  }
#endif

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
  srv.on("/yubox-api/mqtt/conf.json", HTTP_DELETE, std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_DELETE, this, std::placeholders::_1));
#if ASYNC_TCP_SSL_ENABLED
  srv.on("/yubox-api/mqtt/tls_servercert", HTTP_POST,
    std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_POST, this, std::placeholders::_1),
    std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_handleUpload, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
  srv.on("/yubox-api/mqtt/tls_clientcert", HTTP_POST,
    std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_POST, this, std::placeholders::_1),
    std::bind(&YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_handleUpload, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
#endif
}

void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  AsyncResponseStream *response = request->beginResponseStream("application/json");
#if ARDUINOJSON_VERSION_MAJOR <= 6
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(17));
#else
  JsonDocument json_doc;
#endif

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
  json_doc["mqttmsec"] = getRequestedMQTTInterval();

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

  json_doc["ws"] = _yuboxMQTT_ws;
  json_doc["wsuri"] = _yuboxMQTT_wsUri.c_str();

  json_doc["topic_capable"] = (_appDefaultPrefix != NULL);
  if (_yuboxMQTT_customPrefixSet) {
    json_doc["topic"] = _yuboxMQTT_customPrefix.c_str();
  } else {
    json_doc["topic"] = (const char *)NULL;
  }

  // Valores por omisión para cuando no hay TLS
  json_doc["tls_capable"] = false;
  json_doc["tls_verifylevel"] = YUBOX_MQTT_SSL_NONE;
  json_doc["tls_servercert"] = false;
  json_doc["tls_clientcert"] = false;
#if ASYNC_TCP_SSL_ENABLED
  // Valores concernientes a TLS
  json_doc["tls_capable"] = true;
  json_doc["tls_verifylevel"] = _tls_verifylevel;
  json_doc["tls_servercert"] = SPIFFS.exists(MQTT_SERVER_CERT);
  json_doc["tls_clientcert"] = (SPIFFS.exists(MQTT_CLIENT_CERT) && SPIFFS.exists(MQTT_CLIENT_KEY));
#endif

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
  bool n_ws;
  String n_wsUri;
  uint32_t n_mqttmsec;
  String n_topic;

  n_host = _yuboxMQTT_host;
  n_user = _yuboxMQTT_user;
  n_pass = _yuboxMQTT_pass;
  n_port = _yuboxMQTT_port;
  n_ws = _yuboxMQTT_ws;
  n_wsUri = _yuboxMQTT_wsUri;
  n_mqttmsec = _mqtt_msec;
  n_topic = _yuboxMQTT_customPrefix;

#if ASYNC_TCP_SSL_ENABLED
  uint8_t n_tls_verifylevel = _tls_verifylevel;
#endif

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  YBX_ASSIGN_STR_FROM_POST(host, "host de broker MQTT", (YBX_POST_VAR_REQUIRED|YBX_POST_VAR_NONEMPTY|YBX_POST_VAR_TRIM), n_host)
  if (!clientError) {
    n_host.toLowerCase();
    if (!_isValidHostname(n_host)) {
      clientError = true;
      responseMsg = "Nombre de host de broker MQTT no es válido";
    }
  }

  YBX_ASSIGN_STR_FROM_POST(user, "usuario autenticación", (YBX_POST_VAR_TRIM|YBX_POST_VAR_BLANK), n_user)
  YBX_ASSIGN_STR_FROM_POST(pass, "clave autenticación", (YBX_POST_VAR_TRIM | ((n_user.length() > 0) ? (YBX_POST_VAR_REQUIRED|YBX_POST_VAR_NONEMPTY) : YBX_POST_VAR_BLANK )), n_pass)
  if (!clientError && n_user.length() <= 0) {
    n_pass.clear();
  }

#define ASSIGN_FROM_POST(TAG, FMT) YBX_ASSIGN_NUM_FROM_POST(TAG, NULL, FMT, 0, (n_##TAG))

  ASSIGN_FROM_POST(port, "%hu")
#if ASYNC_TCP_SSL_ENABLED
  ASSIGN_FROM_POST(tls_verifylevel, "%hhu")
  if (!clientError && n_tls_verifylevel > YUBOX_MQTT_SSL_VERIFY_CLIENT) {
    n_tls_verifylevel = YUBOX_MQTT_SSL_VERIFY_CLIENT;
  }
#endif

  ASSIGN_FROM_POST(mqttmsec, "%u")
  if (!clientError && n_mqttmsec <= _mqtt_min) {
    clientError = true;
    responseMsg = "Intervalo MQTT demasiado corto. Mínimo es ";
    responseMsg += (_mqtt_min / 1000.0);
    responseMsg += " segundos.";
  }

  if (!clientError) {
    if (request->hasParam("ws", true)) {
      p = request->getParam("ws", true);
      n_ws = (p->value() != "0");
    }
    if (n_ws) {
      YBX_ASSIGN_STR_FROM_POST(wsuri, "URI WebSocket", YBX_POST_VAR_TRIM, n_wsUri)
      if (!n_wsUri.startsWith("/")) {
        clientError = true;
        responseMsg = "URI Websocket inválido (no inicia con \"/\")";
      }
    }
  }


  // Sólo considerar tópico personalizado si proyecto ha sido actualizado para lidiar con él.
  if (!clientError && _appDefaultPrefix != NULL) {
    YBX_ASSIGN_STR_FROM_POST(topic, "plantilla tópico MQTT", YBX_POST_VAR_TRIM, n_topic)
    if (n_topic.length() > 0 && !_isValidMQTTTopic(n_topic)) {
      clientError = true;
      responseMsg = "Plantilla para tópico MQTT no es válida";
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
    if (!serverError && !nvram.putUInt("mqttmsec", n_mqttmsec)) {
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
    if (!serverError && !nvram.putBool("ws", n_ws)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: ws";
    }
    if (!serverError && !nvram.putString("wsuri", n_wsUri)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: wsuri";
    }
    if (!serverError && !NVRAM_PUTSTRING(nvram, "topic", n_topic)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: topic";
    }
#if ASYNC_TCP_SSL_ENABLED
    if (!serverError && !nvram.putUChar("tlslevel", n_tls_verifylevel)) {
      serverError = true;
      responseMsg = "No se puede guardar valor para clave: tlslevel";
    }
#endif
    nvram.end();

    if (!serverError) {
      if (_mqtt_msec != n_mqttmsec) _mqtt_msec_changed = true;

      // Cerrar la conexión MQTT, si hay una previa, y volver a abrir
      if (_mqttClient.connected()) {
        log_i("Cerrando conexión previa de MQTT para refresco de credenciales...");
        _mqttClient.disconnect();
      }
      _loadSavedCredentialsFromNVRAM();
      if (WiFi.isConnected()) _connectMQTT();

      if (_mqtt_msec_changed) {
        _mqtt_msec_changed = false;
        if (_mqtt_msec_changed_cb != NULL) _mqtt_msec_changed_cb();
      }
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }

  YBX_STD_RESPONSE
}

void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqttconfjson_DELETE(AsyncWebServerRequest * request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";

  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_mqtt, false);
  serverError = !nvram.clear();
  nvram.end();

  if (!serverError) {
    // Cerrar la conexión MQTT, si hay una previa, y volver a abrir
    if (_mqttClient.connected()) {
      log_i("Cerrando conexión previa de MQTT para refresco de credenciales...");
      _mqttClient.disconnect();
    }
    _loadSavedCredentialsFromNVRAM();
    if (WiFi.isConnected()) _connectMQTT();
  } else {
    responseMsg = "No se pueden borrar configuraciones de MQTT";
  }

  if (!clientError && !serverError) {
    request->send(204);
  } else {
    YBX_STD_RESPONSE
  }
}

#if ASYNC_TCP_SSL_ENABLED

void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_handleUpload(AsyncWebServerRequest * request,
  String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  log_v("filename=%s index=%d data=%p len=%d final=%d",
    filename.c_str(), index, data, len, final ? 1 : 0);

  if (index == 0 && !_uploadRejected) {
    /* La macro YUBOX_RUN_AUTH no es adecuada porque requestAuthentication() no puede llamarse
       aquí - vienen más fragmentos del upload. Se debe rechazar el upload si la autenticación
       ha fallado.
     */
    if (!YuboxWebAuth.authenticate((request))) {
      // Credenciales incorrectas
      _uploadRejected = true;
    } else {
      // Resolver archivo temporal de posible iteración anterior
      if (!_resolveTempFileName(request)) {
        _rejectUpload("Error al renombrar archivos temporales", true);
      } else {
        // Abrir nueva instancia de archivo temporal
        log_d("Abriendo archivo temporal: %s ...", MQTT_TMP_CERT);
        _tmpCert = SPIFFS.open(MQTT_TMP_CERT, FILE_WRITE);
        if (!_tmpCert) {
          log_e("Fallo al abrir nuevo archivo temporal %s", _tmpCert);
          _rejectUpload("Error al crear archivo temporal", true);
        } else {
          log_d("Archivo temporal %s abierto correctamente...", MQTT_TMP_CERT);
        }
      }
    }
  }

  if (_uploadRejected) return;

  // Escribir todo el contenido recibido en el archivo temporal
  while (len > 0 && !_uploadRejected) {
    if (!_tmpCert) {
      log_e("Objeto archivo rechaza escritura! %s", MQTT_TMP_CERT);
      _rejectUpload("Fallo al escribir archivo temporal (2)", true);
    } else {
      auto r = _tmpCert.write(data, len);
      if (r <= 0) {
        log_e("Fallo al escribir %d bytes desde %p en %s offset %d: %d", len, data, MQTT_TMP_CERT, index, r);
        _tmpCert.close();
        SPIFFS.remove(MQTT_TMP_CERT);
        _rejectUpload("Fallo al escribir archivo temporal", true);
      } else {
        index += r;
        data += r;
        len -= r;
        log_d("Escritos %d bytes...", r);
      }
    }
  }

  if (final) {
    // Cerrar el archivo temporal, y dejarlo para siguiente iteración
    _tmpCert.close();
  }
}

bool YuboxMQTTConfClass::_resolveTempFileName(AsyncWebServerRequest * request)
{
  if (!SPIFFS.exists(MQTT_TMP_CERT)) {
    log_d("Archivo temporal %s no existe, no se hace nada", MQTT_TMP_CERT);
    return true;
  }

  /* Para cada rol de certificado, se arma el archivo temporal que debe existir
     para este certificado. Si el parámetro de request EXISTE, pero el
     correspondiente archivo NO existe, se asume que el archivo temporal
     corresponde a este archivo, y se renombra.
   */
  char buf[32];
  for (auto i = 0; i < NUM_MQTT_CERTROLES; i++) {
    if (!request->hasParam(mqtt_certroles[i], true, true)) continue;
    snprintf(buf, sizeof(buf), "/_tmp_%s", mqtt_certroles[i]);
    if (!SPIFFS.exists(buf)) {
      log_d("Renombrando %s --> %s ...", MQTT_TMP_CERT, buf);
      if (!SPIFFS.rename(MQTT_TMP_CERT, buf)) {
        log_e("no se pudo renombrar %s --> %s ...", MQTT_TMP_CERT, buf);
        return false;
      }
      return true;
    }
  }

  return true;
}

bool YuboxMQTTConfClass::_renameFinalTempFile(const char * tmpname, const char * finalname)
{
  log_d("se requiere renombrar %s --> %s ...", tmpname, finalname);
  if (!SPIFFS.exists(tmpname)) {
    log_e("no se encuentra archivo temporal esperado %s !", tmpname);
    return false;
  }
  if (SPIFFS.exists(finalname)) {
    log_d("archivo final %s ya existía, se ELIMINA...", finalname);
    SPIFFS.remove(finalname);
  }
  log_d("Renombrando %s --> %s ...", tmpname, finalname);
  if (!SPIFFS.rename(tmpname, finalname)) {
    log_e("no se pudo renombrar %s --> %s ...", tmpname, finalname);
    return false;
  }
  return true;
}

void YuboxMQTTConfClass::_cleanupTempFiles(void)
{
  char buf[32];
  for (auto i = 0; i < NUM_MQTT_CERTROLES; i++) {
    snprintf(buf, sizeof(buf), "/_tmp_%s", mqtt_certroles[i]);
    if (SPIFFS.exists(buf)) SPIFFS.remove(buf);
  }
  if (SPIFFS.exists(MQTT_TMP_CERT)) SPIFFS.remove(MQTT_TMP_CERT);
}

void YuboxMQTTConfClass::_routeHandler_yuboxAPI_mqtt_certupload_POST(AsyncWebServerRequest * request)
{
  /* La macro YUBOX_RUN_AUTH no es adecuada aquí porque el manejador de upload se ejecuta primero
     y puede haber rechazado el upload. La bandera de rechazo debe limpiarse antes de rechazar la
     autenticación.
   */
  if (!YuboxWebAuth.authenticate((request))) {
    _uploadRejected = false;
    _cleanupTempFiles();
    return (request)->requestAuthentication();
  }

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";

  // Resolver archivo temporal de procesamiento upload
  if (!_resolveTempFileName(request)) {
    _rejectUpload("Error al renombrar archivos temporales", true);
  } else {
    if (request->hasParam("tls_clientcert", true, true) && request->hasParam("tls_clientkey", true, true)) {
      // Actualización de certificado cliente
    } else if (request->hasParam("tls_servercert", true, true)) {
      // Actualización de certificado servidor
    } else {
      clientError = true;
      responseMsg = "No se ha especificado archivo de certificado.";
    }

    // TODO: validar que los archivos subidos son efectivamente certificados PEM

    if (!clientError) clientError = _cert_clientError;
    if (!serverError) serverError = _cert_serverError;
    if (responseMsg == "") responseMsg = _cert_responseMsg;

    if (!clientError && !serverError) {
      // TODO: insertar aquí manipulación de archivos temporales
      if (request->hasParam("tls_clientcert", true, true)) {
        // Actualización de certificado cliente
        if (!_renameFinalTempFile("/_tmp_tls_clientcert", MQTT_CLIENT_CERT)) {
          serverError = true;
          responseMsg = "No se puede renombrar archivo temporal para: ";
          responseMsg += MQTT_CLIENT_CERT;
        } else if (!_renameFinalTempFile("/_tmp_tls_clientkey", MQTT_CLIENT_KEY)) {
          serverError = true;
          responseMsg = "No se puede renombrar archivo temporal para: ";
          responseMsg += MQTT_CLIENT_KEY;
        }
      } else if (request->hasParam("tls_servercert", true, true)) {
        // Actualización de certificado servidor
        if (!_renameFinalTempFile("/_tmp_tls_servercert", MQTT_SERVER_CERT)) {
          serverError = true;
          responseMsg = "No se puede renombrar archivo temporal para: ";
          responseMsg += MQTT_SERVER_CERT;
        }
      }

      if (!serverError) responseMsg = "Certificado actualizado correctamente.";
    }
  }

  _cleanupTempFiles();

  _uploadRejected = false;
  _cert_clientError = false;
  _cert_serverError = false;
  _cert_responseMsg = "";

  YBX_STD_RESPONSE
}

void YuboxMQTTConfClass::_rejectUpload(const String & s, bool serverError)
{
  if (!_cert_serverError && !_cert_clientError) {
    if (serverError) _cert_serverError = true; else _cert_clientError = true;
  }
  if (_cert_responseMsg.isEmpty()) _cert_responseMsg = s;
  _uploadRejected = true;
}

void YuboxMQTTConfClass::_rejectUpload(const char * msg, bool serverError)
{
  String s = msg;
  _rejectUpload(s, serverError);
}


#endif

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

bool YuboxMQTTConfClass::_isValidMQTTTopic(String & topic)
{
  // Tópico vacío está explícitamente validado porque indica resetear a default
  if (topic.isEmpty()) return true;

  // Los tópicos que inician con '$' son reservados
  if (topic.charAt(0) == '$') return false;

  // El tópico no puede empezar con barra inclinada
  if (topic.charAt(0) == '/') return false;

  int topicLevelStart = 0;
  do {
    int nextSlash = topic.indexOf('/', topicLevelStart);
    String topicLevel = (nextSlash < 0) ? topic.substring(topicLevelStart) : topic.substring(topicLevelStart, nextSlash);

    // El nivel de tópico debe ser no-vacío
    if (topicLevel.isEmpty()) return false;

    // El nivel de tópico no puede contener: +#
    const char * blacklist = "+#";
    for (const char * chr = blacklist; *chr != '\0'; chr++) {
      if (-1 != topicLevel.indexOf(*chr)) return false;
    }

    topicLevelStart = (nextSlash < 0) ? -1 : nextSlash + 1;
  } while (topicLevelStart >= 0);

  return true;
}

YuboxMQTTConfClass YuboxMQTTConf;
#endif