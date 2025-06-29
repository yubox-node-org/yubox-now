#include "YuboxWiFiClass.h"
#include "esp_wpa2.h"
#include "esp_mac.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "YuboxParamPOST.h"

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#include <functional>

#include "Yubox_internal_LastParamRewrite.hpp"

const char * YuboxWiFiClass::_ns_nvram_yuboxframework_wifi = "YUBOX/WiFi";
void _cb_YuboxWiFiClass_wifiRescan(TimerHandle_t);
void _cb_YuboxWiFiClass_wifiNoIpTimeout(TimerHandle_t);

// En ciertos escenarios, el re-escaneo de WiFi cada 2 segundos puede interferir
// con tareas de la aplicación. Para mitigar esto, se escanea cada 30 segundos,
// a menos que haya al menos un cliente SSE que requiere configurar la red WiFi.
#define WIFI_RESCAN_FAST  8000
#define WIFI_RESCAN_SLOW  30000

// Timeout desde STA_CONNECTED hasta que, si no se tiene wifi, se reintenta conexión
#define WIFI_STA_NOIP_TIMEOUT 5000

YuboxWiFiClass::YuboxWiFiClass(void)
{
  String tpl = "YUBOX-{MAC}";

  _assumeControlOfWiFi = true;
  _enableSoftAP = true;
  _softAPConfigured = false;
  _softAPHide = false;
  _skipWiFiOnWakeupDeepSleep = false;

  _selNetwork = -1;

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

  _timer_wifiNoIpTimeout = xTimerCreate(
    "YuboxWiFiClass_wifiNoIpTimeout",
    pdMS_TO_TICKS(WIFI_STA_NOIP_TIMEOUT),
    pdFALSE,
    (void*)this,
    &_cb_YuboxWiFiClass_wifiNoIpTimeout
  );

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
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char macStr[18] = { 0 };
  sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

void YuboxWiFiClass::begin(AsyncWebServer & srv)
{
  _loadSavedNetworksFromNVRAM();
  _setupHTTPRoutes(srv);

  _loadBootstrapNetworkFromFS();

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
    if (_skipWiFiOnWakeupDeepSleep && esp_reset_reason() == ESP_RST_DEEPSLEEP) {
      log_i("Dispositivo despierta de deep-sleep, se omite inicialización de WiFi...");
    } else {
      takeControlOfWiFi();
    }
  }
}

void YuboxWiFiClass::takeControlOfWiFi(void)
{
  _assumeControlOfWiFi = true;
  _eventId_cbHandler_WiFiEvent = WiFi.onEvent(
    std::bind(&YuboxWiFiClass::_cbHandler_WiFiEvent, this, std::placeholders::_1, std::placeholders::_2));
  _startWiFi();
  _publishWiFiStatus();
}

void YuboxWiFiClass::releaseControlOfWiFi(bool wifioff)
{
  log_i("Cediendo control del WiFi (WiFi %s)...", wifioff ? "OFF" : "ON");

  _assumeControlOfWiFi = false;
  _publishWiFiStatus();
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
    case ARDUINO_EVENT_WIFI_AP_START:
#else
    case SYSTEM_EVENT_AP_START:
#endif
        {
            // Estos deberían ser los valores por omisión del SDK. Se establecen
            // explícitamente para recuperar control luego de cederlo a otra lib.
            IPAddress apIp(192, 168, 4, 1);
            IPAddress apNetmask(255, 255, 255, 0);
#ifdef ESP_ARDUINO_VERSION
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 4)
            // NO DEBE DE DEPENDERSE DE PARÁMETRO default IPAddress dhcp_lease_start = INADDR_NONE
            // en WiFiAPClass::softAPConfig() !
            // https://github.com/espressif/arduino-esp32/issues/6760
            IPAddress apLeaseStart(0, 0, 0, 0);
#endif
#endif

            if (!WiFi.softAPConfig(apIp, apIp, apNetmask
#ifdef ESP_ARDUINO_VERSION
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 4)
              , apLeaseStart
#endif
#endif
              )) {
                log_e("Falla al asignar interfaz SoftAP: %s | Gateway: %s "
#ifdef ESP_ARDUINO_VERSION
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 4)
                      "| DHCP Start: %s "
#endif
#endif
                      "| Netmask: %s",
                    apIp.toString().c_str(),
                    apIp.toString().c_str(),
#ifdef ESP_ARDUINO_VERSION
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 4)
                    apLeaseStart.toString().c_str(),
