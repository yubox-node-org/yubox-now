#include "YuboxOTAClass.h"

#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>

#include <functional>

extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

#include "YuboxOTA_Streamer.h"

#include "esp_task_wdt.h"

#include "YuboxOTA_Flasher_ESP32.h"
#include "YuboxOTA_Streamer_GZ.h"
#include "YuboxOTA_Streamer_Identity.h"

typedef struct YuboxOTAVetoList
{
  static yuboxota_event_id_t current_id;
  yuboxota_event_id_t id;
  YuboxOTA_Veto_cb cb;
  YuboxOTA_Veto_func_cb fcb;

  YuboxOTAVetoList() : id(current_id++), cb(NULL), fcb(NULL) {}
} YuboxOTAVetoList_t;
yuboxota_event_id_t YuboxOTAVetoList::current_id = 1;

static std::vector<YuboxOTAVetoList_t> cbVetoList;

typedef struct YuboxOTA_Flasher_Factory_rec{
  String _tag;
  String _desc;
  String _route_tgzupload;
  String _route_rollback;
  YuboxOTA_Flasher_Factory_func_cb _factory;

  YuboxOTA_Flasher_Factory_rec(String t, String d, String upload, String rb, YuboxOTA_Flasher_Factory_func_cb f)
    : _tag(t), _desc(d), _route_tgzupload(upload), _route_rollback(rb), _factory(f) {}
} YuboxOTA_Flasher_Factory_rec_t;

static std::vector<YuboxOTA_Flasher_Factory_rec_t> flasherFactoryList;

int _tar_cb_feedFromBuffer(unsigned char *, size_t);
int _tar_cb_gotEntryHeader(header_translated_t *, int, void *);
int _tar_cb_gotEntryData(header_translated_t *, int, void *, unsigned char *, int);
int _tar_cb_gotEntryEnd(header_translated_t *, int, void *);

YuboxOTAClass::YuboxOTAClass(void)
{
  _flasherImpl = NULL;

  _uploadRejected = false;
  _shouldReboot = false;
  _uploadFinished = false;
  _streamerImpl = NULL;
  _tarCB.header_cb = ::_tar_cb_gotEntryHeader;
  _tarCB.data_cb = ::_tar_cb_gotEntryData;
  _tarCB.end_cb = ::_tar_cb_gotEntryEnd;
  tinyUntarReadCallback = ::_tar_cb_feedFromBuffer;
  _pEvents = NULL;

  _timer_restartYUBOX = xTimerCreate(
    "YuboxOTAClass_restartYUBOX",
    pdMS_TO_TICKS(2000),
    pdFALSE,
    0,
    &YuboxOTAClass::_cbHandler_restartYUBOX);
}

void YuboxOTAClass::begin(AsyncWebServer & srv)
{
  srv.on("/yubox-api/yuboxOTA/hwreport.json", HTTP_GET,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxhwreport_GET, this, std::placeholders::_1));
  srv.on("/_spiffslist.html", HTTP_GET,
    std::bind(&YuboxOTAClass::_routeHandler_spiffslist_GET, this, std::placeholders::_1));

  srv.on("/yubox-api/yuboxOTA/firmwarelist.json", HTTP_GET,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_firmwarelistjson_GET, this, std::placeholders::_1));
  srv.on("/yubox-api/yuboxOTA/reboot", HTTP_POST,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_reboot_POST, this, std::placeholders::_1));
  addFirmwareFlasher(srv, "esp32", "YUBOX ESP32 Firmware", std::bind(&YuboxOTAClass::_getESP32FlasherImpl, this));

  _pEvents = new AsyncEventSource("/yubox-api/yuboxOTA/events");
  YuboxWebAuth.addManagedHandler(_pEvents);
  srv.addHandler(_pEvents);
}

