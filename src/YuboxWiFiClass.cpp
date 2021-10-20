#include "YuboxWiFiClass.h"
#include "esp_wpa2.h"
#include <ESPmDNS.h>
#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#include <functional>

#include "Yubox_internal_LastParamRewrite.hpp"

const char * YuboxWiFiClass::_ns_nvram_yuboxframework_wifi = "YUBOX/WiFi";
void _cb_YuboxWiFiClass_wifiRescan(TimerHandle_t);

// En ciertos escenarios, el re-escaneo de WiFi cada 2 segundos puede interferir
// con tareas de la aplicación. Para mitigar esto, se escanea cada 30 segundos,
// a menos que haya al menos un cliente SSE que requiere configurar la red WiFi.
#define WIFI_RESCAN_FAST  8000
#define WIFI_RESCAN_SLOW  30000

YuboxWiFiClass::YuboxWiFiClass(void)
{
  String tpl = "YUBOX-{MAC}";

  _assumeControlOfWiFi = true;
  _enableSoftAP = true;
  _softAPConfigured = false;

  _pWebSrvBootstrap = NULL;
  _eventId_cbHandler_WiFiEvent = 0;
  _eventId_cbHandler_WiFiEvent_ready = 0;
  _wifiReadyEventReceived = false;
  _pEvents = NULL;
  _disconnectBeforeRescan = false;
  _interval_wifiRescan = WIFI_RESCAN_SLOW;
  _timer_wifiRescan = xTimerCreate(
    "YuboxWiFiClass_wifiRescan",
    pdMS_TO_TICKS(_interval_wifiRescan),
    pdFALSE,
    (void*)this,
    &_cb_YuboxWiFiClass_wifiRescan);

  WiFi.persistent(false);

  _scannedNetworks_timestamp = 0;
  setMDNSHostname(tpl);
  setAPName(tpl);
}

void YuboxWiFiClass::setMDNSHostname(String & tpl)
{
  _mdnsName = tpl;
  _mdnsName.replace("{MAC}", _getWiFiMAC());
}

void YuboxWiFiClass::setAPName(String & tpl)
{
  _apName = tpl;
  _apName.replace("{MAC}", _getWiFiMAC());
}

String YuboxWiFiClass::_getWiFiMAC(void)
{
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac;
}

void YuboxWiFiClass::begin(AsyncWebServer & srv)
{
  _loadSavedNetworksFromNVRAM();
  _setupHTTPRoutes(srv);

  // SIEMPRE instalar el manejador de evento de WiFi listo
  _eventId_cbHandler_WiFiEvent_ready = WiFi.onEvent(
    std::bind(&YuboxWiFiClass::_cbHandler_WiFiEvent_ready, this, std::placeholders::_1, std::placeholders::_2),
#if ESP_IDF_VERSION_MAJOR > 3
    ARDUINO_EVENT_WIFI_READY
#else
    SYSTEM_EVENT_WIFI_READY
#endif
    );

  // SIEMPRE instalar el manejador de escaneo, para reportar incluso
  // si hay otro controlando el WiFi. El manejador evita tocar estado
  // global del WiFi si no está en control del WiFi
  WiFi.onEvent(
    std::bind(&YuboxWiFiClass::_cbHandler_WiFiEvent_scandone, this, std::placeholders::_1, std::placeholders::_2),
#if ESP_IDF_VERSION_MAJOR > 3
    ARDUINO_EVENT_WIFI_SCAN_DONE
#else
    SYSTEM_EVENT_SCAN_DONE
#endif
    );

  if (_assumeControlOfWiFi) {
    takeControlOfWiFi();
  }
}