#endif
#endif
                    apNetmask.toString().c_str()
                );
            }
        }
        break;
#if ESP_IDF_VERSION_MAJOR > 3
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
#else
    case SYSTEM_EVENT_STA_CONNECTED:
#endif
        if (_assumeControlOfWiFi) {
            if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
                auto wifistatus = WiFi.status();
                if (wifistatus != WL_CONNECTED) {
                    log_d("En este momento el estatus wifi es %u != WL_CONNECTED", wifistatus);
                }
                log_d("Conectado a nivel STA, se espera una IP...");
                WiFi.setAutoReconnect(true);

                xTimerStart(_timer_wifiNoIpTimeout, 0);
            }
        }
        break;
#if ESP_IDF_VERSION_MAJOR > 3
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#else
    case SYSTEM_EVENT_STA_GOT_IP:
#endif
        if (xTimerIsTimerActive(_timer_wifiNoIpTimeout)) {
            xTimerStop(_timer_wifiNoIpTimeout, 0);
        }
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
        if (_savedNetworks.size() > 0 || (_pEvents != NULL && _pEvents->count() > 0)) {
          _startCondRescanTimer(false);
        } else {
          log_d("- no hay redes guardadas y no hay conexiones SSE, se evita escaneo.");
        }
        break;
    }
}

void YuboxWiFiClass::_enableWiFiMode(void)
{
  auto curr_wifimode = WiFi.getMode();

  if (_enableSoftAP) {
    if (curr_wifimode != WIFI_AP_STA) {
      log_i("Iniciando modo dual WiFi (AP+STA)...");
      WiFi.mode(WIFI_AP_STA);
    }
    if (!_softAPConfigured) {
      log_i("- activando softAP SSID=%s (%s)...",
        _apName.c_str(),
        _softAPHide ? "ESCONDIDO" : "VISIBLE");
      WiFi.softAP(
        _apName.c_str(),
        NULL,               // passphrase
        1,                  // channel
        _softAPHide ? 1 : 0 // ssid_hidden
        );
      _softAPConfigured = true;
    }
  } else {
    _softAPConfigured = false;
    if (curr_wifimode != WIFI_STA) {
      log_i("Iniciando modo cliente WiFi (STA)...");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }
  }
}

void YuboxWiFiClass::_startWiFi(void)
{
  _enableWiFiMode();
  WiFi.setSleep(true);  // <--- NO PONER A FALSE, o de lo contrario BlueTooth se crashea si se inicia simultáneamente

  WiFi.setAutoReconnect(false);

  _activeNetwork = getLastActiveNetwork();
  if (!_activeNetwork.ssid.isEmpty()) {
    // Se anula última red válida. Debería volverse a asignar cuando se obtenga IP.
    log_d("Reconectando a última red válida: %s ...", _activeNetwork.ssid.c_str());
    wl_status_t r = _connectToActiveNetwork();
    if (r != WL_CONNECT_FAILED) return;
  }

  if (_savedNetworks.size() > 0 || (_pEvents != NULL && _pEvents->count() > 0)) {
    log_d("Iniciando escaneo de redes WiFi (1)...");
    WiFi.scanNetworks(true);
  } else {
    log_d("- no hay redes guardadas y no hay conexiones SSE, se evita escaneo.");
  }
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
      if (_savedNetworks.size() > 0 || (_pEvents != NULL && _pEvents->count() > 0)) {
        _startCondRescanTimer(false);
      } else {
        log_d("- no hay redes guardadas y no hay conexiones SSE, se evita escaneo.");
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

      _activeNetwork = _savedNetworks[netIdx].cred;
      _connectToActiveNetwork();
    }
  }
}

