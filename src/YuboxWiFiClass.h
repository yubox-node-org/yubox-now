#ifndef _YUBOX_WIFI_CLASS_H_
#define _YUBOX_WIFI_CLASS_H_

#include "FS.h"
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <vector>

#include "YuboxWebAuthClass.h"

typedef struct {
  // Los siguientes 4 parámetros se leen de la NVRAM al arranque
  String ssid;
  String psk;
  String identity;
  String password;

  // Los siguientes parámetros son para monitorear resultado de conexión a red
  uint32_t numFails;

  // Indicar si el registro tiene que escribirse a la NVRAM
  bool _dirty;
} YuboxWiFi_nvramrec;

typedef struct {
  String bssid;
  String ssid;
  int32_t channel;
  int32_t rssi;
  uint8_t authmode;
} YuboxWiFi_scanCache;

class YuboxWiFiClass
{
private:
  static const char * _ns_nvram_yuboxframework_wifi;

  // La cadena que corresponde al nombre MDNS construido
  String _mdnsName;

  // La cadena que corresponde al nombre de AP de WiFi construido
  String _apName;

  // La siguiente es una caché de los valores de las redes guardadas en NVRAM.
  // En estado estable, .size() se corresponde con valor de net/n en NVRAM
  // NOTA: elemento i-ésimo contando desde 0 se corresponde a net/n/... donde
  // n = i+1 porque el vector cuenta desde 0.
  int32_t _selNetwork;
  std::vector<YuboxWiFi_nvramrec> _savedNetworks;

  // Caché de el último escaneo de redes efectuado
  std::vector<YuboxWiFi_scanCache> _scannedNetworks;
  unsigned long _scannedNetworks_timestamp;

  // Copia de las credenciales elegidas, para asegurar vida de cadenas
  YuboxWiFi_nvramrec _activeNetwork;
  YuboxWiFi_nvramrec _trialNetwork;
  bool _useTrialNetworkFirst;

  // Timers asociados a llamadas de métodos
  TimerHandle_t _timer_wifiDisconnectRescan;

  String _getWiFiMAC(void);
  void _loadOneNetworkFromNVRAM(Preferences &, uint32_t, YuboxWiFi_nvramrec &);
  void _saveOneNetworkToNVRAM(Preferences &, uint32_t, YuboxWiFi_nvramrec &);
  void _loadSavedNetworksFromNVRAM(void);
  void _saveNetworksToNVRAM(void);
  void _startWiFi(void);
  void _collectScannedNetworks(void);
  void _chooseKnownScannedNetwork(void);
  void _connectToActiveNetwork(void);

  String _buildAvailableNetworksJSONReport(void);

  // Callbacks y timers
  void _cbHandler_WiFiEvent(WiFiEvent_t event);

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_connection_PUT(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_connection_DELETE(AsyncWebServerRequest *request);

public:
  YuboxWiFiClass();
  void begin(AsyncWebServer & srv);

  String getMDNSHostname() { return _mdnsName; }
  String getAPName() { return _apName; }

  // Para estas funciones, la plantilla {MAC} se reemplazará con la cadena hex de la MAC WiFi.
  void setMDNSHostname(String &);
  void setAPName(String &);

  static void _cbHandler_wifiDisconnectRescan(TimerHandle_t);
};

extern YuboxWiFiClass YuboxWiFi;

#endif