void YuboxWiFiClass::takeControlOfWiFi(void)
{
  _assumeControlOfWiFi = true;
  _eventId_cbHandler_WiFiEvent = WiFi.onEvent(
    std::bind(&YuboxWiFiClass::_cbHandler_WiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
  _startWiFi();
}

void YuboxWiFiClass::releaseControlOfWiFi(bool wifioff)
{
  log_i("Cediendo control del WiFi (WiFi %s)...", wifioff ? "OFF" : "ON");

  _assumeControlOfWiFi = false;
  if (_eventId_cbHandler_WiFiEvent) WiFi.removeEvent(_eventId_cbHandler_WiFiEvent);
  _eventId_cbHandler_WiFiEvent = 0;
  if (WiFi.status() != WL_DISCONNECTED) {
    log_i("Desconectando del WiFi (STA)...");
    WiFi.disconnect(wifioff);
  }
  if (xTimerIsTimerActive(_timer_wifiRescan)) {
    xTimerStop(_timer_wifiRescan, 0);
  }
  _disconnectBeforeRescan = false;
  log_i("Desconectando del WiFi (AP)...");
  WiFi.softAPdisconnect(wifioff);

  _softAPConfigured = false;
  if (wifioff) WiFi.mode(WIFI_OFF);
}

void YuboxWiFiClass::_startCondRescanTimer(bool disconn)
{
  bool needFastScan = (_pEvents != NULL && _pEvents->count() > 0);
  if (disconn) _disconnectBeforeRescan = true;
  log_v("se requiere escaneo WiFi %s", needFastScan ? "RAPIDO" : "LENTO");
  if (xTimerIsTimerActive(_timer_wifiRescan)) {
    log_v("timer escaneo WiFi ACTIVO");
    if (_interval_wifiRescan == (needFastScan ? WIFI_RESCAN_FAST : WIFI_RESCAN_SLOW)) {
      log_v("(no se hace nada, escaneo actual es %s)", needFastScan ? "RAPIDO" : "LENTO");
    } else {
      log_v("detenido timer de escaneo WiFi");
      xTimerStop(_timer_wifiRescan, 0);

      log_v("se cambia e inicia escaneo de WiFi %s", needFastScan ? "RAPIDO" : "LENTO");
      _interval_wifiRescan = needFastScan ? WIFI_RESCAN_FAST : WIFI_RESCAN_SLOW;
      xTimerChangePeriod(
        _timer_wifiRescan,
        pdMS_TO_TICKS(_interval_wifiRescan),
        portMAX_DELAY);
    }
  } else {
    log_v("timer escaneo WiFi INACTIVO");
    if (_interval_wifiRescan == (needFastScan ? WIFI_RESCAN_FAST : WIFI_RESCAN_SLOW)) {
      log_v("(se inicia directamente timer scan compatible)");
      xTimerStart(_timer_wifiRescan, 0);
    } else {
      log_v("se cambia e inicia escaneo de WiFi %s", needFastScan ? "RAPIDO" : "LENTO");
      _interval_wifiRescan = needFastScan ? WIFI_RESCAN_FAST : WIFI_RESCAN_SLOW;
      xTimerChangePeriod(
        _timer_wifiRescan,
        pdMS_TO_TICKS(_interval_wifiRescan),
        portMAX_DELAY);
    }
  }
}

void YuboxWiFiClass::beginServerOnWiFiReady(AsyncWebServer * pSrv)
{
  if (_wifiReadyEventReceived) {
    log_d("beginServerOnWiFiReady iniciando webserver de inmediato...");
    pSrv->begin();
  } else {
    log_d("beginServerOnWiFiReady almacenando ptr para inicio retardado");
    _pWebSrvBootstrap = pSrv;
  }
}

void YuboxWiFiClass::_cbHandler_WiFiEvent_ready(WiFiEvent_t event, WiFiEventInfo_t)
{
  log_v("_cbHandler_WiFiEvent_ready recibido");

  // Manejar el evento una sola vez
  _wifiReadyEventReceived = true;
  WiFi.removeEvent(_eventId_cbHandler_WiFiEvent_ready);
  _eventId_cbHandler_WiFiEvent_ready = 0;

  _bootstrapWebServer();
}

void YuboxWiFiClass::_bootstrapWebServer(void)
{
  if (!MDNS.begin(_mdnsName.c_str())) {
    log_e("no se puede iniciar mDNS para anunciar hostname!");
  } else {
    log_d("Iniciando mDNS con nombre de host: %s.local", _mdnsName.c_str());
  }

  MDNS.addService("http", "tcp", 80);

  if (_pWebSrvBootstrap != NULL) {
    log_d("_cbHandler_WiFiEvent_ready arrancando webserver...");
    _pWebSrvBootstrap->begin();
  }
  _pWebSrvBootstrap = NULL;
}

void YuboxWiFiClass::_cbHandler_WiFiEvent_scandone(WiFiEvent_t event, WiFiEventInfo_t)
{
  if (_assumeControlOfWiFi) WiFi.setAutoReconnect(true);
  _collectScannedNetworks();

  // Reportar las redes a cualquier cliente SSE que esté escuchando
  if (_pEvents != NULL && _pEvents->count() > 0) {
    log_v("hay %d clientes SSE conectados, se reporta resultado scan...", _pEvents->count());
    String json_report = _buildAvailableNetworksJSONReport();
    _pEvents->send(json_report.c_str(), "WiFiScanResult");
    if (_assumeControlOfWiFi) _startCondRescanTimer(false);
  }

  if (_assumeControlOfWiFi) {
    if (WiFi.status() != WL_CONNECTED) {
      log_d(
#if ESP_IDF_VERSION_MAJOR > 3
        "ARDUINO_EVENT_WIFI_SCAN_DONE"
#else
        "SYSTEM_EVENT_SCAN_DONE"
#endif
        " y no conectado a red alguna, se verifica una red...");
      if (_useTrialNetworkFirst) {
        _activeNetwork = _trialNetwork;
        _useTrialNetworkFirst = false;
        _connectToActiveNetwork();
      } else {
        _chooseKnownScannedNetwork();
      }
    } else {
      //
    }
  }
}

void YuboxWiFiClass::_cbHandler_WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t)
{
    log_v("[WiFi-event] event: %d", event);
    switch(event) {
#if ESP_IDF_VERSION_MAJOR > 3
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#else
    case SYSTEM_EVENT_STA_GOT_IP:
#endif
        log_d("Conectado al WiFi. Dirección IP: %s", WiFi.localIP().toString().c_str());
        WiFi.setAutoReconnect(true);
        _updateActiveNetworkNVRAM();
        break;
#if ESP_IDF_VERSION_MAJOR > 3
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
#else
    case SYSTEM_EVENT_STA_DISCONNECTED:
#endif
        log_d("Se perdió conexión WiFi.");
        _startCondRescanTimer(false);
        break;
    }
}

void YuboxWiFiClass::_enableWiFiMode(void)
{
  if (_enableSoftAP) {
    // Estos deberían ser los valores por omisión del SDK. Se establecen
    // explícitamente para recuperar control luego de cederlo a otra lib.
    IPAddress apIp(192, 168, 4, 1);
    IPAddress apNetmask(255, 255, 255, 0);

    log_i("Iniciando modo dual WiFi (AP+STA)...");
    WiFi.mode(WIFI_AP_STA);
    if (!_softAPConfigured) {
      WiFi.softAPConfig(apIp, apIp, apNetmask);
      WiFi.softAP(_apName.c_str());
      _softAPConfigured = true;
    }
  } else {
    log_i("Iniciando modo cliente WiFi (STA)...");
    _softAPConfigured = false;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }
}

void YuboxWiFiClass::_startWiFi(void)
{
  _enableWiFiMode();
  WiFi.setSleep(true);  // <--- NO PONER A FALSE, o de lo contrario BlueTooth se crashea si se inicia simultáneamente

  log_d("Iniciando escaneo de redes WiFi (1)...");
  WiFi.setAutoReconnect(false);
  WiFi.scanNetworks(true);
}

void YuboxWiFiClass::_collectScannedNetworks(void)
{
  std::vector<YuboxWiFi_scanCache> t;
  unsigned long ts = millis();
  int16_t n = WiFi.scanComplete();

  if (n != WIFI_SCAN_FAILED) for (auto i = 0; i < n; i++) {
    YuboxWiFi_scanCache e;

    e.bssid = WiFi.BSSIDstr(i);
    e.ssid = WiFi.SSID(i);
    e.channel = WiFi.channel(i);
    e.rssi = WiFi.RSSI(i);
    e.authmode = (uint8_t)WiFi.encryptionType(i);

    t.push_back(e);
  }
  if (_assumeControlOfWiFi) WiFi.scanDelete();

  // TODO: candados en este acceso?
  _scannedNetworks_timestamp = ts;
  _scannedNetworks = t;
}

void YuboxWiFiClass::_chooseKnownScannedNetwork(void)
{
  int16_t n = _scannedNetworks.size();

  if (n >= 0) {
    int netIdx = -1;
    
    log_v("se han descubierto %u redes", n);
    if (_selNetwork >= 0 && _selNetwork < _savedNetworks.size()) {
      // Hay una red seleccionada. Buscar si está presente en el escaneo
      log_v("red fijada, verificando si existe en escaneo...");
      for (int i = 0; i < n; i++) {
        if (_scannedNetworks[i].ssid == _savedNetworks[_selNetwork].cred.ssid) {
          log_v("red seleccionada %u existe en escaneo (%s)", _selNetwork, _savedNetworks[_selNetwork].cred.ssid.c_str());
          netIdx = _selNetwork;
          break;
        }
      }
    } else if (_savedNetworks.size() > 0) {
      // Buscar la red más potente que se conoce
      log_v("eligiendo la red óptima de entre las escaneadas...");
      int netBest = -1;
      int8_t netBest_rssi;

      for (int i = 0; i < n; i++) {
        int netFound = -1;
        int8_t netFound_rssi;

        // Primero la red debe ser conocida
        for (int j = 0; j < _savedNetworks.size(); j++) {
          if (_scannedNetworks[i].ssid == _savedNetworks[j].cred.ssid) {
            netFound = j;
            netFound_rssi =_scannedNetworks[i].rssi;
            break;
          }
        }
        if (netFound == -1) continue;

        if (netBest == -1) {
          // Asignar primera red si no se tiene una anterior
          netBest = netFound;
          netBest_rssi = netFound_rssi;
        } else {
          // Verificar si la red analizada es mejor que la última elegida

          if (_savedNetworks[netBest].numFails > _savedNetworks[netFound].numFails) {
            // Seleccionar como mejor red si número de errores de conexión es
            // menor que la de la red anterior elegida
            netBest = netFound;
            netBest_rssi = netFound_rssi;
          } else if (
            (_savedNetworks[netBest].cred.psk.isEmpty() && _savedNetworks[netBest].cred.identity.isEmpty() && _savedNetworks[netBest].cred.password.isEmpty())
            &&
            !(_savedNetworks[netFound].cred.psk.isEmpty() && _savedNetworks[netFound].cred.identity.isEmpty() && _savedNetworks[netFound].cred.password.isEmpty())) {
            // Seleccionar como mejor red si tiene credenciales y la red anterior
            // elegida no tenía (red abierta)
            netBest = netFound;
            netBest_rssi = netFound_rssi;
          } else if (netBest_rssi < netFound_rssi) {
            // Seleccionar como mejor red si es de mayor potencia a la red
            // anterior elegida
            netBest = netFound;
            netBest_rssi = netFound_rssi;
          }
        }
      }

      if (netBest != -1) {
        log_v("se ha elegido red %u presente en escaneo (%s)", netBest, _savedNetworks[netBest].cred.ssid.c_str());
        netIdx = netBest;
      }
    }

    if (netIdx == -1) {
      // Ninguna de las redes guardadas aparece en el escaneo
      log_v("ninguna de las redes guardadas aparece en escaneo.");
      _startCondRescanTimer(false);
    } else {
      WiFi.disconnect(true);

      // Resetear a estado conocido
      esp_wifi_sta_wpa2_ent_clear_identity();
      esp_wifi_sta_wpa2_ent_clear_username();
      esp_wifi_sta_wpa2_ent_clear_password();
      esp_wifi_sta_wpa2_ent_clear_new_password();
      esp_wifi_sta_wpa2_ent_clear_ca_cert();
      esp_wifi_sta_wpa2_ent_clear_cert_key();
      esp_wifi_sta_wpa2_ent_disable();

      _activeNetwork = _savedNetworks[netIdx].cred;
      _connectToActiveNetwork();
    }
  }
}

void YuboxWiFiClass::_connectToActiveNetwork(void)
{
  // Iniciar conexión a red elegida según credenciales
  if (!_activeNetwork.identity.isEmpty()) {
    // Autenticación WPA-Enterprise
    esp_wifi_sta_wpa2_ent_set_identity((const unsigned char *)_activeNetwork.identity.c_str(), _activeNetwork.identity.length());
    esp_wifi_sta_wpa2_ent_set_username((const unsigned char *)_activeNetwork.identity.c_str(), _activeNetwork.identity.length());
    esp_wifi_sta_wpa2_ent_set_password((const unsigned char *)_activeNetwork.password.c_str(), _activeNetwork.password.length());
    // TODO: ¿cuándo es realmente necesario el paso de abajo MSCHAPv2?
    esp_wifi_sta_wpa2_ent_set_new_password((const unsigned char *)_activeNetwork.password.c_str(), _activeNetwork.password.length());
#if ESP_IDF_VERSION_MAJOR > 3
    esp_wifi_sta_wpa2_ent_enable();
#else
    esp_wpa2_config_t wpa2_config = WPA2_CONFIG_INIT_DEFAULT();
    esp_wifi_sta_wpa2_ent_enable(&wpa2_config);
#endif
    WiFi.begin(_activeNetwork.ssid.c_str());
  } else if (!_activeNetwork.psk.isEmpty()) {
    // Autenticación con clave
    WiFi.begin(_activeNetwork.ssid.c_str(), _activeNetwork.psk.c_str());
  } else {
    // Red abierta
    WiFi.begin(_activeNetwork.ssid.c_str());
  }
}

void YuboxWiFiClass::_loadSavedNetworksFromNVRAM(void)
{
  if (!_savedNetworks.empty()) return;

  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_wifi, true);
  _selNetwork = nvram.getInt("net/sel", 0);        // <-- si se lee 0 se asume la red conocida más fuerte
  log_d("net/sel = %d", _selNetwork);
  _selNetwork = (_selNetwork <= 0) ? -1 : _selNetwork - 1;
  uint32_t numNets = nvram.getUInt("net/n", 0);     // <-- número de redes guardadas
  log_d("net/n = %u", numNets);
  for (auto i = 0; i < numNets; i++) {
    YuboxWiFi_nvramrec r;

    _loadOneNetworkFromNVRAM(nvram, i+1, r);

    r.numFails = 0;
    _savedNetworks.push_back(r);
  }

  // Estado de control WiFi
  _assumeControlOfWiFi = nvram.getBool("net/ctrl", true);

  // Activación expresa de interfaz softAP
  _enableSoftAP = nvram.getBool("net/softAP", true);
}

