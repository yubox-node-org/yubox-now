#ifndef _YUBOX_ASYNC_NTP_CLIENT_H_
#define _YUBOX_ASYNC_NTP_CLIENT_H_

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "Stream.h"
#include "AsyncUDP.h"

extern "C" {
#include "lwip/ip_addr.h"
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/dns.h"
}

class YuboxAsyncNTPClient
{
private:

  AsyncUDP _udp;
  bool _udpStarted = false;
  const char * _ntpServer = NULL;
  unsigned long _ts_lastRequest_millis = 0;
  unsigned long _ts_lastResponse_millis = 0;
  unsigned long _ts_lastUpdate_millis = 0;
  unsigned long _ts_ntpEpoch = 0;

  TimerHandle_t _ntpQueryTimer = NULL;
  unsigned int _currUpdateInterval = 0;

  ip_addr_t _ntpAddr;
  err_t _connectResult; 

  void _updateNTP(void);
  void _lookupDNS(const char *, const ip_addr_t *);
  //err_t _beginCommon(const ip_addr_t *);

  void _onPacket(AsyncUDPPacket &);

public:

  YuboxAsyncNTPClient(void);
  void setUpdateInterval(unsigned long);
  void setPoolServerName(const char *);
  void setTimeZone(long offset, int daylight = 0);
  void begin();
  void end(void);

  err_t connectResult(void) { return _connectResult; }
  bool isTimeValid(void)  { return (_ts_ntpEpoch != 0); }

  time_t time(time_t * t = NULL);
  uint64_t time_ms(void);

  struct tm * gmtime_r(struct tm *);
  struct tm * localtime_r(struct tm *);

  friend void _cb_YuboxAsyncNTPClient_updateNTP(TimerHandle_t);
  friend void _cb_YuboxAsyncNTPClient_lookupDNS(const char *, const ip_addr_t *, void *);
};

#endif
