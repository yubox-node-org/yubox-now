#include "YuboxWebAuthClass.h"
#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <functional>

const char * YuboxWebAuthClass::_ns_nvram_yuboxframework_webauth = "YUBOX/Auth";

YuboxWebAuthClass::YuboxWebAuthClass(void)
{
  _enabledAuth = false;
  _username = "admin";
  _password = "yubox";
}

void YuboxWebAuthClass::setEnabled(bool n)
{
  _enabledAuth = n;
  _updateCredentialsForHandlers();
}

void YuboxWebAuthClass::begin(AsyncWebServer & srv)
{
  _loadSavedAuthsFromNVRAM();
  _updateCredentialsForHandlers();
  _setupHTTPRoutes(srv);
}

void YuboxWebAuthClass::_loadSavedAuthsFromNVRAM(void)
{
  // TODO: implementar
}

void YuboxWebAuthClass::_updateCredentialsForHandlers(void)
{
  for (auto i = 0; i < _handlers.size(); i++) {
    if (_enabledAuth) {
      _handlers[i]->setAuthentication(_username.c_str(), _password.c_str());
    } else {
      _handlers[i]->setAuthentication("", "");
    }
  }
}

AsyncWebHandler& YuboxWebAuthClass::addManagedHandler(AsyncWebHandler* handler)
{
  for (auto i = 0; i < _handlers.size(); i++) {
    if (_handlers[i] == handler) return *handler;
  }
  _handlers.push_back(handler);
  if (_enabledAuth) {
    handler->setAuthentication(_username.c_str(), _password.c_str());
  } else {
    handler->setAuthentication("", "");
  }
  return *handler;
}

bool YuboxWebAuthClass::removeManagedHandler(AsyncWebHandler* handler)
{
  int idx = -1;
  for (auto i = 0; i < _handlers.size(); i++) {
    if (_handlers[i] == handler) {
      idx = i;
      break;
    }
  }
  if (idx == -1) return false;

  // Manipular el vector de elementos para compactar
  if (idx < _handlers.size() - 1) {
    // Copiar último elemento encima del que se elimina
    _handlers[idx] = _handlers[_handlers.size() - 1];
  }
  _handlers.pop_back();

  return true;
}

bool YuboxWebAuthClass::authenticate(AsyncWebServerRequest * request)
{
  if (!_enabledAuth) return true;
  return request->authenticate(_username.c_str(), _password.c_str());
}

void YuboxWebAuthClass::_setupHTTPRoutes(AsyncWebServer & srv)
{
  //srv.on("/yubox-api/authconfig", HTTP_GET, std::bind(&YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_GET, this, std::placeholders::_1));
  //srv.on("/yubox-api/authconfig", HTTP_POST, std::bind(&YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_POST, this, std::placeholders::_1));
}

YuboxWebAuthClass YuboxWebAuth;
