#include "YuboxWiFiClass.h"
#include "esp_wpa2.h"
#include <ESPmDNS.h>
#include <Preferences.h>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <functional>

const char * YuboxWiFiClass::_ns_nvram_yuboxframework_wifi = "YUBOX/WiFi";

YuboxWiFiClass::YuboxWiFiClass(void)
{
  String tpl = "YUBOX-{MAC}";

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
  byte maccopy[6];
  memset(maccopy, 0, sizeof(maccopy));
  WiFi.macAddress(maccopy);

  String mac;
  for (auto i = 0; i < sizeof(maccopy); i++) {
    String octet = String(maccopy[i], 16);
    octet.toUpperCase();
    mac += octet;
  }
  return mac;
}

void YuboxWiFiClass::begin(AsyncWebServer & srv)
{
  _loadSavedNetworksFromNVRAM();
  _setupHTTPRoutes(srv);

  WiFi.onEvent(std::bind(&YuboxWiFiClass::_cbHandler_WiFiEvent, this, std::placeholders::_1));
  _startWiFi();
}

void YuboxWiFiClass::_cbHandler_WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("DEBUG: [WiFi-event] event: %d\r\n", event);
    switch(event) {
    case SYSTEM_EVENT_SCAN_DONE:
      _collectScannedNetworks();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("DEBUG: SYSTEM_EVENT_SCAN_DONE y no conectado a red alguna, se verifica una red...");
        _chooseKnownScannedNetwork();
      } else {
        //
      }
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.print("DEBUG: Conectado al WiFi. Dirección IP: ");
        Serial.println(WiFi.localIP());
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("DEBUG: Se perdió conexión WiFi.");
        if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
          Serial.println("DEBUG: Iniciando escaneo de redes WiFi (2)...");
          WiFi.scanNetworks(true);
        }
        break;
    }
}

