#ifndef _YUBOX_WEB_AUTH_CLASS_H_
#define _YUBOX_WEB_AUTH_CLASS_H_

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <vector>

class YuboxWebAuthClass
{
private:
  static const char * _ns_nvram_yuboxframework_webauth;

  // Manejo de autenticación habilitada para este proyecto
  bool _enabledAuth;

  // Lista de manejadores de rutas que deben setearse usuario/clave
  std::vector<AsyncWebHandler*> _handlers;

  // Actualmente sólo se implementa un solo usuario estático. La clave puede
  // ser asignada libremente para este único usuario.
  String _username;
  String _password;

  void _loadSavedAuthsFromNVRAM(void);

  void _setupHTTPRoutes(AsyncWebServer &);
  void _updateCredentialsForHandlers(void);

  void _routeHandler_yuboxAPI_authconfig_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_authconfig_POST(AsyncWebServerRequest *);
  
public:
  YuboxWebAuthClass(void);
  bool isEnabled(void) { return _enabledAuth; }
  void setEnabled(bool);
  void begin(AsyncWebServer & srv);

  AsyncWebHandler& addManagedHandler(AsyncWebHandler* handler);
  bool removeManagedHandler(AsyncWebHandler* handler);

  String getUsername(void) { return _username; }
  String getPassword(void) { return _password; }
  bool savePassword(String);

  bool authenticate(AsyncWebServerRequest *);
};

extern YuboxWebAuthClass YuboxWebAuth;

#define YUBOX_RUN_AUTH(r) \
  do { if (!YuboxWebAuth.authenticate((r))) return (r)->requestAuthentication(); } while (0)

#endif
