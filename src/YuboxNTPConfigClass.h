#ifndef _YUBOX_NTPDATE_CONF_CLASS_H_
#define _YUBOX_NTPDATE_CONF_CLASS_H_

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <NTPClient.h>

class YuboxNTPConfigClass
{
private:
  static const char * _ns_nvram_yuboxframework_ntpclient;

  // El cliente NTP que se administra con esta clase
  WiFiUDP _ntpUDP;
  String _ntpServerName;
  NTPClient * _ntpClient;
  long _ntpOffset;
  bool _ntpStart;
  bool _ntpValid;
  bool _ntpFirst;

  void _loadSavedCredentialsFromNVRAM(void);

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_ntpconfjson_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_ntpconfjson_POST(AsyncWebServerRequest *);

  bool _isValidHostname(String & h);

  void _cbHandler_WiFiEvent(WiFiEvent_t event);

public:
  YuboxNTPConfigClass(void);
  void begin(AsyncWebServer & srv);

  // Obtener referencia al cliente MQTT que se maneja, para agregar callbacks
  NTPClient & getNTPClient(void) { return *_ntpClient; }

  bool isNTPValid(void) { return _ntpValid; }

  // Función a llamar regularmente para actualizar el cliente NTPClient
  bool update(void);

  // Mantener separación entre hora local y hora UTC
  unsigned long getLocalTime(void) { return _ntpClient->getEpochTime(); }
  unsigned long getUTCTime(void) { return _ntpClient->getEpochTime() - _ntpOffset; }
};

extern YuboxNTPConfigClass YuboxNTPConf;

#endif
