#ifndef _YUBOX_OTA_CLASS_H_
#define _YUBOX_OTA_CLASS_H_

#include <ESPAsyncWebServer.h>
#include "YuboxWebAuthClass.h"

#include "uzlib/uzlib.h"

class YuboxOTAClass
{
private:
  // Rechazar el resto de los fragmentos de upload si primera revisión falla
  bool _uploadRejected;

  struct uzlib_uncomp _uzLib_decomp;
  unsigned char * _gz_srcdata;
  unsigned char * _gz_dstdata;
  unsigned char * _gz_dict;
  unsigned long _gz_actualExpandedSize;
  bool _gz_headerParsed;
  
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