wl_status_t YuboxWiFiClass::_connectToActiveNetwork(void)
{
  wl_status_t r = WL_DISCONNECTED;

  // Iniciar conexión a red elegida según credenciales
  log_d("Iniciando conexión a red: %s ...", _activeNetwork.ssid.c_str());
  uint32_t t1 = millis();
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
    r = WiFi.begin(_activeNetwork.ssid.c_str());
  } else if (!_activeNetwork.psk.isEmpty()) {
    // Autenticación con clave
    r = WiFi.begin(_activeNetwork.ssid.c_str(), _activeNetwork.psk.c_str());
  } else {
    // Red abierta
    r = WiFi.begin(_activeNetwork.ssid.c_str());
  }
  uint32_t t2 = millis();

  log_d("WiFi.begin() devuelte status %d luego de %u msec", r, (t2 - t1));
  if (r == WL_CONNECT_FAILED) {
    for (auto it = _savedNetworks.begin(); it != _savedNetworks.end(); it++) {
      if (it->cred.ssid == _activeNetwork.ssid) {
        it->numFails++;
      }
    }
  }

  return r;
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

  if (_selNetwork >= 0 && _selNetwork >= numNets) {
    _selNetwork = -1;
  }

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

  // Esconder red softAP de los escaneos
  _softAPHide = nvram.getBool("net/softAPHide", false);

  _skipWiFiOnWakeupDeepSleep = nvram.getBool("net/skipOnDS");

  // Personlización del nombre del softAP
  String tpl = nvram.getString("net/apname", "YUBOX-{MAC}");
  setAPName(tpl);
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