void YuboxWiFiClass::_loadOneNetworkFromNVRAM(Preferences & nvram, uint32_t idx, YuboxWiFi_nvramrec & r)
{
    char key[64];

    sprintf(key, "net/%u/ssid", idx); r.cred.ssid = nvram.getString(key);
    sprintf(key, "net/%u/psk", idx); r.cred.psk = nvram.getString(key);
    sprintf(key, "net/%u/identity", idx); r.cred.identity = nvram.getString(key);
    sprintf(key, "net/%u/password", idx); r.cred.password = nvram.getString(key);
    sprintf(key, "net/%u/lastsel", idx); r.selectedNet = nvram.getBool(key, false);
    r._dirty = false;
}

void YuboxWiFiClass::_saveNetworksToNVRAM(void)
{
  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_wifi, false);
  nvram.putInt("net/sel", (_selNetwork == -1) ? 0 : _selNetwork + 1);
  nvram.putUInt("net/n", _savedNetworks.size());
  for (auto i = 0; i < _savedNetworks.size(); i++) {
    if (!_savedNetworks[i]._dirty) continue;
    _saveOneNetworkToNVRAM(nvram, i+1, _savedNetworks[i]);
  }
}

#define NVRAM_PUTSTRING(nvram, k, s) \
    (((s).length() > 0) ? ((nvram).putString((k), (s)) != 0) : ( (nvram).isKey((k)) ? (nvram).remove((k)) : true ))

