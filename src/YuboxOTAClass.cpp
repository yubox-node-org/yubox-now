#include "YuboxOTAClass.h"

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <functional>

#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

#include "esp_task_wdt.h"

#include "YuboxOTA_Flasher_ESP32.h"

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

#define GZIP_DICT_SIZE 32768
#define GZIP_BUFF_SIZE 4096

/* De pruebas se ha visto que el máximo tamaño de segmento de datos en el upload es
 * de 1460 bytes. Si el espacio libre en _gz_srcdata es igual o menor al siguiente
 * valor, se iniciará la descompresión con los datos leídos hasta el momento.
 */
#define GZIP_FILL_WATERMARK 1500

int _tar_cb_feedFromBuffer(unsigned char *, size_t);
int _tar_cb_gotEntryHeader(header_translated_t *, int, void *);
int _tar_cb_gotEntryData(header_translated_t *, int, void *, unsigned char *, int);
int _tar_cb_gotEntryEnd(header_translated_t *, int, void *);

YuboxOTAClass::YuboxOTAClass(void)
{
  _flasherImpl = NULL;

  _uploadRejected = false;
  _shouldReboot = false;
  _gz_srcdata = NULL;
  _gz_dstdata = NULL;
  _gz_dict = NULL;
  _gz_actualExpandedSize = 0;
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
  return flasherFactoryList[idx]._factory();
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
    DynamicJsonDocument json_tablerow(JSON_OBJECT_SIZE(4));
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

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload(AsyncWebServerRequest * request,
    String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  //Serial.printf("DEBUG: tgzupload_handleUpload filename=%s index=%d data=%p len=%d final=%d\r\n",
  //  filename.c_str(), index, data, len, final ? 1 : 0);

  if (!filename.endsWith(".tar.gz") && !filename.endsWith(".tgz")) {
    // Este upload no parece ser un tarball, se rechaza localmente.
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
        _tgzupload_responseMsg = "El flasheo concurrente de firmwares no está soportado.";
        _tgzupload_clientError = true;
        _uploadRejected = true;
      } else {
        int idxFlash = _idxFlasherFromURL(request->url());
        if (idxFlash < 0) {
          _tgzupload_responseMsg = "No implementado flasheo para ruta: ";
          _tgzupload_responseMsg += request->url();
          _tgzupload_serverError = true;
          _uploadRejected = true;
        } else {
          _flasherImpl = _buildFlasherFromIdx(idxFlash);
          if (_flasherImpl == NULL) {
            _tgzupload_responseMsg = "Fallo al instanciar flasheador";
            _tgzupload_serverError = true;
            _uploadRejected = true;
          }
        }
      }
    }
  } else {
    if (!_uploadRejected && _flasherImpl == NULL) {
      _tgzupload_responseMsg = "No se ha instanciado flasheador y se sigue recibiendo datos!";
      _tgzupload_serverError = true;
      _uploadRejected = true;
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
    return new YuboxOTA_Flasher_ESP32(
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileStart, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
      std::bind(&YuboxOTAClass::_emitUploadEvent_FileEnd, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
}

void YuboxOTAClass::_handle_tgzOTAchunk(size_t index, uint8_t *data, size_t len, bool final)
{
  int r;

  // Revisar lista de vetos
  if (index == 0) {
    String vetoMsg = _checkOTA_Veto(false);
    if (!vetoMsg.isEmpty()) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = vetoMsg;
      _uploadRejected = true;

      return;
    }
  }

  assert(_flasherImpl != NULL);

  // Inicializar búferes al encontrar el primer segmento
  if (index == 0) {
    _tgzupload_rawBytesReceived = 0;
    _shouldReboot = false;

    /* El valor de GZIP_BUFF_SIZE es suficiente para al menos dos fragmentos de datos entrantes.
     * Se descomprime únicamente TAR_BLOCK_SIZE a la vez para simplificar el código y para que
     * no ocurra que se acabe el búfer de datos de entrada antes de llenar el búfer de salida.
     */
    _gz_dict = new unsigned char[GZIP_DICT_SIZE];
    _gz_srcdata = new unsigned char[GZIP_BUFF_SIZE];
    _gz_dstdata = new unsigned char[2 * TAR_BLOCK_SIZE];
    _gz_actualExpandedSize = 0;
    _gz_headerParsed = false;

    uzlib_init();

    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    _uzLib_decomp.source = _gz_srcdata;         // <-- El búfer inicial de datos
    _uzLib_decomp.source_limit = _gz_srcdata;   // <-- Será movido al agregar los datos recibidos
    uzlib_uncompress_init(&_uzLib_decomp, _gz_dict, GZIP_DICT_SIZE);

    // Inicialización de parseo tar
    _tar_available = 0;
    _tar_emptyChunk = 0;
    _tar_eof = false;
    tar_setup(&_tarCB, this);

    // Inicialización de estado de actualización
    _tgzupload_clientError = false;
    _tgzupload_serverError = false;

    if (!_flasherImpl->startUpdate()) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = _flasherImpl->getLastErrorMessage();
      _uploadRejected = true;
    }
  }

  _tgzupload_rawBytesReceived += len;

  // Agregar búfer recibido al búfer de entrada, notando si debe empezarse a parsear gzip
  unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
  unsigned int consumed;
  //Serial.printf("DEBUG: INICIO: used=%u MAX=%u\r\n", used, GZIP_BUFF_SIZE);
  bool runUnzip = false;
  if (_uploadRejected) {
    Serial.printf("ERR: falla upload en index %d - %s\r\n", index, _tgzupload_responseMsg.c_str());
  } else if (GZIP_BUFF_SIZE - used < len) {
    // No hay suficiente espacio para este bloque de datos. ESTO NO DEBERÍA PASAR
    Serial.printf("ERR: no hay suficiente espacio en _gz_srcdata: libre=%u requerido=%u\r\n", GZIP_BUFF_SIZE - used, len);
    _tgzupload_serverError = true;
    _tgzupload_responseMsg = "(internal) Falta espacio en búfer para siguiente pedazo de datos!";
    _uploadRejected = true;
  } else {
    unsigned long gz_expectedExpandedSize = 0;

    memcpy((void *)_uzLib_decomp.source_limit, data, len);
    _uzLib_decomp.source_limit += len;
    used += len;
    //Serial.printf("DEBUG: LUEGO DE AGREGAR chunk: used=%u MAX=%u\r\n", used, GZIP_BUFF_SIZE);

    if (final) {
      // Para el último bloque HTTP debería tenerse los 4 últimos bytes LSB que indican
      // el tamaño esperado de longitud expandida.
      if (used < 4) {
        Serial.printf("ERR: no hay suficientes datos luego de bloque final para determinar tamaño expandido, se tienen %u bytes\r\n", used);
        _tgzupload_clientError = true;
        _tgzupload_responseMsg = "Archivo es demasiado corto para validar longitud gzip";
        _uploadRejected = true;
      } else {
        gz_expectedExpandedSize =
          ((unsigned long)(*(_uzLib_decomp.source_limit - 4))      ) |
          ((unsigned long)(*(_uzLib_decomp.source_limit - 3)) <<  8) |
          ((unsigned long)(*(_uzLib_decomp.source_limit - 2)) << 16) |
          ((unsigned long)(*(_uzLib_decomp.source_limit - 1)) << 24);
        //Serial.printf("DEBUG: longitud esperada de datos expandidos es %lu bytes\r\n", gz_expectedExpandedSize);
        if (gz_expectedExpandedSize < _gz_actualExpandedSize) {
          Serial.printf("ERR: longitud ya expandida excede longitud esperada! %lu > %lu\r\n", _gz_actualExpandedSize, gz_expectedExpandedSize);
          _tgzupload_clientError = true;
          _tgzupload_responseMsg = "Longitud esperada inconsistente con datos ya expandidos";
          _uploadRejected = true;
        }
      }
    }

    // Ejecutar descompresión si es el ÚLTIMO bloque, o si hay menos espacio que el necesario
    // para agregar un bloque más.
    runUnzip = (final || (GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK));
    while (!_uploadRejected && runUnzip && !_tar_eof && (gz_expectedExpandedSize == 0 || _gz_actualExpandedSize < gz_expectedExpandedSize)) {
      //Serial.printf("DEBUG: _gz_actualExpandedSize=%lu gz_expectedExpandedSize=%lu\r\n", _gz_actualExpandedSize, gz_expectedExpandedSize);
      //Serial.printf("DEBUG: se tienen %u bytes, se ejecuta gunzip...\r\n", used);
      if (!_gz_headerParsed) {
        // Se requiere parsear cabecera gzip para validación
        r = uzlib_gzip_parse_header(&_uzLib_decomp);
        if (r != TINF_OK) {
          // Fallo al parsear la cabecera gzip
          Serial.println("ERR: fallo al parsear cabecera gzip");
          _tgzupload_clientError = true;
          _tgzupload_responseMsg = "Archivo no parece ser un archivo tar.gz, o está corrupto";
          _uploadRejected = true;
          break;
        }
        // Cabecera gzip OK, se ajustan búferes
        _gz_headerParsed = true;
      } else {
        _uzLib_decomp.dest_start = _gz_dstdata;
        _uzLib_decomp.dest = _gz_dstdata + _tar_available;
        _uzLib_decomp.dest_limit = _gz_dstdata + 2 * TAR_BLOCK_SIZE;
        if (final && (gz_expectedExpandedSize - _gz_actualExpandedSize) < 2 * TAR_BLOCK_SIZE - _tar_available) {
          _uzLib_decomp.dest_limit = _gz_dstdata + _tar_available + (gz_expectedExpandedSize - _gz_actualExpandedSize);
        }
        while (_uzLib_decomp.dest < _uzLib_decomp.dest_limit) {
          r = uzlib_uncompress(&_uzLib_decomp);
          if (r != TINF_DONE && r != TINF_OK) {
            Serial.printf("ERR: fallo al descomprimir gzip (err=%d)\r\n", r);
            _tgzupload_clientError = true;
            _tgzupload_responseMsg = "Archivo corrupto o truncado (gzip), no puede descomprimirse";
            _uploadRejected = true;
            break;
          }
          if (_uploadRejected) break;
        }
        _gz_actualExpandedSize += (_uzLib_decomp.dest - _uzLib_decomp.dest_start) - _tar_available;
        //Serial.printf("DEBUG: producidos %u bytes expandidos:\r\n", (_uzLib_decomp.dest - _uzLib_decomp.dest_start) - _tar_available);
        _tar_available = _uzLib_decomp.dest - _uzLib_decomp.dest_start;

        // Pasar búfer descomprimido a rutina tar
        while (!_tar_eof && !_uploadRejected && ((_tar_available >= 2 * TAR_BLOCK_SIZE)
          || (final && gz_expectedExpandedSize != 0 && _gz_actualExpandedSize >= gz_expectedExpandedSize && _tar_available > 0))) {
          //Serial.printf("DEBUG: _tar_available=%u se ejecuta lectura tar\r\n", _tar_available);
          // _tar_available se actualiza en _tar_cb_feedFromBuffer()
          // Procesamiento continúa en callbacks _tar_cb_*
          r = _tar_eof ? 0 : read_tar_step();
          if (r == -1 && _tar_emptyChunk >= 2 && gz_expectedExpandedSize != 0) {
            _tar_eof = true;
            //Serial.printf("DEBUG: se alcanzó el final del tar actual=%lu esperado=%lu\r\n", _gz_actualExpandedSize, gz_expectedExpandedSize);
          } else if (r != 0) {
            Serial.printf("ERR: fallo al procesar tar en bloque available %u error %d emptychunk %d actual=%lu esperado=%lu\r\n", _tar_available, r, _tar_emptyChunk, _gz_actualExpandedSize, gz_expectedExpandedSize);
            // No sobreescribir mensaje raíz si ha sido ya asignado
            if (!_tgzupload_clientError && !_tgzupload_serverError) {
              _tgzupload_clientError = true;
              _tgzupload_responseMsg = "Archivo corrupto o truncado (tar), no puede procesarse";
            }
            _uploadRejected = true;
          } else {
            //Serial.printf("DEBUG: luego de parseo tar: _tar_available=%u\r\n", _tar_available);
          }
        }

        if (final && !_uploadRejected && gz_expectedExpandedSize != 0 && _gz_actualExpandedSize >= gz_expectedExpandedSize
          && _tar_available <= 0 && _tar_emptyChunk >= 2) {
          _tar_eof = true;
        }
      }

      consumed = _uzLib_decomp.source - _gz_srcdata;
      if (consumed != 0) {
        //Serial.printf("DEBUG: parseo gzip consumió %u bytes, se ajusta...\r\n", consumed);
        if (_uzLib_decomp.source < _uzLib_decomp.source_limit) {
          memmove(_gz_srcdata, _gz_srcdata + consumed, _uzLib_decomp.source_limit - _uzLib_decomp.source);
          _uzLib_decomp.source_limit -= consumed;
          _uzLib_decomp.source -= consumed;
        } else {
          _uzLib_decomp.source = _gz_srcdata;
          _uzLib_decomp.source_limit = _gz_srcdata;
        }
      } else {
        //Serial.println("DEBUG: parseo gzip no consumió bytes...");
      }
      used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
      runUnzip = (final || (GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK));

      //Serial.printf("DEBUG: quedan %u bytes en búfer de entrada gzip\r\n", used);
    }
  }

  // Cada llamada al callback de upload cede el CPU al menos aquí.
  // Se requiere de al menos 25 milisegundos para impedir que la posible
  // tarea "ipc0" active el watchdog mientras se sube código.
  vTaskDelay(pdMS_TO_TICKS(25));

  if (_tar_eof) {
    if (_flasherImpl != NULL) {
      bool ok = _flasherImpl->finishUpdate();
      if (!ok) {
        // TODO: distinguir entre error de formato y error de flasheo
        _tgzupload_serverError = true;
        _tgzupload_responseMsg = _flasherImpl->getLastErrorMessage();
        _uploadRejected = true;
      } else {
        _shouldReboot = _flasherImpl->shouldReboot();
      }
    }
  } else if (final) {
    // Se ha llegado al último chunk y no se ha detectado el fin del tar.
    // Esto o es un tar corrupto dentro de gzip, o un bug del código
    if (_flasherImpl != NULL) {
      _flasherImpl->truncateUpdate();
    }
    _tgzupload_clientError = true;
    _tgzupload_responseMsg = "No se ha detectado final del tar al término del upload";
    _uploadRejected = true;
  }

  if (_uploadRejected || final) {
    tar_abort("tar cleanup", 0);
    if (_gz_dict != NULL) { delete _gz_dict; _gz_dict = NULL; }
    if (_gz_srcdata != NULL) { delete _gz_srcdata; _gz_srcdata = NULL; }
    if (_gz_dstdata != NULL) { delete _gz_dstdata; _gz_dstdata = NULL; }
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
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
  //Serial.printf("DEBUG: YuboxOTAClass::_tar_cb_feedFromBuffer(0x%p, %u)\r\n", buf, size);
  if (size % TAR_BLOCK_SIZE != 0) {
    Serial.printf("ERR: _tar_cb_feedFromBuffer: longitud pedida %d no es múltiplo de %d\r\n", size, TAR_BLOCK_SIZE);
    return 0;
  }

  _tar_emptyChunk++;

  unsigned int copySize = _tar_available;
  //Serial.printf("DEBUG: _tar_cb_feedFromBuffer máximo copia posible %d bytes...\r\n", copySize);
  if (copySize > size) copySize = size;
  //Serial.printf("DEBUG: _tar_cb_feedFromBuffer copiando %d bytes...\r\n", copySize);
  if (copySize < size) {
    Serial.printf("WARN: _tar_cb_feedFromBuffer: se piden %d bytes pero sólo se pueden proveer %d bytes\r\n",
      size, copySize);
  }
  if (copySize > 0) {
    memcpy(buf, _uzLib_decomp.dest_start, copySize);
    if (_tar_available - copySize > 0) {
      memmove(_uzLib_decomp.dest_start, _uzLib_decomp.dest_start + copySize, _tar_available - copySize);
    }
    _uzLib_decomp.dest -= copySize;
    _tar_available -= copySize;
  }
  return copySize;
}

int YuboxOTAClass::_tar_cb_gotEntryHeader(header_translated_t * hdr, int entry_index)
{
  //Serial.printf("DEBUG: _tar_cb_gotEntryHeader: %s INICIO\r\n", hdr->filename);
  switch (hdr->type)
  {
  case T_NORMAL:
    //Serial.printf("DEBUG: archivo ordinario tamaño 0x%08x%08x\r\n", (unsigned long)(hdr->filesize >> 32), (unsigned long)(hdr->filesize & 0xFFFFFFFFUL));
    if (_flasherImpl != NULL) {
      bool ok = _flasherImpl->startFile(hdr->filename, hdr->filesize);
      if (!ok) {
        _tgzupload_serverError = true;
        _tgzupload_responseMsg = _flasherImpl->getLastErrorMessage();
        _uploadRejected = true;
      }
    }

    break;
/*
  case T_HARDLINK:       Serial.printf("Ignoring hard link to %s.\r\n", hdr->filename); break;
  case T_SYMBOLIC:       Serial.printf("Ignoring sym link to %s.\r\n", hdr->filename); break;
  case T_CHARSPECIAL:    Serial.printf("Ignoring special char.\r\n"); break;
  case T_BLOCKSPECIAL:   Serial.printf("Ignoring special block.\r\n"); break;
  case T_DIRECTORY:      Serial.printf("Entering %s directory.\r\n", hdr->filename); break;
  case T_FIFO:           Serial.printf("Ignoring FIFO request.\r\n"); break;
  case T_CONTIGUOUS:     Serial.printf("Ignoring contiguous data to %s.\r\n", hdr->filename); break;
  case T_GLOBALEXTENDED: Serial.printf("Ignoring global extended data.\r\n"); break;
  case T_EXTENDED:       Serial.printf("Ignoring extended data.\r\n"); break;
  case T_OTHER: default: Serial.printf("Ignoring unrelevant data.\r\n");       break;
*/
  }
  _tar_emptyChunk = 0;

  return _uploadRejected ? -1 : 0;
}

int YuboxOTAClass::_tar_cb_gotEntryData(header_translated_t * hdr, int entry_index, unsigned char * block, int size)
{
  if (_flasherImpl != NULL) {
    bool ok = _flasherImpl->appendFileData(hdr->filename, hdr->filesize, block, size);
    if (!ok) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = _flasherImpl->getLastErrorMessage();
      _uploadRejected = true;
    }
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}

int YuboxOTAClass::_tar_cb_gotEntryEnd(header_translated_t * hdr, int entry_index)
{
  if (_flasherImpl != NULL) {
    bool ok = _flasherImpl->finishFile(hdr->filename, hdr->filesize);
    if (!ok) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = _flasherImpl->getLastErrorMessage();
      _uploadRejected = true;
    }
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}


int _tar_cb_feedFromBuffer(unsigned char * buf, size_t size)
{
  //Serial.printf("DEBUG: _tar_cb_feedFromBuffer buf=%p size=%d\r\n", buf, size);
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
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(5));
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
  if (millis() - _tgzupload_lastEventSent < 400) return;

  _tgzupload_lastEventSent = millis();
  String s;
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(6));
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
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(5));
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

  if (_flasherImpl != NULL) {
    Serial.println("WARN: flasheador no fue destruido al terminar manejo upload, se destruye ahora...");
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
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(3));
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
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(1));
  response->setCode(200);
  json_doc["canrollback"] = canRollBack;

  serializeJson(json_doc, *response);
  request->send(response);
}

void YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_rollback_POST(AsyncWebServerRequest * request)
{
  YUBOX_RUN_AUTH(request);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));

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
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));

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
#ifdef DEBUG_YUBOX_OTA
  Serial.println("YUBOX OTA: reiniciando luego de cambio de firmware...");
#endif
  ESP.restart();
}

YuboxOTAClass YuboxOTA;
