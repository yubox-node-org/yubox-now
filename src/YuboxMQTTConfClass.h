#ifndef _YUBOX_MQTT_CONF_CLASS_H_
#define _YUBOX_MQTT_CONF_CLASS_H_

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <AsyncMqttClient.h>

class YuboxMQTTConfClass
{
private:
  static const char * _ns_nvram_yuboxframework_mqtt;

  // El cliente MQTT que se administra con esta clase
  AsyncMqttClient _mqttClient;

  // Bandera de si de debe iniciar automáticamente la conexión si se tienen credenciales
  // y se detecta conexión de red establecida.
  bool _autoConnect;
  TimerHandle_t _mqttReconnectTimer;
  AsyncMqttClientDisconnectReason _lastdisconnect;

  // Host y credenciales para conexión MQTT cargadas desde NVRAM
  String _yuboxMQTT_host;
  String _yuboxMQTT_user;
  String _yuboxMQTT_pass;
  uint16_t _yuboxMQTT_port;

  String _yuboxMQTT_default_clientid;

#if ASYNC_TCP_SSL_ENABLED
  uint32_t _rootCA_len; uint8_t * _rootCA;
  uint32_t _clientCert_len; uint8_t * _clientCert;
  uint32_t _clientKey_len; uint8_t * _clientKey;
  uint8_t _tls_verifylevel;

  // Archivo temporal que se está escribiendo
  File _tmpCert;

  // Rechazar el resto de los fragmentos de upload si primera revisión falla
  bool _uploadRejected;

  // Descripción de errores posibles al subir el tar.gz
  bool _cert_clientError;
  bool _cert_serverError;
  String _cert_responseMsg;

  bool _loadFile(const char * path, uint32_t &datalen, uint8_t * &dataptr);
  void _rejectUpload(const String &, bool serverError);
  void _rejectUpload(const char *, bool serverError);
  void _routeHandler_yuboxAPI_mqtt_certupload_POST(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_mqtt_certupload_handleUpload(AsyncWebServerRequest *,
    String filename, size_t index, uint8_t *data, size_t len, bool final);
  bool _resolveTempFileName(AsyncWebServerRequest *);
  bool _renameFinalTempFile(const char * tmpname, const char * finalname);
  void _cleanupTempFiles(void);
#endif

  void _loadSavedCredentialsFromNVRAM(void);

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_mqttconfjson_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_mqttconfjson_POST(AsyncWebServerRequest *);

  bool _isValidHostname(String & h);

  void _cbHandler_WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t);
  void _cbHandler_onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
  void _connectMQTT(void);

public:
  YuboxMQTTConfClass(void);
  void begin(AsyncWebServer & srv);

  // Si el autoconnect está activo, la conexión MQTT se reintentará si se
  // pierde la conexión, en el momento en que se detecte que volvió.
  // Por omisión está apagada.
  bool getAutoConnect(void) { return _autoConnect; }
  void setAutoConnect(bool);

  // Obtener referencia al cliente MQTT que se maneja, para agregar callbacks
  AsyncMqttClient & getMQTTClient(void) { return _mqttClient; }

  friend void _cb_YuboxMQTTConfClass_connectMQTT(TimerHandle_t);
};

extern YuboxMQTTConfClass YuboxMQTTConf;

#endif
