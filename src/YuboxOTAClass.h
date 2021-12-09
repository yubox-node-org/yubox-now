#ifndef _YUBOX_OTA_CLASS_H_
#define _YUBOX_OTA_CLASS_H_

#include <ESPAsyncWebServer.h>
#include "YuboxWebAuthClass.h"

extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

#include "YuboxOTA_Streamer.h"
#include "YuboxOTA_Flasher.h"

#include "FS.h"
#include <vector>
#include <functional>

typedef String (*YuboxOTA_Veto_cb)(bool isReboot);
typedef std::function<String (bool isReboot) > YuboxOTA_Veto_func_cb;

typedef size_t yuboxota_event_id_t;

typedef std::function<YuboxOTA_Flasher * (void)> YuboxOTA_Flasher_Factory_func_cb;

class YuboxOTAClass
{
private:
  // Rechazar el resto de los fragmentos de upload si primera revisión falla
  bool _uploadRejected;

  // Bandera de reinicio requerido para aplicar cambios
  bool _shouldReboot;

  // Bandera para indicar que ya se invocó la finalización del flasheo
  bool _uploadFinished;

  // Cuenta de datos subidos del archivo para reportar en eventos
  unsigned long _tgzupload_rawBytesReceived;

  // Datos requeridos para manejar la descompresión gzip
  YuboxOTA_Streamer * _streamerImpl;

  // Datos requeridos para manejar el parseo tar
  entry_callbacks_t _tarCB;             // Callbacks a llamar en cabecera, datos, final de archivos
  unsigned int _tar_emptyChunk;         // Número de bloques de 512 bytes llenos de ceros contiguos
  bool _tar_eof;                        // Bandera de si se llegó a fin normal de tar

  // Descripción de errores posibles al subir el tar.gz
  bool _tgzupload_clientError;
  bool _tgzupload_serverError;
  String _tgzupload_responseMsg;

  // Operación de actualización en sí
  YuboxOTA_Flasher * _flasherImpl;
  TimerHandle_t _timer_restartYUBOX;

  AsyncEventSource * _pEvents;
  unsigned long _tgzupload_lastEventSent;

  void _setupHTTPRoutes(AsyncWebServer &);

  void _routeHandler_yuboxAPI_yuboxOTA_firmwarelistjson_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_yuboxOTA_tgzupload_POST(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload(AsyncWebServerRequest *,
    String filename, size_t index, uint8_t *data, size_t len, bool final);
  void _routeHandler_yuboxAPI_yuboxOTA_rollback_GET(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_yuboxOTA_rollback_POST(AsyncWebServerRequest *);
  void _routeHandler_yuboxAPI_yuboxOTA_reboot_POST(AsyncWebServerRequest *);

  void _routeHandler_spiffslist_GET(AsyncWebServerRequest *request);
  void _routeHandler_yuboxhwreport_GET(AsyncWebServerRequest *request);

  void _handle_tgzOTAchunk(size_t index, uint8_t *data, size_t len, bool final);

  // Verificación de veto sobre operación de flasheo o reinicio
  String _checkOTA_Veto(bool isReboot);

  // Las siguientes funciones son llamadas por la correspondiente friend del mismo nombre
  int _tar_cb_feedFromBuffer(unsigned char *, size_t);
  int _tar_cb_gotEntryHeader(header_translated_t *, int);
  int _tar_cb_gotEntryData(header_translated_t *, int, unsigned char *, int);
  int _tar_cb_gotEntryEnd(header_translated_t *, int);

  void _emitUploadEvent_FileStart(const char * filename, bool isfirmware, unsigned long size);
  void _emitUploadEvent_FileProgress(const char * filename, bool isfirmware, unsigned long size, unsigned long offset);
  void _emitUploadEvent_FileEnd(const char * filename, bool isfirmware, unsigned long size);

  YuboxOTA_Flasher * _getESP32FlasherImpl(void);

  int _idxFlasherFromURL(String);
  YuboxOTA_Flasher * _buildFlasherFromIdx(int);
  YuboxOTA_Flasher * _buildFlasherFromURL(String);

  void _rejectUpload(const String &, bool serverError);
  void _rejectUpload(const char *, bool serverError);

public:
  YuboxOTAClass(void);
  void begin(AsyncWebServer & srv);

  // Registro de un callback que permite vetar condicionalmente el flasheo y/o
  // el reinicio del dispositivo. Si el callback decide que la operación NO debe
  // permitirse, debe de devolver una cadena no-vacía con un mensaje a mostrar
  // en alguna interfaz indicando el motivo del veto. De lo contrario, se debe
  // devolver una cadena vacía para que la operación continúe normalmente. Si se
  // registran múltiples callbacks simultáneos, se llamarán secuencialmente y
  // TODOS deben permitir la operación para que proceda.
  yuboxota_event_id_t onOTAUpdateVeto(YuboxOTA_Veto_cb cbVeto);
  yuboxota_event_id_t onOTAUpdateVeto(YuboxOTA_Veto_func_cb cbVeto);
  void removeOTAUpdateVeto(YuboxOTA_Veto_cb cbVeto);
  void removeOTAUpdateVeto(yuboxota_event_id_t id);

  // Para invocar al arranque del YUBOX y limpiar archivos de una subida fallida
  void cleanupFailedUpdateFiles(void);

  void addFirmwareFlasher(AsyncWebServer & srv, const char *, const char *, YuboxOTA_Flasher_Factory_func_cb);

  friend int _tar_cb_feedFromBuffer(unsigned char *, size_t);
  friend int _tar_cb_gotEntryHeader(header_translated_t *, int, void *);
  friend int _tar_cb_gotEntryData(header_translated_t *, int, void *, unsigned char *, int);
  friend int _tar_cb_gotEntryEnd(header_translated_t *, int, void *);

  static void _cbHandler_restartYUBOX(TimerHandle_t);
};

extern YuboxOTAClass YuboxOTA;

#endif