void YuboxWiFiClass::_saveOneNetworkToNVRAM(Preferences & nvram, uint32_t idx, YuboxWiFi_nvramrec & r)
{
    char key[64];

    sprintf(key, "net/%u/ssid", idx); NVRAM_PUTSTRING(nvram, key, r.cred.ssid);
    sprintf(key, "net/%u/psk", idx); NVRAM_PUTSTRING(nvram, key, r.cred.psk);
    sprintf(key, "net/%u/identity", idx); NVRAM_PUTSTRING(nvram, key, r.cred.identity);
    sprintf(key, "net/%u/password", idx); NVRAM_PUTSTRING(nvram, key, r.cred.password);
    sprintf(key, "net/%u/lastsel", idx); nvram.putBool(key, r.selectedNet);
    r._dirty = false;
}

void YuboxWiFiClass::_updateActiveNetworkNVRAM(void)
{
  String currNet = WiFi.SSID();

  Preferences nvram;
  char key[64];
  nvram.begin(_ns_nvram_yuboxframework_wifi, false);

  for (auto i = 0; i < _savedNetworks.size(); i++) {
    bool nv = (_savedNetworks[i].cred.ssid == currNet);

    if (nv != _savedNetworks[i].selectedNet) {
      _savedNetworks[i].selectedNet = nv;
      sprintf(key, "net/%u/lastsel", i+1); nvram.putBool(key, _savedNetworks[i].selectedNet);
    }
  }
}