void YuboxWiFiClass::_loadBootstrapNetworkFromFS(void)
{
  const char * wifiboot = "/yuboxwifiboot.txt";

  if (!SPIFFS.exists(wifiboot)) {
    log_d("Archivo %s no existe, se asume funcionamiento normal.", wifiboot);
    return;
  }

  // Archivo de arranque wifi existe. Lo necesito?
  if (_savedNetworks.size() == 0) {
    log_d("No hay redes cargadas al arranque, se intenta cargar red inicial desde %s ...", wifiboot);

    File h = SPIFFS.open(wifiboot, FILE_READ);
    if (!h) {
      log_w("%s existe pero no se puede abrir. No puede cargarse red inicial!", wifiboot);
    } else {
      String s1, s2, s3;
      while (h.available()) {
        String s;

        s = h.readStringUntil('\n');
        s.trim();
        if (!s.isEmpty()) {
          if (s1.isEmpty())
            s1 = s;
          else if (s2.isEmpty())
            s2 = s;
          else if (s3.isEmpty())
            s3 = s;
          else
            break;
        }
      }
      h.close();

      /* Se asume que el archivo es un archivo de texto que contiene una de
       * las siguientes:
       * - una sola línea, se asume que es un SSID sin autenticación
       * - dos líneas, se asume que es SSID, y clave en 2da línea
       * - tres líneas, se asume que es SSID, identidad WPA en 2da línea, contraseña en 3ra línea
       */
      YuboxWiFi_nvramrec r;
      if (!s1.isEmpty()) {
        log_d("boot SSID: %s", s1.c_str());
        r.cred.ssid = s1;

        if (s2.isEmpty()) {
          log_d("boot SSID SIN AUTENTICACIÓN");
        } else if (s3.isEmpty()) {
          log_d("boot PSK: %s", s2.c_str());
          r.cred.psk = s2;
        } else {
          log_d("boot WPA: %s %s", s2.c_str(), s3.c_str());
          r.cred.identity = s2;
          r.cred.password = s3;
        }


        r.numFails = 0;
        r._dirty = true;
        _savedNetworks.push_back(r);
        _saveNetworksToNVRAM();
      }
    }
  }

  // Debe de borrarse el archivo si existe, incondicionalmente
  log_v("BORRANDO %s ...", wifiboot);
  if (!SPIFFS.remove(wifiboot)) {
    log_w("no se pudo borrar %s !", wifiboot);
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
  srv.on("/yubox-api/wificonfig/connection/pin", HTTP_POST, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_pin_POST, this, std::placeholders::_1));
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
  srv.on("/yubox-api/wificonfig/softap", HTTP_POST, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_softap_POST, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/softap_name", HTTP_POST, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_softapname_POST, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/skip_wifi_after_deepsleep", HTTP_GET, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_skipwifiafterdeepsleep_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/wificonfig/skip_wifi_after_deepsleep", HTTP_POST, std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_skipwifiafterdeepsleep_POST, this, std::placeholders::_1));
  _pEvents = new AsyncEventSource("/yubox-api/wificonfig/netscan");
  YuboxWebAuth.addManagedHandler(_pEvents);
  srv.addHandler(_pEvents);
  _pEvents->onConnect(std::bind(&YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_netscan_onConnect, this, std::placeholders::_1));
}

String YuboxWiFiClass::_buildAvailableNetworksJSONReport(void)
{
#if ARDUINOJSON_VERSION_MAJOR <= 6
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(11));
#else
  JsonDocument json_doc;
#endif
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
    json_doc["psk"] = false;
    json_doc["identity"] = false;
    json_doc["password"] = false;

    unsigned int j;
    for (j = 0; j < _savedNetworks.size(); j++) {
      if (_savedNetworks[j].cred.ssid == temp_ssid) break;
    }
    if (j < _savedNetworks.size()) {
      json_doc["saved"] = true;       // Se tiene disponible información sobre la red

      temp_psk = _savedNetworks[j].cred.psk;
      if (!temp_psk.isEmpty()) json_doc["psk"] = true;

      temp_identity = _savedNetworks[j].cred.identity;
      if (!temp_identity.isEmpty()) json_doc["identity"] = true;

      temp_password = _savedNetworks[j].cred.password;
      if (!temp_password.isEmpty()) json_doc["password"] = true;
    }

    String s;
    serializeJson(json_doc, s);
    json_output += s;
  }

  json_output += "]";
  return json_output;
}

