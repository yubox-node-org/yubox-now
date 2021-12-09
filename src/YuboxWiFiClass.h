#ifndef _YUBOX_WIFI_CLASS_H_
#define _YUBOX_WIFI_CLASS_H_

#include "FS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <vector>

#include "YuboxWebAuthClass.h"

// Estructura para representar credenciales de una red WiFi
typedef struct {
  String ssid;
  String psk;
  String identity;
  String password;
} YuboxWiFi_cred;

typedef struct {
  // Los siguientes 4 parámetros se leen de la NVRAM al arranque
  YuboxWiFi_cred cred;

  // Bandera de red usada por última vez. Esta bandera no debe confundirse con
  // la bandera de pin de red (net/sel). La bandera de pin de red indica la
  // red que TIENE que usarse como activa. La bandera de aquí es la red que
  // EFECTIVAMENTE fue seleccionada. Esto es necesario para implementar la
  // función getLastActiveNetwork().
  bool selectedNet;

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

  // Si != NULL, este AsyncWebServer debe iniciarse con ->begin() cuando WiFi esté listo
  AsyncWebServer * _pWebSrvBootstrap;

  // ID de evento para Wifi, para poderlo desinstalar
  WiFiEventId_t _eventId_cbHandler_WiFiEvent;
  WiFiEventId_t _eventId_cbHandler_WiFiEvent_ready;
  bool _wifiReadyEventReceived;

  // Bandera de asumir control del WiFi o no.
  bool _assumeControlOfWiFi;

  // Bandera de activar la interfaz softAP o no
  bool _enableSoftAP;
  bool _softAPConfigured;

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
  YuboxWiFi_cred _activeNetwork;
  YuboxWiFi_cred _trialNetwork;
  bool _useTrialNetworkFirst;

  AsyncEventSource * _pEvents;

  // Timers asociados a llamadas de métodos
  TimerHandle_t _timer_wifiRescan;
  bool _disconnectBeforeRescan;
  uint32_t _interval_wifiRescan;

  String _getWiFiMAC(void);
  void _loadOneNetworkFromNVRAM(Preferences &, uint32_t, YuboxWiFi_nvramrec &);
  void _saveOneNetworkToNVRAM(Preferences &, uint32_t, YuboxWiFi_nvramrec &);
  void _loadSavedNetworksFromNVRAM(void);
  void _saveNetworksToNVRAM(void);
  void _updateActiveNetworkNVRAM(void);
  void _enableWiFiMode(void);
  void _startWiFi(void);
  void _collectScannedNetworks(void);
  void _chooseKnownScannedNetwork(void);
  void _connectToActiveNetwork(void);
  void _startCondRescanTimer(bool);

  String _buildAvailableNetworksJSONReport(void);

  void _bootstrapWebServer(void);

  // Callbacks y timers
  void _cbHandler_WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t);
  void _cbHandler_WiFiEvent_ready(WiFiEvent_t event, WiFiEventInfo_t);
  void _cbHandler_WiFiEvent_scandone(WiFiEvent_t event, WiFiEventInfo_t);
  void _cbHandler_WiFiRescan(TimerHandle_t);

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_wificonfig_netscan_onConnect(AsyncEventSourceClient *);
  void _routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_connection_PUT(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_connection_DELETE(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_networks_POST(AsyncWebServerRequest *request);
  void _routeHandler_yuboxAPI_wificonfig_networks_DELETE(AsyncWebServerRequest *request);

  // Funciones de ayuda para responder a peticiones web
  void _serializeOneSavedNetwork(AsyncResponseStream *response, uint32_t i);
  void _addOneSavedNetwork(AsyncWebServerRequest *request, bool switch2net);
  void _delOneSavedNetwork(AsyncWebServerRequest *request, String ssid, bool deleteconnected);

public:
  YuboxWiFiClass();
  void begin(AsyncWebServer & srv);
  void beginServerOnWiFiReady(AsyncWebServer *);

  String getMDNSHostname() { return _mdnsName; }
  String getAPName() { return _apName; }

  // Para estas funciones, la plantilla {MAC} se reemplazará con la cadena hex de la MAC WiFi.
  void setMDNSHostname(String &);
  void setAPName(String &);

  // Asumir control del WiFi, y ceder control. Para pasar la posta a otras bibliotecas WiFi
  void takeControlOfWiFi(void);
  void releaseControlOfWiFi(bool wifioff = false);
  bool haveControlOfWiFi(void) { return _assumeControlOfWiFi; }
  void saveControlOfWiFi(void);
  YuboxWiFi_cred getLastActiveNetwork(void);

  // Activar y desactivar únicamente la interfaz softAP. Se requiere control de
  // WiFi. Este soporte es necesario para activiar Bluetooth en simultáneo con WiFi
  // en modo cliente luego de desactivar la porción softAP.
  void toggleStateAP(bool);
  bool getSavedStateAP(void) { return _enableSoftAP; }
  void saveStateAP(void);

  friend void _cb_YuboxWiFiClass_wifiRescan(TimerHandle_t);
};

extern YuboxWiFiClass YuboxWiFi;

#endif