void YuboxOTAClass::addFirmwareFlasher(AsyncWebServer & srv, const char * tag, const char * desc, YuboxOTA_Flasher_Factory_func_cb factory_cb)
{
  String route_tgzupload = "/yubox-api/yuboxOTA/";
  route_tgzupload += tag;
  route_tgzupload += "/tgzupload";
  String route_rollback = "/yubox-api/yuboxOTA/";
  route_rollback += tag;
  route_rollback += "/rollback";

  flasherFactoryList.emplace_back(tag, desc, route_tgzupload, route_rollback, factory_cb);

  srv.on(route_tgzupload.c_str(), HTTP_POST,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_POST, this, std::placeholders::_1),
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
  srv.on(route_rollback.c_str(), HTTP_GET,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_rollback_GET, this, std::placeholders::_1));
  srv.on(route_rollback.c_str(), HTTP_POST,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_rollback_POST, this, std::placeholders::_1));
}

int YuboxOTAClass::_idxFlasherFromURL(String url)
{
  for (auto i = 0; i < flasherFactoryList.size(); i++) {
    if (url == flasherFactoryList[i]._route_tgzupload) return i;
    if (url == flasherFactoryList[i]._route_rollback) return i;
  }
  return -1;
}

YuboxOTA_Flasher * YuboxOTAClass::_buildFlasherFromIdx(int idx)
{
  if (idx < 0 || idx >= flasherFactoryList.size()) return NULL;
  YuboxOTA_Flasher * f = flasherFactoryList[idx]._factory();
  f->setProgressCallbacks(
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileStart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileEnd, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
  return f;
}

YuboxOTA_Flasher * YuboxOTAClass::_buildFlasherFromURL(String url)
{
  int idx = _idxFlasherFromURL(url);
  return (idx >= 0) ? _buildFlasherFromIdx(idx) : NULL;
}

yuboxota_event_id_t YuboxOTAClass::onOTAUpdateVeto(YuboxOTA_Veto_cb cbVeto)
{
  if (!cbVeto) return 0;

  YuboxOTAVetoList_t n_evHandler;
  n_evHandler.cb = cbVeto;
  n_evHandler.fcb = NULL;
  cbVetoList.push_back(n_evHandler);
  return n_evHandler.id;
}

yuboxota_event_id_t YuboxOTAClass::onOTAUpdateVeto(YuboxOTA_Veto_func_cb cbVeto)
{
  if (!cbVeto) return 0;

  YuboxOTAVetoList_t n_evHandler;
  n_evHandler.fcb = NULL;
  n_evHandler.fcb = cbVeto;
  cbVetoList.push_back(n_evHandler);
  return n_evHandler.id;
}

void YuboxOTAClass::removeOTAUpdateVeto(YuboxOTA_Veto_cb cbVeto)
{
  if (!cbVeto) return;

  for (auto i = 0; i < cbVetoList.size(); i++) {
    YuboxOTAVetoList_t entry = cbVetoList[i];
    if (entry.cb == cbVeto) cbVetoList.erase(cbVetoList.begin() + i);
  }
}

void YuboxOTAClass::removeOTAUpdateVeto(yuboxota_event_id_t id)
{
  for (auto i = 0; i < cbVetoList.size(); i++) {
    YuboxOTAVetoList_t entry = cbVetoList[i];
    if (entry.id == id) cbVetoList.erase(cbVetoList.begin() + i);
  }
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_firmwarelistjson_GET(AsyncWebServerRequest * request)
{
    YUBOX_RUN_AUTH(request);

    // Construir tabla de flasheadores disponibles
    String json_tableOutput = "[";
    StaticJsonDocument<JSON_OBJECT_SIZE(4)> json_tablerow;
    for (auto it = flasherFactoryList.begin(); it != flasherFactoryList.end(); it++) {
      if (json_tableOutput.length() > 1) json_tableOutput += ",";

      json_tablerow["tag"] = it->_tag.c_str();
      json_tablerow["desc"] = it->_desc.c_str();
      json_tablerow["tgzupload"] = it->_route_tgzupload.c_str();
      json_tablerow["rollback"] = it->_route_rollback.c_str();

      serializeJson(json_tablerow, json_tableOutput);
    }
    json_tableOutput += "]";

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print(json_tableOutput);
    request->send(response);
}

void YuboxOTAClass::_rejectUpload(const String & s, bool serverError)
{
  if (!_tgzupload_serverError && !_tgzupload_clientError) {
    if (serverError) _tgzupload_serverError = true; else _tgzupload_clientError = true;
  }
  if (_tgzupload_responseMsg.isEmpty()) _tgzupload_responseMsg = s;
  _uploadRejected = true;
}

void YuboxOTAClass::_rejectUpload(const char * msg, bool serverError)
{
  String s = msg;
  _rejectUpload(s, serverError);
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload(AsyncWebServerRequest * request,
    String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  log_v("filename=%s index=%d data=%p len=%d final=%d",
    filename.c_str(), index, data, len, final ? 1 : 0);

  if (filename.endsWith(".tar.gz") || filename.endsWith(".tgz")) {
    if (_streamerImpl == NULL) _streamerImpl = new YuboxOTA_Streamer_GZ();
  } else if (filename.endsWith(".tar")) {
    if (_streamerImpl == NULL) _streamerImpl = new YuboxOTA_Streamer_Identity();
  } else {
    // Este upload no parece ser un tarball, se rechaza localmente.
    _rejectUpload("Archivo no parece un tarball - se espera extensión .tar.gz o .tgz", false);
    return;
  }
  if (index == 0) {
    /* La macro YUBOX_RUN_AUTH no es adecuada porque requestAuthentication() no puede llamarse
       aquí - vienen más fragmentos del upload. Se debe rechazar el upload si la autenticación
       ha fallado.
     */
    if (!YuboxWebAuth.authenticate((request))) {
      // Credenciales incorrectas
      _uploadRejected = true;
    } else {
      if (_flasherImpl != NULL) {
        _rejectUpload("El flasheo concurrente de firmwares no está soportado.", false);
      } else {
        int idxFlash = _idxFlasherFromURL(request->url());
        if (idxFlash < 0) {
          String msg = "No implementado flasheo para ruta: ";
          msg += request->url();
          _rejectUpload(msg, true);
        } else {
          _flasherImpl = _buildFlasherFromIdx(idxFlash);
          if (_flasherImpl == NULL) {
            _rejectUpload("Fallo al instanciar flasheador", true);
          }
        }
      }
    }
  } else {
    if (!_uploadRejected && _flasherImpl == NULL) {
      _rejectUpload("No se ha instanciado flasheador y se sigue recibiendo datos!", true);
    }
  }
  
  if (_uploadRejected) return;

  _handle_tgzOTAchunk(index, data, len, final);
}

String YuboxOTAClass::_checkOTA_Veto(bool isReboot)
{
  String s;

  for (auto i = 0; i < cbVetoList.size(); i++) {
    YuboxOTAVetoList_t entry = cbVetoList[i];
    if (entry.cb || entry.fcb) {
      if (entry.cb) {
        s = entry.cb(isReboot);
      } else if (entry.fcb) {
        s = entry.fcb(isReboot);
      }

      if (!s.isEmpty()) break;
    }
  }

  return s;
}

YuboxOTA_Flasher * YuboxOTAClass::_getESP32FlasherImpl(void)
{
    return new YuboxOTA_Flasher_ESP32();
}

void YuboxOTAClass::_handle_tgzOTAchunk(size_t index, uint8_t *data, size_t len, bool final)
{
  int r;

  // Revisar lista de vetos
  if (index == 0) {
    String vetoMsg = _checkOTA_Veto(false);
    if (!vetoMsg.isEmpty()) {
      _rejectUpload(vetoMsg, true);

      return;
    }
  }

  assert(_flasherImpl != NULL);
  assert(_streamerImpl != NULL);

  // Inicializar búferes al encontrar el primer segmento
  if (index == 0) {
    _tgzupload_rawBytesReceived = 0;
    _shouldReboot = false;
    _uploadFinished = false;

    if (!_streamerImpl->begin()) {
      _rejectUpload(_streamerImpl->getErrorMessage(), true);
    }

    // Inicialización de parseo tar
    _tar_emptyChunk = 0;
    _tar_eof = false;
    tar_setup(&_tarCB, this);

    // Inicialización de estado de actualización
    _tgzupload_clientError = false;
    _tgzupload_serverError = false;

    if (!_uploadRejected && !_flasherImpl->startUpdate()) {
      _rejectUpload(_flasherImpl->getLastErrorMessage(), true);
    }
  }

  _tgzupload_rawBytesReceived += len;

  if (!_uploadRejected) {
    if (!_streamerImpl->attachInputBuffer(data, len, final)) {
      _rejectUpload(_streamerImpl->getErrorMessage(), false);
    }
  }

  if (!_uploadRejected) {
    bool tar_progress;

    do {
      if (!_streamerImpl->transformInputBuffer()) {
        _rejectUpload(_streamerImpl->getErrorMessage(), false);
      }

      tar_progress = false;
      while (!_tar_eof && !_uploadRejected && (
        _streamerImpl->isOutputBufferFull() || (final && _streamerImpl->getAvailableOutputLength() > 0 && _streamerImpl->inputStreamEOF())
      )) {
        tar_progress = true;

        log_v("_tar_available=%u se ejecuta lectura tar", _streamerImpl->getAvailableOutputLength());
        // _tar_available se actualiza en _tar_cb_feedFromBuffer()
        // Procesamiento continúa en callbacks _tar_cb_*
        r = _tar_eof ? 0 : read_tar_step();
        if (r == -1 && _tar_emptyChunk >= 2 /*&& _streamerImpl->getTotalExpectedOutput() != 0*/) {
          _tar_eof = true;
          log_d("se alcanzó el final del tar actual=%lu esperado=%lu",
            _streamerImpl->getTotalProducedOutput(),
            _streamerImpl->getTotalExpectedOutput());
        } else if (r != 0) {
          // Error -5 es fallo por _tar_cb_gotEntry[Header|End] que devuelve != 0 - debería manejarse vía _uploadRejected
          // Error -7 es fallo por _tar_cb_gotEntryData que devuelve != 0 - debería manejarse vía _uploadRejected
          if (r != -7 && r != -5) {
            log_e("fallo al procesar tar en bloque available %u error %d emptychunk %d actual=%lu esperado=%lu",
              _streamerImpl->getAvailableOutputLength(), r, _tar_emptyChunk,
              _streamerImpl->getTotalProducedOutput(),
              _streamerImpl->getTotalExpectedOutput());
          }
          _rejectUpload("Archivo corrupto o truncado (tar), no puede procesarse", false);
        } else {
          log_v("luego de parseo tar: _tar_available=%u", _streamerImpl->getAvailableOutputLength());
        }
      }

      if (final && !_uploadRejected && _streamerImpl->inputStreamEOF()
        && _streamerImpl->getAvailableOutputLength() <= 0 && _tar_emptyChunk >= 2) {
        _tar_eof = true;
      }

    } while (tar_progress && !_tar_eof && !_uploadRejected);
  }

  if (!_uploadRejected) {
    _streamerImpl->detachInputBuffer();
  }

  // Cada llamada al callback de upload cede el CPU al menos aquí.
  vTaskDelay(pdMS_TO_TICKS(5));

  if (_tar_eof) {
    if (final && _flasherImpl != NULL && !_uploadFinished) {
      bool ok = _flasherImpl->finishUpdate();
      if (!ok) {
        // TODO: distinguir entre error de formato y error de flasheo
        _rejectUpload(_flasherImpl->getLastErrorMessage(), true);
      } else {
        _shouldReboot = _flasherImpl->shouldReboot();
      }
      _uploadFinished = true;
    }
  } else if (final) {
    // Se ha llegado al último chunk y no se ha detectado el fin del tar.
    // Esto o es un tar corrupto dentro de gzip, o un bug del código
    if (_flasherImpl != NULL) {
      _flasherImpl->truncateUpdate();
    }
    _rejectUpload("No se ha detectado final del tar al término del upload", false);
  }

  if (_uploadRejected || final) {
    tar_abort("tar cleanup", 0);
    delete _streamerImpl;
    _streamerImpl = NULL;
  }

  if (final && _flasherImpl != NULL) {
    delete _flasherImpl;
    _flasherImpl = NULL;
  }
}

void YuboxOTAClass::cleanupFailedUpdateFiles(void)
{
  YuboxOTA_Flasher_ESP32 * fi = (YuboxOTA_Flasher_ESP32 *)_getESP32FlasherImpl();
  fi->cleanupFailedUpdateFiles();
  delete fi;
}

int YuboxOTAClass::_tar_cb_feedFromBuffer(unsigned char * buf, size_t size)
{
  log_v("(0x%p, %u)", buf, size);
  if (size % TAR_BLOCK_SIZE != 0) {
    log_e("longitud pedida %d no es múltiplo de %d", size, TAR_BLOCK_SIZE);
    return 0;
  }

  _tar_emptyChunk++;

  unsigned int copySize = _streamerImpl->transferOutputData(buf, size);
  log_v("copiando %d bytes...", copySize);
  if (copySize < size) {
    log_w("se piden %d bytes pero sólo se pueden proveer %d bytes",
      size, copySize);
  }
  return copySize;
}

int YuboxOTAClass::_tar_cb_gotEntryHeader(header_translated_t * hdr, int entry_index)
{
  log_v("INICIO: %s", hdr->filename);
  switch (hdr->type)
  {
  case T_NORMAL:
    log_v("archivo ordinario tamaño 0x%08x%08x", (unsigned long)(hdr->filesize >> 32), (unsigned long)(hdr->filesize & 0xFFFFFFFFUL));
    if (_flasherImpl != NULL) {
      bool ok = _flasherImpl->startFile(hdr->filename, hdr->filesize);
      if (!ok) {
        _rejectUpload(_flasherImpl->getLastErrorMessage(), true);
      }
    }

    break;

  case T_HARDLINK:       log_v("Ignoring hard link to %s.", hdr->filename); break;
  case T_SYMBOLIC:       log_v("Ignoring sym link to %s.", hdr->filename); break;
  case T_CHARSPECIAL:    log_v("Ignoring special char."); break;
  case T_BLOCKSPECIAL:   log_v("Ignoring special block."); break;
  case T_DIRECTORY:      log_v("Entering %s directory.", hdr->filename); break;
  case T_FIFO:           log_v("Ignoring FIFO request."); break;
  case T_CONTIGUOUS:     log_v("Ignoring contiguous data to %s.", hdr->filename); break;
  case T_GLOBALEXTENDED: log_v("Ignoring global extended data."); break;
  case T_EXTENDED:       log_v("Ignoring extended data."); break;
  case T_OTHER: default: log_v("Ignoring unrelevant data.");       break;

  }
  _tar_emptyChunk = 0;

  return _uploadRejected ? -1 : 0;
}

int YuboxOTAClass::_tar_cb_gotEntryData(header_translated_t * hdr, int entry_index, unsigned char * block, int size)
{
  log_v("DATA: %s entry_index=%d (0x%p, %u)", hdr->filename, entry_index, block, size);
  if (_flasherImpl != NULL) {
    bool ok = _flasherImpl->appendFileData(hdr->filename, hdr->filesize, block, size);
    if (!ok) {
      _rejectUpload(_flasherImpl->getLastErrorMessage(), true);
    }
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}

int YuboxOTAClass::_tar_cb_gotEntryEnd(header_translated_t * hdr, int entry_index)
{
  log_v("FINAL: %s entry_index=%d", hdr->filename, entry_index);
  if (_flasherImpl != NULL) {
    bool ok = _flasherImpl->finishFile(hdr->filename, hdr->filesize);
    if (!ok) {
      _rejectUpload(_flasherImpl->getLastErrorMessage(), true);
    }
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}


int _tar_cb_feedFromBuffer(unsigned char * buf, size_t size)
{
  // Ya que no hay un puntero a contexto, este método está obligado a accesar
  // a YuboxOTA directamente
  YuboxOTAClass * ota = &YuboxOTA;
  return ota->_tar_cb_feedFromBuffer(buf, size);
}

int _tar_cb_gotEntryHeader(header_translated_t * hdr, int entry_index, void * context_data)
{
  YuboxOTAClass * ota = (YuboxOTAClass *)context_data;
  return ota->_tar_cb_gotEntryHeader(hdr, entry_index);
}

int _tar_cb_gotEntryData(header_translated_t * hdr, int entry_index, void * context_data, unsigned char * block, int size)
{
  YuboxOTAClass * ota = (YuboxOTAClass *)context_data;
  return ota->_tar_cb_gotEntryData(hdr, entry_index, block, size);

}

int _tar_cb_gotEntryEnd(header_translated_t * hdr, int entry_index, void * context_data)
{
  YuboxOTAClass * ota = (YuboxOTAClass *)context_data;
  return ota->_tar_cb_gotEntryEnd(hdr, entry_index);
}

void YuboxOTAClass::_emitUploadEvent_FileStart(const char * filename, bool isfirmware, unsigned long size)
{
  if (_pEvents == NULL) return;
  if (_pEvents->count() <= 0) return;

  _tgzupload_lastEventSent = millis();
  String s;
  StaticJsonDocument<JSON_OBJECT_SIZE(5)> json_doc;
  json_doc["event"] = "uploadFileStart";
  json_doc["filename"] = filename;
  json_doc["firmware"] = isfirmware;
  json_doc["total"] = size;
  json_doc["currupload"] = _tgzupload_rawBytesReceived;
  serializeJson(json_doc, s);
  _pEvents->send(s.c_str(), "uploadFileStart");
}

void YuboxOTAClass::_emitUploadEvent_FileProgress(const char * filename, bool isfirmware, unsigned long size, unsigned long offset)
{
  if (_pEvents == NULL) return;
  if (_pEvents->count() <= 0) return;
  if (millis() - _tgzupload_lastEventSent < 200) return;

  _tgzupload_lastEventSent = millis();
  String s;
  StaticJsonDocument<JSON_OBJECT_SIZE(6)> json_doc;
  json_doc["event"] = "uploadFileProgress";
  json_doc["filename"] = filename;
  json_doc["firmware"] = isfirmware;
  json_doc["current"] = offset;
  json_doc["total"] = size;
  json_doc["currupload"] = _tgzupload_rawBytesReceived;
  serializeJson(json_doc, s);
  _pEvents->send(s.c_str(), "uploadFileProgress");
}

void YuboxOTAClass::_emitUploadEvent_FileEnd(const char * filename, bool isfirmware, unsigned long size)
{
  if (_pEvents == NULL) return;
  if (_pEvents->count() <= 0) return;

  _tgzupload_lastEventSent = millis();
  String s;
  StaticJsonDocument<JSON_OBJECT_SIZE(5)> json_doc;
  json_doc["event"] = "uploadFileEnd";
  json_doc["filename"] = filename;
  json_doc["firmware"] = isfirmware;
  json_doc["total"] = size;
  json_doc["currupload"] = _tgzupload_rawBytesReceived;
  serializeJson(json_doc, s);
  _pEvents->send(s.c_str(), "uploadFileEnd");
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_POST(AsyncWebServerRequest * request)
{
  /* La macro YUBOX_RUN_AUTH no es adecuada aquí porque el manejador de upload se ejecuta primero
     y puede haber rechazado el upload. La bandera de rechazo debe limpiarse antes de rechazar la
     autenticación.
   */
  if (!YuboxWebAuth.authenticate((request))) {
    _uploadRejected = false;
    return (request)->requestAuthentication();
  }

  bool clientError = false;
  bool serverError = false;
  String responseMsg = "";

  if (!request->hasParam("tgzupload", true, true)) {
    clientError = true;
    responseMsg = "No se ha especificado archivo de actualización.";
  }

  if (!clientError) clientError = _tgzupload_clientError;
  if (!serverError) serverError = _tgzupload_serverError;
  if (responseMsg == "") responseMsg = _tgzupload_responseMsg;

  if (!clientError && !serverError) {
    responseMsg = "Firmware actualizado correctamente. El equipo se reiniciará en unos momentos.";
  }

  if (_streamerImpl != NULL) {
    log_w("streamer no fue destruido al terminar manejo upload, se destruye ahora...");
    delete _streamerImpl;
    _streamerImpl = NULL;
  }

  if (_flasherImpl != NULL) {
    log_w("flasheador no fue destruido al terminar manejo upload, se destruye ahora...");
    delete _flasherImpl;
    _flasherImpl = NULL;
  }

  _uploadRejected = false;
  _tgzupload_clientError = false;
  _tgzupload_serverError = false;
  _tgzupload_responseMsg = "";

  unsigned int httpCode = 200;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> json_doc;
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();
  json_doc["reboot"] = (_shouldReboot && !clientError && !serverError);

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_rollback_GET(AsyncWebServerRequest * request)
{
  YUBOX_RUN_AUTH(request);

  YuboxOTA_Flasher * fi = _buildFlasherFromURL(request->url());
  if (fi == NULL) {
    request->send(404, "application/json", "{\"success\":false,\"msg\":\"El flasheador indicado no existe o no ha sido implementado\"}");
    return;
  }

  bool canRollBack = fi->canRollBack();
  delete fi;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<JSON_OBJECT_SIZE(1)> json_doc;
  response->setCode(200);
  json_doc["canrollback"] = canRollBack;

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_rollback_POST(AsyncWebServerRequest * request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;

  // Revisar lista de vetos
  String vetoMsg = _checkOTA_Veto(false); // Rollback cuenta como flasheo
  String errMsg;
  if (!vetoMsg.isEmpty()) {
    json_doc["success"] = false;
    json_doc["msg"] = vetoMsg.c_str();

    response->setCode(500);
  } else {
    YuboxOTA_Flasher * fi = _buildFlasherFromURL(request->url());

    if (fi == NULL) {
      json_doc["success"] = false;
      json_doc["msg"] = "El flasheador indicado no existe o no ha sido implementado";
    } else if (!fi->doRollBack()) {
      errMsg = fi->getLastErrorMessage();
      json_doc["success"] = false;
      json_doc["msg"] = errMsg.c_str();

      response->setCode(500);
    } else {
      vetoMsg = _checkOTA_Veto(true);
      if (!vetoMsg.isEmpty()) {
        json_doc["success"] = false;
        json_doc["msg"] = vetoMsg.c_str();

        response->setCode(500);
      } else {
        json_doc["success"] = true;
        json_doc["msg"] = "Firmware restaurado correctamente. El equipo se reiniciará en unos momentos.";
        xTimerStart(_timer_restartYUBOX, 0);

        response->setCode(200);
      }
    }

    if (fi != NULL) delete fi;
  }

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_reboot_POST(AsyncWebServerRequest * request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;

  // Revisar lista de vetos
  String vetoMsg = _checkOTA_Veto(true);
  if (!vetoMsg.isEmpty()) {
    json_doc["success"] = false;
    json_doc["msg"] = vetoMsg.c_str();

    response->setCode(500);
  } else {
    json_doc["success"] = true;
    json_doc["msg"] = "El equipo se reiniciará en unos momentos.";
    xTimerStart(_timer_restartYUBOX, 0);

    response->setCode(200);
  }

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxOTAClass::_cbHandler_restartYUBOX(TimerHandle_t)
{
  log_w("YUBOX OTA: reiniciando luego de cambio de firmware...");
  ESP.restart();
}

#include "SPIFFS.h"

void YuboxOTAClass::_routeHandler_spiffslist_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->print(
    "<!DOCTYPE html><html lang=\"en\"><head><title>Index of SPIFFS</title></head><body><h1>Index of SPIFFS</h1>");
  response->print("<table><tr><th>Name</th><th>Size</th></tr><tr><th colspan=\"2\"><hr></th></tr>");

  File h = SPIFFS.open("/");
  if (h && h.isDirectory()) {
    File f = h.openNextFile();
    while (f) {
      String s = f.name();
      unsigned int sz = f.size();
      bool gzcomp = s.endsWith(".gz");
      f.close();
      if (gzcomp) {
        s.remove(s.length() - 3);
      }
      response->printf("<tr><td><a href=\"%s\">%s</a>%s</td><td align=\"right\">%u</td></tr>",
        s.c_str(), s.c_str(), gzcomp ? ".gz" : "", sz);
      f = h.openNextFile();
    }

    h.close();
  }

  response->print("<tr><th colspan=\"2\"><hr></th></tr></table>");

  // Reportar uso total de partición SPIFFS
  size_t total = SPIFFS.totalBytes();
  size_t used = SPIFFS.usedBytes();
  response->printf("<p>Total bytes: %d Used bytes: %d Free bytes: %d</p>", total, used, (total - used));

  response->print("</body></html>");
  request->send(response);
}

#include "core_version.h"

void YuboxOTAClass::_routeHandler_yuboxhwreport_GET(AsyncWebServerRequest *request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(18));

  json_doc["ARDUINO_ESP32_GIT_VER"] = ARDUINO_ESP32_GIT_VER;
  json_doc["ARDUINO_ESP32_RELEASE"] = ARDUINO_ESP32_RELEASE;
  json_doc["SKETCH_COMPILE_DATETIME"] = __DATE__ " " __TIME__;
  json_doc["IDF_VER"] = ESP.getSdkVersion();
  json_doc["CHIP_MODEL"] = ESP.getChipModel();
  json_doc["CHIP_CORES"] = ESP.getChipCores();
  json_doc["CPU_MHZ"] = ESP.getCpuFreqMHz();
  json_doc["FLASH_SIZE"] = ESP.getFlashChipSize();
  json_doc["FLASH_SPEED"] = ESP.getFlashChipSpeed();

  json_doc["SKETCH_SIZE"] = ESP.getSketchSize();
  String sketch_md5 = ESP.getSketchMD5();
  json_doc["SKETCH_MD5"] = sketch_md5.c_str();
  json_doc["EFUSE_MAC"] = ESP.getEfuseMac();

  json_doc["psramsize"] = ESP.getPsramSize();
  json_doc["psramfree"] = ESP.getFreePsram();
  json_doc["psrammaxalloc"] = ESP.getMaxAllocPsram();

  // MALLOC_CAP_DEFAULT es la memoria directamente provista vía malloc() y operator new
  multi_heap_info_t info = {0};
  heap_caps_get_info(&info, /*MALLOC_CAP_INTERNAL*/MALLOC_CAP_DEFAULT);
  json_doc["heapfree"] = info.total_free_bytes;
  json_doc["heapmaxalloc"] = info.largest_free_block;
  json_doc["heapsize"] = info.total_free_bytes + info.total_allocated_bytes;

  serializeJson(json_doc, *response);
  request->send(response);
}

YuboxOTAClass YuboxOTA;