void YuboxWiFiClass::saveControlOfWiFi(void)
{
  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_wifi, false);

  // Estado de control WiFi
  nvram.putBool("net/ctrl", _assumeControlOfWiFi);
}

YuboxWiFi_cred YuboxWiFiClass::getLastActiveNetwork(void)
{
  for (auto i = 0; i < _savedNetworks.size(); i++) {
    if (_savedNetworks[i].selectedNet) {
      return _savedNetworks[i].cred;
    }
  }
  return _activeNetwork;  // Sólo para devolver algo, probablemente tiene cadenas vacías
}

void YuboxWiFiClass::toggleStateAP(bool ap_enable)
{
  _enableSoftAP = ap_enable;
  if (!_assumeControlOfWiFi) return;
  auto curr_wifimode = WiFi.getMode();
  if (curr_wifimode == WIFI_OFF) return;

  if ((curr_wifimode & WIFI_AP) && !_enableSoftAP) {
    log_i("Modo softAP está actualmente activo, y debe ser desactivado");
    _enableWiFiMode();
  } else if (!(curr_wifimode & WIFI_AP) && _enableSoftAP) {
    log_i("Modo softAP está actualmente inactivo, y debe ser activado");
    _enableWiFiMode();
  }
}

void YuboxWiFiClass::saveStateAP(void)
{
  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_wifi, false);

  // Estado de control WiFi
  nvram.putBool("net/softAP", _enableSoftAP);
}

void YuboxWiFiClass::_setupHTTPRoutes(AsyncWebServer & srv)
{
  srv.on("/yubox-api/wificonfig/connection", HTTP_GET, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/connection", HTTP_PUT, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_PUT, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/connection", HTTP_DELETE, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_DELETE, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/networks", HTTP_GET, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/networks", HTTP_POST, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_POST, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/networks", HTTP_DELETE, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_DELETE, this, std::placeholders::_1));
  srv.addRewrite(new Yubox_internal_LastParamRewrite(
    "/yubox-api/wificonfig/networks/{SSID}",
    "/yubox-api/wificonfig/networks?ssid={SSID}"
  ));
  _pEvents = new AsyncEventSource("/yubox-api/wificonfig/netscan");
  YuboxWebAuth.addManagedHandler(_pEvents);
  srv.addHandler(_pEvents);
  _pEvents->onConnect(std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_netscan_onConnect, this, std::placeholders::_1));
}

