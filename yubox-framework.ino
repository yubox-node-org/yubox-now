#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <string>
#include <unordered_map>

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

String mDNS_hostname;

const char * ns_nvram_yuboxframework_wifi = "YUBOX/WiFi";

String wifi_ssid;
String wifi_password;

bool wifiInit = false;
bool wifiConectado = false;

TimerHandle_t wifiReconnectTimer;

AsyncWebServer server(80);
void setupAsyncServerHTTP(void);

void setupMDNS(String &);
String generarHostnameMAC(void);
void iniciarWifi(void);

void setup()
{
  mDNS_hostname = generarHostnameMAC();

  // La siguiente demora es sólo para comodidad de desarrollo para enchufar el USB
  // y verlo en gtkterm. No es en lo absoluto necesaria como algoritmo requerido.
  delay(3000);
  Serial.begin(115200);

  // TODO: quitar esto. Esto es sólo para inicializar NVRAM con credenciales antes de desarollo
  Preferences * nvram = new Preferences;
  nvram->begin(ns_nvram_yuboxframework_wifi, false);
  uint32_t numNets = nvram->getUInt("net/n");
  Serial.printf("DEVEL: número de redes guardadas: %u\r\n", numNets);
  if (numNets > 0) {
    int32_t selNet = nvram->getUInt("net/sel");
    Serial.printf("Seleccionada la red conocida %d de %u\r\n", selNet, numNets);
    wifi_ssid = nvram->getString("net/1/ssid");
    wifi_password = nvram->getString("net/1/psk");
  } else {
    Serial.println("NO HAY REDES EN NVRAM. Sólo con HostAP");
  }
  delete nvram; nvram = NULL;

  Serial.println("Inicializando SPIFFS...");
  if (!SPIFFS.begin(true)){
    Serial.println("Ha ocurrido un error al montar SPIFFS");
    return;
  }

  setupAsyncServerHTTP();

  wifiReconnectTimer = xTimerCreate(
    "wifiTimer",
    pdMS_TO_TICKS(2000),
    pdFALSE,
    (void*)0,
    reinterpret_cast<TimerCallbackFunction_t>(iniciarWifi));

  WiFi.onEvent(WiFiEvent);
  iniciarWifi();

  server.begin();

  setupMDNS(mDNS_hostname);
}

void loop()
{
}

void iniciarServiciosRed(void);
void WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\r\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        wifiConectado = true;

        Serial.println("Conectado al WiFi.");
        Serial.println("Dirección IP: ");
        Serial.println(WiFi.localIP());
        iniciarServiciosRed();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        wifiConectado = false;

        Serial.println("Se perdió conexión WiFi.");
        xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void iniciarWifi(void)
{
  Serial.println("Iniciando conexión a WiFi...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(mDNS_hostname.c_str());
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
}

void iniciarServiciosRed(void)
{
  if (wifiInit) return;

/*
  // NOTA: este es un bucle bloqueante. Debería implementárselo de otra manera.
  Serial.print("Pidiendo hora de red vía NTP...");
  timeClient.begin();
  while(!timeClient.update()) {
    timeClient.forceUpdate();
    Serial.print(".");
  }
  Serial.print(timeClient.getFormattedTime()); Serial.println(" UTC");
*/
  wifiInit = true;
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

// TODO: mover estas rutinas a bibliotecas separadas
void setupHTTPRoutes_WiFi(AsyncWebServer & srv);

void setupAsyncServerHTTP(void)
{
  server.serveStatic("/", SPIFFS, "/");

  // Rutinas que instalan componentes del api de Yubox Framework
  setupHTTPRoutes_WiFi(server);

  server.onNotFound(notFound);
}


void routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request);
void routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request);
void routeHandler_yuboxAPI_wificonfig_connection_PUT(AsyncWebServerRequest *request);
void routeHandler_yuboxAPI_wificonfig_connection_DELETE(AsyncWebServerRequest *request);

