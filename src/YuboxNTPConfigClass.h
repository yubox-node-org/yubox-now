#ifndef _YUBOX_NTPDATE_CONF_CLASS_H_
#define _YUBOX_NTPDATE_CONF_CLASS_H_

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include "time.h"

class YuboxNTPConfigClass
{
private:
  static const char * _ns_nvram_yuboxframework_ntpclient;
  static volatile bool _ntpValid;

  // El cliente NTP que se administra con esta clase
  String _ntpServerName;
  long _ntpOffset;
  bool _ntpStart;
  bool _ntpFirst;

  // Si el proyecto dispone de RTC, aquí se almacena el valor de unixtime
  // generado a partir del RTC hasta hacer funcionar el NTP.
  uint32_t _rtcHint;

  uint32_t _getSketchCompileTimestamp(void);

  void _loadSavedCredentialsFromNVRAM(void);
  void _configTime(void);

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_ntpconfjson_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_ntpconfjson_POST(AsyncWebServerRequest *);

  bool _isValidHostname(String & h);

  void _cbHandler_WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t);

public:
  YuboxNTPConfigClass(void);

  void setRTCHint(uint32_t t) { _rtcHint = t; }

  void begin(AsyncWebServer & srv);

  bool isNTPValid(uint32_t ms_timeout = 1000);

  // Función a llamar regularmente para actualizar el cliente NTPClient
  bool update(uint32_t ms_timeout = 1000);

  // Mantener separación entre hora local y hora UTC
  unsigned long getLocalTime(void);
  unsigned long getUTCTime(void);

  // NO LLAMAR DESDE APLICACIÓN
  void _sntp_sync_time_cb(struct timeval *);
};

extern YuboxNTPConfigClass YuboxNTPConf;

#endif