String YuboxWiFiClass::_buildAvailableNetworksJSONReport(void)
{
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(11));
  String json_output;

  json_output = "[";

  String currNet = WiFi.SSID();
  String currBssid = WiFi.BSSIDstr();
  wl_status_t currNetStatus = WiFi.status();
  if (currNet.isEmpty()) {
    currNet = _activeNetwork.ssid;
  }

  bool redVista = false;
  for (int i = 0; i < _scannedNetworks.size(); i++) {
    if (i > 0) json_output += ",";

    String temp_bssid = _scannedNetworks[i].bssid;
    String temp_ssid = _scannedNetworks[i].ssid;
    String temp_psk;
    String temp_identity;
    String temp_password;

    json_doc["bssid"] = temp_bssid.c_str();
    json_doc["ssid"] = temp_ssid.c_str();
    json_doc["channel"] = _scannedNetworks[i].channel;
    json_doc["rssi"] = _scannedNetworks[i].rssi;
    json_doc["authmode"] = _scannedNetworks[i].authmode;

    // TODO: actualizar estado de bandera de conexión exitosa
    json_doc["connected"] = false;
    json_doc["connfail"] = false;
    if (temp_ssid == currNet && ((currBssid.isEmpty() && !redVista) || temp_bssid == currBssid)) {
      if (currNetStatus == WL_CONNECTED) json_doc["connected"] = true;
      if (currNetStatus == WL_CONNECT_FAILED || currNetStatus == WL_DISCONNECTED) json_doc["connfail"] = true;
      redVista = true;
    }

    // Asignar clave conocida desde NVRAM si está disponible
    json_doc["saved"] = false;
    json_doc["psk"] = (char *)NULL;
    json_doc["identity"] = (char *)NULL;
    json_doc["password"] = (char *)NULL;

    unsigned int j;
    for (j = 0; j < _savedNetworks.size(); j++) {
      if (_savedNetworks[j].cred.ssid == temp_ssid) break;
    }
    if (j < _savedNetworks.size()) {
      json_doc["saved"] = true;       // Se tiene disponible información sobre la red

      temp_psk = _savedNetworks[j].cred.psk;
      if (!temp_psk.isEmpty()) json_doc["psk"] = temp_psk.c_str();

      temp_identity = _savedNetworks[j].cred.identity;
      if (!temp_identity.isEmpty()) json_doc["identity"] = temp_identity.c_str();

      temp_password = _savedNetworks[j].cred.password;
      if (!temp_password.isEmpty()) json_doc["password"] = temp_password.c_str();
    }

    serializeJson(json_doc, json_output);
  }

  json_output += "]";
  return json_output;
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_netscan_onConnect(AsyncEventSourceClient *)
{
  // Emitir estado actual de control de WiFi
  StaticJsonDocument<JSON_OBJECT_SIZE(1)> json_doc;
  json_doc["yubox_control_wifi"] = _assumeControlOfWiFi;
  String json_str;
  serializeJson(json_doc, json_str);
  _pEvents->send(json_str.c_str(), "WiFiStatus");

  // Emitir cualquier lista disponible de inmediato, posiblemente poblada por otro dueño de WiFi
  String json_report = _buildAvailableNetworksJSONReport();
  _pEvents->send(json_report.c_str(), "WiFiScanResult");

  // No iniciar escaneo a menos que se tenga control del WiFi
  if (!_assumeControlOfWiFi) return;

  // Si el timer está activo, pero en escaneo lento, debe de desactivarse
  bool tmrActive = xTimerIsTimerActive(_timer_wifiRescan);
  if (tmrActive && _interval_wifiRescan == WIFI_RESCAN_SLOW) {
    log_v("se detiene timer rescan wifi LENTO porque se conectó cliente para configurar WiFi");
    xTimerStop(_timer_wifiRescan, 0);
    log_d("Iniciando escaneo de redes WiFi (2b)...");
    WiFi.setAutoReconnect(false);
    WiFi.scanNetworks(true);
  } else if (!tmrActive) {
    // Por ahora no me interesa el cliente individual, sólo el hecho de que se debe iniciar escaneo
    log_d("Iniciando escaneo de redes WiFi (2a)...");
    WiFi.setAutoReconnect(false);
    WiFi.scanNetworks(true);
  }
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);
  
  wl_status_t currNetStatus = WiFi.status();
  if (currNetStatus != WL_CONNECTED) {
    request->send(404, "application/json", "{\"msg\":\"No hay conexi\\u00f3nn actualmente activa\"}");
    return;
  }

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(10) + JSON_ARRAY_SIZE(3));

  String temp_ssid = WiFi.SSID();
  String temp_bssid = WiFi.BSSIDstr();
  String temp_mac = WiFi.macAddress();
  String temp_ipv4 = WiFi.localIP().toString();
  String temp_gw = WiFi.gatewayIP().toString();
  String temp_netmask = WiFi.subnetMask().toString();
  String temp_dns[3];

  json_doc["connected"] = true;
  json_doc["rssi"] = WiFi.RSSI();
  json_doc["ssid"] = temp_ssid.c_str();
  json_doc["bssid"] = temp_bssid.c_str();
  json_doc["mac"] = temp_mac.c_str();
  json_doc["ipv4"] = temp_ipv4.c_str();
  json_doc["gateway"] = temp_gw.c_str();
  json_doc["netmask"] = temp_netmask.c_str();
  JsonArray json_dns = json_doc.createNestedArray("dns");
  for (auto i = 0; i < 3; i++) {
    temp_dns[i] = WiFi.dnsIP(i).toString();
    if (temp_dns[i] != "0.0.0.0") json_dns.add(temp_dns[i].c_str());
  }

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_PUT(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  _addOneSavedNetwork(request, true);
}

