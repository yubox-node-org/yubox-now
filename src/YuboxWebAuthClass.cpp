#include "YuboxWebAuthClass.h"
#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

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
  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_webauth, true);
  String usr = nvram.getString("usr/1/usr");
  if (!usr.isEmpty()) {
    _username = usr;
  }
  String pwd = nvram.getString("usr/1/pwd");
  if (!pwd.isEmpty()) {
    _password = pwd;
  }
}

bool YuboxWebAuthClass::savePassword(String pwd)
{
  if (pwd.isEmpty()) return false;
  _password = pwd;

  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_webauth, false);
  nvram.putInt("usr/n", 1);
  nvram.putString("usr/1/usr", _username);
  nvram.putString("usr/1/pwd", _password);

  _updateCredentialsForHandlers();
  return true;
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
  srv.on("/yubox-api/authconfig", HTTP_GET, std::bind(&YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/authconfig", HTTP_POST, std::bind(&YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_POST, this, std::placeholders::_1));
}

void YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;

  json_doc["username"] = _username.c_str();
  json_doc["password"] = _password.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxWebAuthClass::_routeHandler_yuboxAPI_authconfig_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  String password[2];

  if (!clientError) {
    if (!request->hasParam("password1", true)) {
      clientError = true;
      responseMsg = "No se encuentra contraseña";
    } else {
      p = request->getParam("password1", true);
      password[0] = p->value();
    }
  }
  if (!clientError) {
    if (!request->hasParam("password2", true)) {
      clientError = true;
      responseMsg = "No se encuentra confirmación de contraseña";
    } else {
      p = request->getParam("password2", true);
      password[1] = p->value();
    }
  }
  if (!clientError && password[0] != password[1]) {
    clientError = true;
    responseMsg = "Contraseña y confirmación no coinciden.";
  }
  if (!clientError && password[0].isEmpty()) {
    clientError = true;
    responseMsg = "Contraseña no puede estar vacía.";
  }

  // Guardar la contraseña validada
  if (!clientError) {
    serverError = !savePassword(password[0]);
    if (serverError) {
      responseMsg = "No se puede guardar nueva contraseña!";
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Contraseña cambiada correctamente.";
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

YuboxWebAuthClass YuboxWebAuth;