void YuboxWiFiClass::_startWiFi(void)
{
  Serial.println("DEBUG: Iniciando modo dual WiFi (AP+STA)...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_apName.c_str());

  if (!MDNS.begin(_mdnsName.c_str())) {
    Serial.println("ERROR: no se puede iniciar mDNS para anunciar hostname!");
  } else {
    Serial.print("DEBUG: Iniciando mDNS con nombre de host: ");
    Serial.print(_mdnsName);
    Serial.println(".local");
  }

  MDNS.addService("http", "tcp", 80);

  Serial.println("DEBUG: Iniciando escaneo de redes WiFi...");
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
  WiFi.scanDelete();

  // TODO: candados en este acceso?
  _scannedNetworks_timestamp = ts;
  _scannedNetworks = t;
}

void YuboxWiFiClass::_chooseKnownScannedNetwork(void)
{
  int16_t n = _scannedNetworks.size();

  if (n >= 0) {
    int netIdx = -1;
    
    Serial.printf("DEBUG: se han descubierto %u redes\r\n", n);
    if (_selNetwork >= 0 && _selNetwork < _savedNetworks.size()) {
      // Hay una red seleccionada. Buscar si está presente en el escaneo
      Serial.println("DEBUG: red fijada, verificando si existe en escaneo...");
      for (int i = 0; i < n; i++) {
        if (_scannedNetworks[i].ssid == _savedNetworks[_selNetwork].ssid) {
          Serial.printf("DEBUG: red seleccionada %u existe en escaneo (%s)\r\n", _selNetwork, _savedNetworks[_selNetwork].ssid.c_str());
          netIdx = _selNetwork;
          break;
        }
      }
    } else if (_savedNetworks.size() > 0) {
      // Buscar la red más potente que se conoce
      Serial.println("DEBUG: eligiendo la red óptima de entre las escaneadas...");
      int netBest = -1;
      int8_t netBest_rssi;

      for (int i = 0; i < n; i++) {
        int netFound = -1;
        int8_t netFound_rssi;

        // Primero la red debe ser conocida
        for (int j = 0; j < _savedNetworks.size(); j++) {
          if (_scannedNetworks[i].ssid == _savedNetworks[j].ssid) {
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
            (_savedNetworks[netBest].psk.isEmpty() && _savedNetworks[netBest].identity.isEmpty() && _savedNetworks[netBest].password.isEmpty()) 
            &&
            !(_savedNetworks[netFound].psk.isEmpty() && _savedNetworks[netFound].identity.isEmpty() && _savedNetworks[netFound].password.isEmpty())) {
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

      Serial.printf("DEBUG: se ha elegido red %u presente en escaneo (%s)\r\n", netBest, _savedNetworks[netBest].ssid.c_str());
      netIdx = netBest;
    }

    if (netIdx == -1) {
      // Ninguna de las redes guardadas aparece en el escaneo
      Serial.println("DEBUG: ninguna de las redes guardadas aparece en escaneo.");
      if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        Serial.println("DEBUG: Iniciando escaneo de redes WiFi (3)...");
        WiFi.scanNetworks(true);
      }
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

      _activeNetwork = _savedNetworks[netIdx];

      // Iniciar conexión a red elegida según credenciales
      if (!_activeNetwork.identity.isEmpty()) {
        // Autenticación WPA-Enterprise
        esp_wifi_sta_wpa2_ent_set_identity((const unsigned char *)_activeNetwork.identity.c_str(), _activeNetwork.identity.length());
        esp_wifi_sta_wpa2_ent_set_username((const unsigned char *)_activeNetwork.identity.c_str(), _activeNetwork.identity.length());
        esp_wifi_sta_wpa2_ent_set_password((const unsigned char *)_activeNetwork.password.c_str(), _activeNetwork.password.length());
        // TODO: ¿cuándo es realmente necesario el paso de abajo MSCHAPv2?
        esp_wifi_sta_wpa2_ent_set_new_password((const unsigned char *)_activeNetwork.password.c_str(), _activeNetwork.password.length());
        esp_wpa2_config_t wpa2_config = WPA2_CONFIG_INIT_DEFAULT();
        esp_wifi_sta_wpa2_ent_enable(&wpa2_config);
        WiFi.begin(_activeNetwork.ssid.c_str());
      } else if (!_activeNetwork.psk.isEmpty()) {
        // Autenticación con clave
        WiFi.begin(_activeNetwork.ssid.c_str(), _activeNetwork.psk.c_str());
      } else {
        // Red abierta
        WiFi.begin(_activeNetwork.ssid.c_str());
      }
    }
  }
}

void YuboxWiFiClass::_loadSavedNetworksFromNVRAM(void)
{
  if (!_savedNetworks.empty()) return;

  Preferences nvram;
  nvram.begin(_ns_nvram_yuboxframework_wifi, true);
  _selNetwork = nvram.getInt("net/sel", 0);        // <-- si se lee 0 se asume la red conocida más fuerte
  Serial.printf("DEBUG: net/sel = %d\r\n", _selNetwork);
  _selNetwork = (_selNetwork <= 0) ? -1 : _selNetwork - 1;
  uint32_t numNets = nvram.getUInt("net/n", 0);     // <-- número de redes guardadas
  Serial.printf("DEBUG: net/n = %u\r\n", numNets);
  for (auto i = 0; i < numNets; i++) {
    YuboxWiFi_nvramrec r;

    _loadOneNetworkFromNVRAM(nvram, i+1, r);

    r.numFails = 0;
    _savedNetworks.push_back(r);
  }
}

void YuboxWiFiClass::_loadOneNetworkFromNVRAM(Preferences & nvram, uint32_t idx, YuboxWiFi_nvramrec & r)
{
    char key[64];

    sprintf(key, "net/%u/ssid", idx); r.ssid = nvram.getString(key);
    sprintf(key, "net/%u/psk", idx); r.psk = nvram.getString(key);
    sprintf(key, "net/%u/identity", idx); r.identity = nvram.getString(key);
    sprintf(key, "net/%u/password", idx); r.password = nvram.getString(key);
}

#define NVRAM_PUTSTRING(nvram, k, s) \
    (((s).length() > 0) ? ((nvram).putString((k), (s)) != 0) : (nvram).remove((k)))

void YuboxWiFiClass::_saveOneNetworkToNVRAM(Preferences & nvram, uint32_t idx, YuboxWiFi_nvramrec & r)
{
    char key[64];

    sprintf(key, "net/%u/ssid", idx); NVRAM_PUTSTRING(nvram, key, r.ssid);
    sprintf(key, "net/%u/psk", idx); NVRAM_PUTSTRING(nvram, key, r.psk);
    sprintf(key, "net/%u/identity", idx); NVRAM_PUTSTRING(nvram, key, r.identity);
    sprintf(key, "net/%u/password", idx); NVRAM_PUTSTRING(nvram, key, r.password);
}

void YuboxWiFiClass::_setupHTTPRoutes(AsyncWebServer & srv)
{
  srv.on("/yubox-api/wificonfig/networks", HTTP_GET, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/connection", HTTP_GET, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_GET, this, std::placeholders::_1));
  //srv.on("/yubox-api/wificonfig/connection", HTTP_PUT, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_PUT, this, std::placeholders::_1));
  //srv.on("/yubox-api/wificonfig/connection", HTTP_DELETE, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_DELETE, this, std::placeholders::_1));
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(10));

  response->print("[");

  String currNet = WiFi.SSID();
  String currBssid = WiFi.BSSIDstr();
  wl_status_t currNetStatus = WiFi.status();

  for (int i = 0; i < _scannedNetworks.size(); i++) {
    if (i > 0) response->print(",");

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
    if (temp_ssid == currNet && temp_bssid == currBssid) {
      if (currNetStatus == WL_CONNECTED) json_doc["connected"] = true;
      if (currNetStatus == WL_CONNECT_FAILED) json_doc["connfail"] = true;
    }

    // Asignar clave conocida desde NVRAM si está disponible
    json_doc["psk"] = (char *)NULL;
    json_doc["identity"] = (char *)NULL;
    json_doc["password"] = (char *)NULL;

    unsigned int j;
    for (j = 0; j < _savedNetworks.size(); j++) {
      if (_savedNetworks[j].ssid == temp_ssid) break;
    }
    if (j < _savedNetworks.size()) {
      temp_psk = _savedNetworks[j].psk;
      if (!temp_psk.isEmpty()) json_doc["psk"] = temp_psk.c_str();

      temp_identity = _savedNetworks[j].identity;
      if (!temp_identity.isEmpty()) json_doc["identity"] = temp_identity.c_str();

      temp_password = _savedNetworks[j].password;
      if (!temp_password.isEmpty()) json_doc["password"] = temp_password.c_str();
    }

    serializeJson(json_doc, *response);
  }

  response->print("]");

  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(true);
  }

  request->send(response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request)
{
  wl_status_t currNetStatus = WiFi.status();
  if (currNetStatus != WL_CONNECTED) {
    request->send(404, "application/json", "{msg:\"No hay conexi\\u00f3nn actualmente activa\"}");
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


YuboxWiFiClass YuboxWiFi;