void YuboxWiFiClass::_addOneSavedNetwork(AsyncWebServerRequest *request, bool switch2net)
{
  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;
  YuboxWiFi_nvramrec tempNetwork;
  bool pinNetwork = false;
  uint8_t authmode;

  if (!clientError) {
    if (!request->hasParam("ssid", true)) {
      clientError = true;
      responseMsg = "Se requiere SSID para conectarse";
    } else {
      p = request->getParam("ssid", true);
      tempNetwork.cred.ssid = p->value();
    }
  }
  if (!clientError) {
    if (request->hasParam("pin", true)) {
      p = request->getParam("pin", true);
      pinNetwork = (p->value() != "0");
    }

    if (!request->hasParam("authmode", true)) {
      clientError = true;
      responseMsg = "Se requiere modo de autenticación a usar";
    } else {
      p = request->getParam("authmode", true);
      if (p->value().length() != 1) {
        clientError = true;
        responseMsg = "Modo de autenticación inválido";
      } else if (!(p->value()[0] >= '0' && p->value()[0] <= '5')) {
        clientError = true;
        responseMsg = "Modo de autenticación inválido";
      } else {
        authmode = p->value().toInt();
      }
    }
  }

  if (!clientError) {
    if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
      if (!clientError && !request->hasParam("identity", true)) {
        clientError = true;
        responseMsg = "Se requiere una identidad para esta red";
      } else {
        p = request->getParam("identity", true);
        tempNetwork.cred.identity = p->value();
      }
      if (!clientError && !request->hasParam("password", true)) {
        clientError = true;
        responseMsg = "Se requiere contraseña para esta red";
      } else {
        p = request->getParam("password", true);
        tempNetwork.cred.password = p->value();
      }
    } else if (authmode != WIFI_AUTH_OPEN) {
      if (!request->hasParam("psk", true)) {
        clientError = true;
        responseMsg = "Se requiere contraseña para esta red";
      } else {
        p = request->getParam("psk", true);
        if (p->value().length() < 8) {
          clientError = true;
          responseMsg = "Contraseña para esta red debe ser como mínimo de 8 caracteres";
        } else {
          tempNetwork.cred.psk = p->value();
        }
      }
    }
  }

  // Crear o actualizar las credenciales indicadas
  if (!clientError) {
    int idx = -1;

    // Información de conexión actual (si existe)
    wl_status_t currNetStatus = WiFi.status();
    String currNet = WiFi.SSID();
    if (currNet.isEmpty()) {
      currNet = _activeNetwork.ssid;
    }

    tempNetwork._dirty = true;
    for (auto i = 0; i < _savedNetworks.size(); i++) {
      // Aunque no se haya indicado cambiar a esta red, si la red modificada es la red
      // a la que se requiere conectar, se debe activar el switcheo
      if (!switch2net) {
        if (currNetStatus == WL_CONNECTED && tempNetwork.cred.ssid == currNet) {
          switch2net = true;
        }
      }

      if (_savedNetworks[i].cred.ssid == tempNetwork.cred.ssid) {
        idx = i;
        _savedNetworks[idx] = tempNetwork;
        break;
      }
    }
    if (idx == -1) {
      idx = _savedNetworks.size();
      _savedNetworks.push_back(tempNetwork);
    }
    _selNetwork = (pinNetwork) ? idx : -1;

    // Mandar a guardar el vector modificado
    _saveNetworksToNVRAM();
    if (switch2net && _assumeControlOfWiFi) {
      _useTrialNetworkFirst = true;
      _trialNetwork = tempNetwork.cred;
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }
  unsigned int httpCode = 202;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);

  if (switch2net && _assumeControlOfWiFi && !clientError && !serverError) {
    _startCondRescanTimer(true);
  }
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_DELETE(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  wl_status_t currNetStatus = WiFi.status();
  if (currNetStatus != WL_CONNECTED) {
    request->send(404, "application/json", "{\"msg\":\"No hay conexi\\u00f3nn actualmente activa\"}");
    return;
  }

  _delOneSavedNetwork(request, WiFi.SSID(), true);
}

