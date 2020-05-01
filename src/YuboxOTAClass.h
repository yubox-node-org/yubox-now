#ifndef _YUBOX_OTA_CLASS_H_
#define _YUBOX_OTA_CLASS_H_

#include <ESPAsyncWebServer.h>
#include "YuboxWebAuthClass.h"

class YuboxOTAClass
{
  // Rechazar el resto de los fragmentos de upload si primera revisión falla
  bool _uploadRejected;
  
private:
  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_yuboxOTA_tgzupload_POST(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload(AsyncWebServerRequest *,
    String filename, size_t index, uint8_t *data, size_t len, bool final);
  
public:
  YuboxOTAClass(void);
  void begin(AsyncWebServer & srv);
};

extern YuboxOTAClass YuboxOTA;

#endif
