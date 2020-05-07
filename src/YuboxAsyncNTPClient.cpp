#include <Arduino.h>

#include "YuboxAsyncNTPClient.h"
#include <functional>

YuboxAsyncNTPClient::YuboxAsyncNTPClient(void)
{
  _connectResult = ERR_INPROGRESS;
  setPoolServerName("pool.ntp.org");
  setUpdateInterval(60000);

  _udp.onPacket(std::bind(&YuboxAsyncNTPClient::_onPacket, this, std::placeholders::_1));
}

void YuboxAsyncNTPClient::setUpdateInterval(unsigned long i)
{
  if (i == _currUpdateInterval) return;
  _currUpdateInterval = i;
  if (_ntpQueryTimer != NULL) {
    xTimerChangePeriod(_ntpQueryTimer, pdMS_TO_TICKS(_currUpdateInterval), pdMS_TO_TICKS(100));
  }
}

void YuboxAsyncNTPClient::setPoolServerName(const char * s)
{
  _ntpServer = s;
}

void _cb_YuboxAsyncNTPClient_updateNTP(TimerHandle_t);
void _cb_YuboxAsyncNTPClient_lookupDNS(const char *, const ip_addr_t *, void *);
void YuboxAsyncNTPClient::begin()
{
  // Timer created but NOT started
  _ntpQueryTimer = xTimerCreate(
    "YuboxAsyncNTPClient",
    pdMS_TO_TICKS(5),
    pdTRUE,
    (void *)this,
    _cb_YuboxAsyncNTPClient_updateNTP);

  // Query IP address from hostname
  ip_addr_t addr;
  err_t err = dns_gethostbyname(_ntpServer, &addr, _cb_YuboxAsyncNTPClient_lookupDNS, this);
  if (err == ERR_OK && addr.u_addr.ip4.addr) {
    // Dirección resuelta inmediatamente, se intenta conectar
    _connectResult = ERR_INPROGRESS;
    _ntpAddr = addr;
    xTimerStart(_ntpQueryTimer, 0);
  } else {
    _connectResult = err;
  }
}

void _cb_YuboxAsyncNTPClient_lookupDNS(const char * n, const ip_addr_t * a, void * arg)
{
  YuboxAsyncNTPClient *self = (YuboxAsyncNTPClient *)arg;
  self->_lookupDNS(n, a);
}

void YuboxAsyncNTPClient::_lookupDNS(const char * n, const ip_addr_t * a)
{
  memcpy(&_ntpAddr, a, sizeof(ip_addr_t));
  xTimerStart(_ntpQueryTimer, 0);
}

void YuboxAsyncNTPClient::end()
{
  if (_ntpQueryTimer != NULL) {
    xTimerStop(_ntpQueryTimer, 0);
    xTimerDelete(_ntpQueryTimer, 0);
    _ntpQueryTimer = NULL;
  }
  
  if (!_udpStarted) return;
  _udp.close();
  _udpStarted = false;
  _connectResult = ERR_INPROGRESS;
}

void _cb_YuboxAsyncNTPClient_updateNTP(TimerHandle_t timer)
{
  YuboxAsyncNTPClient *self = (YuboxAsyncNTPClient *)pvTimerGetTimerID(timer);
  return self->_updateNTP();
}

#define NTP_PACKET_SIZE 48

void YuboxAsyncNTPClient::_updateNTP(void)
{
  uint8_t pkt[NTP_PACKET_SIZE];

  if (!_udpStarted) {
    if (!_udp.connect(&_ntpAddr, 123)) {
      _connectResult = ERR_CONN;
      return;
    }
    xTimerChangePeriod(_ntpQueryTimer, pdMS_TO_TICKS(_currUpdateInterval), 0);
    _connectResult = ERR_OK;
    _udpStarted = true;
  }

  memset(pkt, 0, NTP_PACKET_SIZE);
  pkt[0] = 0b11100011;   // LI, Version, Mode
  pkt[1] = 0;     // Stratum, or type of clock
  pkt[2] = 6;     // Polling Interval
  pkt[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  pkt[12]  = 49;
  pkt[13]  = 0x4E;
  pkt[14]  = 49;
  pkt[15]  = 52;

  _udp.write(pkt, NTP_PACKET_SIZE);
  _ts_lastRequest_millis = millis();
}

void YuboxAsyncNTPClient::_onPacket(AsyncUDPPacket & packet)
{
  if (packet.length() >= 44) {
    _ts_lastResponse_millis = millis();
    const uint8_t * d = packet.data();
    uint32_t secs = ((uint32_t)d[40] << 24) | ((uint32_t)d[41] << 16) | ((uint32_t)d[42] << 8) | ((uint32_t)d[43]);
    _ts_ntpEpoch = secs - 2208988800UL;
    _ts_lastUpdate_millis = _ts_lastRequest_millis + ((_ts_lastResponse_millis - _ts_lastRequest_millis) >> 1);
  }
}

uint64_t YuboxAsyncNTPClient::time_ms(void)
{
  uint64_t r = _ts_ntpEpoch;
  r *= 1000;
  r += (millis() - _ts_lastUpdate_millis);
  return r;
}

time_t YuboxAsyncNTPClient::time(time_t * t)
{
  if (!isTimeValid()) return (time_t)(-1);
  time_t r = _ts_ntpEpoch;
  r += (millis() - _ts_lastUpdate_millis) / 1000;
  if (t != NULL) *t = r;
  return r;
}

struct tm * YuboxAsyncNTPClient::gmtime_r(struct tm * result)
{
  if (!isTimeValid() || result == NULL) return NULL;
  time_t t = time(NULL);
  return ::gmtime_r(&t, result);
}

void YuboxAsyncNTPClient::setTimeZone(long offset, int daylight)
{
  char cst[17] = {0};
  char cdt[17] = "DST";
  char tz[33] = {0};

  offset = -offset;

  if(offset % 3600){
      sprintf(cst, "UTC%ld:%02u:%02u", offset / 3600, abs((offset % 3600) / 60), abs(offset % 60));
  } else {
      sprintf(cst, "UTC%ld", offset / 3600);
  }
  if(daylight != 3600){
      long tz_dst = offset - daylight;
      if(tz_dst % 3600){
          sprintf(cdt, "DST%ld:%02u:%02u", tz_dst / 3600, abs((tz_dst % 3600) / 60), abs(tz_dst % 60));
      } else {
          sprintf(cdt, "DST%ld", tz_dst / 3600);
      }
  }
  sprintf(tz, "%s%s", cst, cdt);
  setenv("TZ", tz, 1);
  tzset();
}

struct tm * YuboxAsyncNTPClient::localtime_r(struct tm * result)
{
  if (!isTimeValid() || result == NULL) return NULL;
  time_t t = time(NULL);
  return ::localtime_r(&t, result);
}