void YuboxWiFiClass::_delOneSavedNetwork(AsyncWebServerRequest *request, String ssid, bool deleteconnected)
{
  // Buscar cuál índice de red guardada hay que eliminar
  int idx = -1;
  for (auto i = 0; i < _savedNetworks.size(); i++) {
    if (_savedNetworks[i].cred.ssid == ssid) {
      idx = i;
      break;
    }
  }
  if (idx != -1) {
    // Manipular el vector de redes para compactar
    if (_selNetwork == idx) _selNetwork = -1;
    if (idx < _savedNetworks.size() - 1) {
      // Copiar último elemento encima del que se elimina
      _savedNetworks[idx] = _savedNetworks[_savedNetworks.size() - 1];
      _savedNetworks[idx]._dirty = true;
    }
    _savedNetworks.pop_back();

    // Mandar a guardar el vector modificado
    _saveNetworksToNVRAM();
  } else if (!deleteconnected) {
    request->send(404, "application/json", "{\"msg\":\"No existe la red indicada\"}");
    return;
  }

  request->send(204);

  if (deleteconnected && _assumeControlOfWiFi) {
    _startCondRescanTimer(true);
  }
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  if (request->hasParam("ssid")) {
    AsyncWebParameter* p = request->getParam("ssid");

    // Buscar cuál índice de red guardada corresponde a este SSID
    auto idx = -1;
    for (auto i = 0; i < _savedNetworks.size(); i++) {
      if (_savedNetworks[i].cred.ssid == p->value()) {
        idx = i;
        break;
      }
    }
    if (idx == -1) {
      // Red indicada no se encuentra
      request->send(404, "application/json", "{\"msg\":\"No existe la red indicada\"}");
    } else {
      AsyncResponseStream *response = request->beginResponseStream("application/json");

      _serializeOneSavedNetwork(response, idx);

      request->send(response);
    }
  } else {
    // Volcar todas las redes guardadas en NVRAM
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("[");
    for (auto i = 0; i < _savedNetworks.size(); i++) {
      if (i > 0) response->print(",");

      _serializeOneSavedNetwork(response, i);
    }
    response->print("]");
    request->send(response);
  }
}

void YuboxWiFiClass::_serializeOneSavedNetwork(AsyncResponseStream *response, uint32_t i)
{
  StaticJsonDocument<JSON_OBJECT_SIZE(4)> json_doc;

  json_doc["ssid"] = _savedNetworks[i].cred.ssid.c_str();
  json_doc["psk"] = (const char *)NULL;
  json_doc["identity"] = (const char *)NULL;
  json_doc["password"] = (const char *)NULL;
  if (_savedNetworks[i].cred.psk.length() > 0) json_doc["psk"] = _savedNetworks[i].cred.psk.c_str();
  if (_savedNetworks[i].cred.identity.length() > 0) json_doc["identity"] = _savedNetworks[i].cred.identity.c_str();
  if (_savedNetworks[i].cred.password.length() > 0) json_doc["password"] = _savedNetworks[i].cred.password.c_str();

  serializeJson(json_doc, *response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  _addOneSavedNetwork(request, false);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_DELETE(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  if (!request->hasParam("ssid")) {
    request->send(400, "application/json", "{\"success\":false,\"msg\":\"No hay conexi\\u00f3nn actualmente activa\"}");
    return;
  }
  AsyncWebParameter* p = request->getParam("ssid");
  String ssid = p->value();
  bool deleteconnected = (WiFi.status() == WL_CONNECTED && ssid == WiFi.SSID());

  _delOneSavedNetwork(request, ssid, deleteconnected);
}

void _cb_YuboxWiFiClass_wifiRescan(TimerHandle_t timer)
{
  YuboxWiFiClass *self = (YuboxWiFiClass *)pvTimerGetTimerID(timer);
  return self->_cbHandler_WiFiRescan(timer);
}

void YuboxWiFiClass::_cbHandler_WiFiRescan(TimerHandle_t timer)
{
  log_v("YuboxWiFiClass::_cbHandler_WiFiRescan");
  if (_disconnectBeforeRescan) {
    _disconnectBeforeRescan = false;
    log_v("Desconectando antes de escaneo...");
    WiFi.disconnect(true);
  }
  _enableWiFiMode();
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING && (WiFi.status() != WL_CONNECTED || (_pEvents != NULL && _pEvents->count() > 0))) {
    log_d("DEBUG: Iniciando escaneo de redes WiFi (3)...");
    WiFi.setAutoReconnect(false);
    WiFi.scanNetworks(true);
  }
}

YuboxWiFiClass YuboxWiFi;