void YuboxWiFiClass::_publishWiFiStatus(void)
{
  if (_pEvents == NULL) return;
  if (_pEvents->count() <= 0) return;

#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(6)> json_doc;
#else
  JsonDocument json_doc;
#endif
  json_doc["yubox_control_wifi"] = _assumeControlOfWiFi;

  if (_selNetwork >= 0 && _selNetwork < _savedNetworks.size()) {
    json_doc["pinned_ssid"] = _savedNetworks[_selNetwork].cred.ssid.c_str();
  } else {
    json_doc["pinned_ssid"] = (const char *)NULL;
  }

  json_doc["enable_softap"] = _enableSoftAP;
  json_doc["softap_ssid"] = _apName.c_str();
  json_doc["softap_hide"] = _softAPHide;
  json_doc["skip_on_wakeup_deepsleep"] = _skipWiFiOnWakeupDeepSleep;

  String json_str;
  serializeJson(json_doc, json_str);
   _pEvents->send(json_str.c_str(), "WiFiStatus");
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_netscan_onConnect(AsyncEventSourceClient * sseclient)
{
  // Sólo se toleran hasta 10 segundos de encolamiento
  sseclient->client()->setAckTimeout(10 * 1000);

  // Emitir estado actual de control de WiFi
  _publishWiFiStatus();

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
#if ARDUINOJSON_VERSION_MAJOR <= 6
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(10) + JSON_ARRAY_SIZE(3));
#else
  JsonDocument json_doc;
#endif

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
  JsonArray json_dns =
#if ARDUINOJSON_VERSION_MAJOR <= 6
    json_doc.createNestedArray("dns");
#else
    json_doc["dns"].to<JsonArray>();
#endif
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

  tempNetwork._dirty = false;
  tempNetwork.numFails = 0;
  tempNetwork.selectedNet = false;

  YBX_ASSIGN_STR_FROM_POST(ssid, "SSID", (YBX_POST_VAR_REQUIRED|YBX_POST_VAR_NONEMPTY|YBX_POST_VAR_TRIM), tempNetwork.cred.ssid)
  if (!clientError) {
    // Si la red ya existía, asumir sus credenciales a menos que se indique otra cosa
    for (auto i = 0; i < _savedNetworks.size(); i++) {
      if (_savedNetworks[i].cred.ssid == tempNetwork.cred.ssid) {
        tempNetwork.cred = _savedNetworks[i].cred;
        break;
      }
    }
  }
  if (!clientError) {
    if (request->hasParam("pin", true)) {
      p = request->getParam("pin", true);
      pinNetwork = (p->value() != "0");
    }
  }

  YBX_ASSIGN_NUM_FROM_POST(authmode, "modo de autenticación a usar", "%hhu", (YBX_POST_VAR_REQUIRED|YBX_POST_VAR_NONEMPTY), authmode)
  if (!clientError) {
    if (!(authmode >= 0 && authmode <= 5)) {
      clientError = true;
      responseMsg = "Modo de autenticación inválido";
    }
  }

  if (!clientError) {
    if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
      // Recoger las credenciales si han sido especificadas, o asumir anteriores (si hay)
      YBX_ASSIGN_STR_FROM_POST(identity, "identidad WPA", (YBX_POST_VAR_NONEMPTY|YBX_POST_VAR_TRIM), tempNetwork.cred.identity)
      YBX_ASSIGN_STR_FROM_POST(password, "contraseña WPA", (YBX_POST_VAR_NONEMPTY|YBX_POST_VAR_TRIM), tempNetwork.cred.password)

      tempNetwork.cred.psk.clear();
    } else if (authmode != WIFI_AUTH_OPEN) {
      // Recoger la clave si se ha especificado, o asumir anterior (si hay)
      YBX_ASSIGN_STR_FROM_POST(psk, "contraseña PSK", (YBX_POST_VAR_NONEMPTY|YBX_POST_VAR_TRIM), tempNetwork.cred.psk)
      if (!clientError && tempNetwork.cred.psk.length() < 8) {
        clientError = true;
        responseMsg = "Contraseña para esta red debe ser como mínimo de 8 caracteres";
      }

      tempNetwork.cred.identity.clear();
      tempNetwork.cred.password.clear();
    } else {
        tempNetwork.cred.psk.clear();
        tempNetwork.cred.identity.clear();
        tempNetwork.cred.password.clear();
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

    auto oldPinned = _selNetwork;
    _selNetwork = (pinNetwork) ? idx : -1;

    // Mandar a guardar el vector modificado
    _saveNetworksToNVRAM();
    if (switch2net && _assumeControlOfWiFi) {
      _useTrialNetworkFirst = true;
      _trialNetwork = tempNetwork.cred;
    }

    if (oldPinned != _selNetwork) _publishWiFiStatus();
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }
  unsigned int httpCode = 202;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
#else
  JsonDocument json_doc;
#endif
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
    auto oldPinned = _selNetwork;
    if (_selNetwork == idx) _selNetwork = -1;
    if (idx < _savedNetworks.size() - 1) {
      // Copiar último elemento encima del que se elimina
      _savedNetworks[idx] = _savedNetworks[_savedNetworks.size() - 1];
      _savedNetworks[idx]._dirty = true;
    }
    _savedNetworks.pop_back();

    // Mandar a guardar el vector modificado
    _saveNetworksToNVRAM();

    if (oldPinned != _selNetwork) _publishWiFiStatus();
  } else if (!deleteconnected) {
    request->send(404, "application/json", "{\"msg\":\"No existe la red indicada\"}");
    return;
  }

  request->send(204);

  if (deleteconnected && _assumeControlOfWiFi) {
    if (_savedNetworks.size() > 0 || (_pEvents != NULL && _pEvents->count() > 0)) {
      _startCondRescanTimer(true);
    } else {
      log_d("- no hay redes guardadas y no hay conexiones SSE, se evita escaneo.");
    }
  }
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_connection_pin_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;
  bool pinNetwork = false;

  if (!clientError) {
    if (request->hasParam("pin", true)) {
      p = request->getParam("pin", true);
      pinNetwork = (p->value() != "0");
    } else {
      clientError = true;
      responseMsg = "Se requiere nuevo estado de anclaje de red";
    }
  }

  if (!clientError) {
    auto oldPinned = _selNetwork;

    if (pinNetwork) {
      wl_status_t currNetStatus = WiFi.status();
      if (currNetStatus != WL_CONNECTED) {
        serverError = true;
        responseMsg = "No hay conexión actualmente activa";
      } else {
        String currNet = WiFi.SSID();

        auto idx = -1;
        for (auto i = 0; i < _savedNetworks.size(); i++) {
          if (_savedNetworks[i].cred.ssid == currNet) {
            idx = i;
            break;
          }
        }

        if (idx == -1) {
          // Red actualmente conectada no se encuentra (???)
          serverError = true;
          responseMsg = "Red actualmente conectada no ha sido guardada: " + currNet;
        } else {
          _selNetwork = idx;
        }
      }
    } else {
      _selNetwork = -1;
    }

    if (!serverError) {
      if (oldPinned != _selNetwork) {
        _saveNetworksToNVRAM();
        _publishWiFiStatus();
      }
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }

  YBX_STD_RESPONSE
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
#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(4)> json_doc;
#else
  JsonDocument json_doc;
#endif

  json_doc["ssid"] = _savedNetworks[i].cred.ssid.c_str();
  json_doc["psk"] = false;
  json_doc["identity"] = false;
  json_doc["password"] = false;
  if (_savedNetworks[i].cred.psk.length() > 0) json_doc["psk"] = true;
  if (_savedNetworks[i].cred.identity.length() > 0) json_doc["identity"] = true;
  if (_savedNetworks[i].cred.password.length() > 0) json_doc["password"] = true;

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

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_softap_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  bool n_enable_softap = _enableSoftAP;
  bool n_softap_hide = _softAPHide;

  if (!clientError) {
    if (request->hasParam("enable_softap", true)) {
      p = request->getParam("enable_softap", true);
      n_enable_softap = (p->value() != "0");
    }
    if (request->hasParam("softap_hide", true)) {
      p = request->getParam("softap_hide", true);
      n_softap_hide = (p->value() != "0");
    }
  }

  if (!clientError) {
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_wifi, false);

    if (!serverError) {
      if (!nvram.putBool("net/softAP", n_enable_softap)) {
        serverError = true;
        responseMsg = "No se puede guardar nuevo estado softAP";
      }
    }
    if (!serverError) {
      if (!nvram.putBool("net/softAPHide", n_softap_hide)) {
        serverError = true;
        responseMsg = "No se puede guardar estado de softAP escondido";
      }
    }
  }

  if (!clientError && !serverError) {
    toggleStateAP(false);
    _softAPHide = n_softap_hide;
    toggleStateAP(n_enable_softap);
    _publishWiFiStatus();
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }
  unsigned int httpCode = 202;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