void setupHTTPRoutes_WiFi(AsyncWebServer & srv)
{
  srv.on("/yubox-api/wificonfig/networks", HTTP_GET, routeHandler_yuboxAPI_wificonfig_networks_GET);
  srv.on("/yubox-api/wificonfig/connection", HTTP_GET, routeHandler_yuboxAPI_wificonfig_connection_GET);
  //srv.on("/yubox-api/wificonfig/connection", HTTP_PUT, routeHandler_yuboxAPI_wificonfig_connection_PUT);
  //srv.on("/yubox-api/wificonfig/connection", HTTP_DELETE, routeHandler_yuboxAPI_wificonfig_connection_DELETE);
}

std::unordered_map<std::string, String> cargarRedesConocidas(Preferences &);

void routeHandler_yuboxAPI_wificonfig_networks_GET(AsyncWebServerRequest *request)
{
  // Cargar de NVRAM todas las redes conocidas, mapeando a su índice correspondiente
  Preferences nvram;
  nvram.begin(ns_nvram_yuboxframework_wifi, true);
  std::unordered_map<std::string, String> redes = cargarRedesConocidas(nvram);
  std::unordered_map<std::string, String>::iterator it;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(10));

  response->print("[");
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
  } else if (n > 0) {
    String currNet = WiFi.SSID();
    String currBssid = WiFi.BSSIDstr();
    wl_status_t currNetStatus = WiFi.status();

    for (int i = 0; i < n; i++) {
      if (i > 0) response->print(",");

      String temp_bssid = WiFi.BSSIDstr(i);
      String temp_ssid = WiFi.SSID(i);
      String temp_psk;
      String temp_identity;
      String temp_password;

      json_doc["bssid"] = temp_bssid.c_str();
      json_doc["ssid"] = temp_ssid.c_str();
      json_doc["channel"] = WiFi.channel(i);
      json_doc["rssi"] = WiFi.RSSI(i);
      json_doc["authmode"] = (uint8_t)WiFi.encryptionType(i);

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
      it = redes.find(temp_ssid.c_str());
      if (it != redes.end()) {
        String k;

        k = it->second + "psk";
        temp_psk = nvram.getString(k.c_str());
        if (!temp_psk.isEmpty()) json_doc["psk"] = temp_psk.c_str();

        k = it->second + "identity";
        temp_identity = nvram.getString(k.c_str());
        if (!temp_identity.isEmpty()) json_doc["identity"] = temp_identity.c_str();

        k = it->second + "password";
        temp_password = nvram.getString(k.c_str());
        if (!temp_password.isEmpty()) json_doc["password"] = temp_password.c_str();
      }

      serializeJson(json_doc, *response);
    }

    WiFi.scanDelete();
    if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);
    }
  }

  response->print("]");

  request->send(response);
}

void routeHandler_yuboxAPI_wificonfig_connection_GET(AsyncWebServerRequest *request)
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

// TODO: clase definitiva debe cachear y llamar equivalente a esto una sola vez
std::unordered_map<std::string, String> cargarRedesConocidas(Preferences & nvram)
{
  std::unordered_map<std::string, String> redes;
  uint32_t numNets = nvram.getUInt("net/n");
  char t1[64];
  for (int i = 1; i <= numNets; i++) {
    sprintf(t1, "net/%d/ssid", i);
    String ssid = nvram.getString(t1);
    std::string s_ssid = ssid.c_str();
    sprintf(t1, "net/%d/", i);
    redes[s_ssid] = t1;
  }

  return redes;
}

void setupMDNS(String & mdnsHostname)
{
  if (!MDNS.begin(mdnsHostname.c_str())) {
    Serial.println("ERROR: no se puede iniciar mDNS para anunciar hostname!");
    return;
  }
  Serial.print("Iniciando mDNS con nombre de host: ");
  Serial.print(mdnsHostname);
  Serial.println(".local");

  MDNS.addService("http", "tcp", 80);
}

String generarHostnameMAC(void)
{
  byte maccopy[6];
  memset(maccopy, 0, sizeof(maccopy));
  WiFi.macAddress(maccopy);

  String hostname = "YUBOX-";
  for (auto i = 0; i < sizeof(maccopy); i++) {
    String octet = String(maccopy[i], 16);
    octet.toUpperCase();
    hostname += octet;
  }
  return hostname;
}