#else
  JsonDocument json_doc;
#endif
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_softapname_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";
  AsyncWebParameter * p;

  bool reboot = false;
  String n_softap_name = _apName;

  if (!clientError) {
    if (request->hasParam("softap_name", true)) {
      p = request->getParam("softap_name", true);
      n_softap_name = p->value();

      if (n_softap_name.length() > 0) {
        String mymac = _getWiFiMAC();

        String uc = n_softap_name;
        uc.toUpperCase();

        auto idx = uc.indexOf(mymac);
        if (idx != -1) {
          n_softap_name =
            n_softap_name.substring(0, idx) +
            "{MAC}" +
            n_softap_name.substring(idx + mymac.length());
        }
        log_d("Plantilla para softAP es ahora: %s", n_softap_name.c_str());
      } else {
        // Escenario de resetear valor en nvram, no se hace nada
      }
    }
  }

  if (!clientError) {
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_wifi, false);

    if (!serverError) {
      if (n_softap_name.length() > 0) {
        if (!nvram.putString("net/apname", n_softap_name)) {
          serverError = true;
          responseMsg = "No se puede guardar nueva plantilla de nombre softAP";
        } else {
          reboot = true;
        }
      } else {
        if (nvram.isKey("net/apname") && !nvram.remove("net/apname")) {
          serverError = true;
          responseMsg = "No se puede resetear plantilla de nombre softAP";
        } else {
          reboot = true;
        }
      }
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
#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> json_doc;
#else
  JsonDocument json_doc;
#endif
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();
  json_doc["reboot"] = reboot;

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_skipwifiafterdeepsleep_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

#if ARDUINOJSON_VERSION_MAJOR <= 6
  StaticJsonDocument<JSON_OBJECT_SIZE(1)> json_doc;
#else
  JsonDocument json_doc;
#endif

  json_doc["skip_on_wakeup_deepsleep"] = _skipWiFiOnWakeupDeepSleep;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxWiFiClass::_routeHandler_yuboxAPI_wificonfig_skipwifiafterdeepsleep_POST(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";

  uint8_t n_skipWiFiOnWakeupDeepSleep = _skipWiFiOnWakeupDeepSleep ? 1 : 0;

  YBX_ASSIGN_NUM_FROM_POST(skip_on_wakeup_deepsleep, "Omitir WiFi al despertar de deep-sleep", "%hhu", YBX_POST_VAR_REQUIRED|YBX_POST_VAR_NONEMPTY, n_skipWiFiOnWakeupDeepSleep)

  if (!clientError) {
    Preferences nvram;
    nvram.begin(_ns_nvram_yuboxframework_wifi, false);

    if (!serverError) {
      if (!nvram.putBool("net/skipOnDS", (n_skipWiFiOnWakeupDeepSleep != 0))) {
        serverError = true;
        responseMsg = "No se puede guardar bandera de WiFi luego de deep-sleep";
      } else {
        _skipWiFiOnWakeupDeepSleep = (n_skipWiFiOnWakeupDeepSleep != 0);
      }
    }
  }

  if (!clientError && !serverError) {
    responseMsg = "Parámetros actualizados correctamente";
  }

  YBX_STD_RESPONSE
}

void _cb_YuboxWiFiClass_wifiRescan(TimerHandle_t timer)
{
  YuboxWiFiClass *self = (YuboxWiFiClass *)pvTimerGetTimerID(timer);
  return self->_cbHandler_WiFiRescan(timer);
}

void _cb_YuboxWiFiClass_wifiNoIpTimeout(TimerHandle_t timer)
{
  YuboxWiFiClass *self = (YuboxWiFiClass *)pvTimerGetTimerID(timer);
  return self->_cbHandler_wifiNoIpTimeout(timer);
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

void YuboxWiFiClass::_cbHandler_wifiNoIpTimeout(TimerHandle_t timer)
{
  log_v("YuboxWiFiClass::_cbHandler_wifiNoIpTimeout");
  if (WiFi.status() != WL_CONNECTED) {
    log_d("Todavía no se consigue IP, se vuelve a intentar conexión...");
    _connectToActiveNetwork();
  }
}

YuboxWiFiClass YuboxWiFi;
