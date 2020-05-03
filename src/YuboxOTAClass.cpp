#include "YuboxOTAClass.h"

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <functional>

#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

#include <Update.h>

#define GZIP_DICT_SIZE 32768
#define GZIP_BUFF_SIZE 4096
#ifndef SPI_FLASH_SEC_SIZE
  #define SPI_FLASH_SEC_SIZE 4096
#endif

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
  _uploadRejected = false;
  _gz_srcdata = NULL;
  _gz_dstdata = NULL;
  _gz_dict = NULL;
  _gz_actualExpandedSize = 0;
  _tarCB.header_cb = ::_tar_cb_gotEntryHeader;
  _tarCB.data_cb = ::_tar_cb_gotEntryData;
  _tarCB.end_cb = ::_tar_cb_gotEntryEnd;
  tinyUntarReadCallback = ::_tar_cb_feedFromBuffer;
}

void YuboxOTAClass::begin(AsyncWebServer & srv)
{
  srv.on("/yubox-api/yuboxOTA/tgzupload", HTTP_POST,
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_POST, this, std::placeholders::_1),
    std::bind(&YuboxOTAClass::_routeHandler_yuboxAPI_yuboxOTA_tgzupload_handleUpload, this, std::placeholders::_1,
      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
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
    }
  }
  
  if (_uploadRejected) return;

  _handle_tgzOTAchunk(index, data, len, final);
}

void YuboxOTAClass::_handle_tgzOTAchunk(size_t index, uint8_t *data, size_t len, bool final)
{
  int r;

  // Inicializar búferes al encontrar el primer segmento
  if (index == 0) {
    /* SPI_FLASH_SEC_SIZE es el mismo valor que GZIP_BUFF_SIZE. Voy a asignar el doble de datos
       para _gz_srcdata que para la salida para que no ocurra que se acabe el búfer de datos
       de entrada antes de llenar el búfer de salida.
     */
    _gz_dict = new unsigned char[GZIP_DICT_SIZE];
    _gz_srcdata = new unsigned char[2 * GZIP_BUFF_SIZE];
    _gz_dstdata = new unsigned char[SPI_FLASH_SEC_SIZE];
    _gz_actualExpandedSize = 0;
    _gz_headerParsed = false;

    uzlib_init();

    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    _uzLib_decomp.source = _gz_srcdata;         // <-- El búfer inicial de datos
    _uzLib_decomp.source_limit = _gz_srcdata;   // <-- Será movido al agregar los datos recibidos
    uzlib_uncompress_init(&_uzLib_decomp, _gz_dict, GZIP_DICT_SIZE);

    // Inicialización de parseo tar
    _tar_offset = 0;
    _tar_emptyChunk = 0;
    _tar_eof = false;
    tar_setup(&_tarCB, this);

    // Inicialización de estado de actualización
    _tgzupload_clientError = false;
    _tgzupload_serverError = false;
    _tgzupload_responseMsg = "";
    _tgzupload_currentOp = OTA_IDLE;
    _tgzupload_foundFirmware = false;
  }

  // Agregar búfer recibido al búfer de entrada, notando si debe empezarse a parsear gzip
  unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
  unsigned int consumed;
  bool runUnzip = false;
  if (2 * GZIP_BUFF_SIZE - used < len) {
    // No hay suficiente espacio para este bloque de datos. ESTO NO DEBERÍA PASAR
    Serial.printf("ERR: no hay suficiente espacio en _gz_srcdata: libre=%u requerido=%u\r\n", 2 * GZIP_BUFF_SIZE - used, len);
    _tgzupload_serverError = true;
    _tgzupload_responseMsg = "(internal) Falta espacio en búfer para siguiente pedazo de datos!";
    _uploadRejected = true;
  } else {
    unsigned long gz_expectedExpandedSize = 0;

    memcpy((void *)_uzLib_decomp.source_limit, data, len);
    _uzLib_decomp.source_limit += len;
    used += len;

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
    runUnzip = (final || (2 * GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK));
    while (!_uploadRejected && runUnzip && (gz_expectedExpandedSize == 0 || _gz_actualExpandedSize < gz_expectedExpandedSize)) {
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
        _uzLib_decomp.dest = _gz_dstdata;
        _uzLib_decomp.dest_limit = _gz_dstdata + ((final && (gz_expectedExpandedSize - _gz_actualExpandedSize) < SPI_FLASH_SEC_SIZE)
          ? gz_expectedExpandedSize - _gz_actualExpandedSize
          : SPI_FLASH_SEC_SIZE);
        r = uzlib_uncompress(&_uzLib_decomp);
        if (r != TINF_DONE && r != TINF_OK) {
          Serial.printf("ERR: fallo al descomprimir gzip (err=%d)\r\n", r);
          _tgzupload_clientError = true;
          _tgzupload_responseMsg = "Archivo corrupto o truncado (gzip), no puede descomprimirse";
          _uploadRejected = true;
          break;
        }
        unsigned int gzExpandedSize = _uzLib_decomp.dest - _uzLib_decomp.dest_start;
        _gz_actualExpandedSize += gzExpandedSize;
        //Serial.printf("DEBUG: producidos %u bytes expandidos:\r\n", gzExpandedSize);

        // Pasar búfer descomprimido a rutina tar
        _tar_offset = 0;
        while (!_tar_eof && !_uploadRejected && _tar_offset < gzExpandedSize) {
          // _tar_offset se actualiza en _tar_cb_feedFromBuffer()
          // Procesamiento continúa en callbacks _tar_cb_*
          r = _tar_eof ? 0 : read_tar_step();
          if (r == -1 && _tar_emptyChunk >= 2 && gz_expectedExpandedSize != 0) {
            _tar_eof = true;
            //Serial.printf("DEBUG: se alcanzó el final del tar actual=%lu esperado=%lu\r\n", _gz_actualExpandedSize, gz_expectedExpandedSize);
          } else if (r != 0) {
            Serial.printf("ERR: fallo al procesar tar en bloque offset %u error %d emptychunk %d actual=%lu esperado=%lu\r\n", _tar_offset, r, _tar_emptyChunk, _gz_actualExpandedSize, gz_expectedExpandedSize);
            _tgzupload_clientError = true;
            _tgzupload_responseMsg = "Archivo corrupto o truncado (tar), no puede procesarse";
            _uploadRejected = true;
          }
        }
      }

      consumed = _uzLib_decomp.source - _gz_srcdata;
      //Serial.printf("DEBUG: parseo consumió %u bytes, se ajusta...\r\n", consumed);
      if (_uzLib_decomp.source < _uzLib_decomp.source_limit) {
        memmove(_gz_srcdata, _gz_srcdata + consumed, _uzLib_decomp.source_limit - _uzLib_decomp.source);
        _uzLib_decomp.source_limit -= consumed;
        _uzLib_decomp.source -= consumed;
      } else {
        _uzLib_decomp.source = _gz_srcdata;
        _uzLib_decomp.source_limit = _gz_srcdata;
      }
      used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
      runUnzip = (final || (2 * GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK));

      //Serial.printf("DEBUG: quedan %u bytes en búfer de entrada\r\n", used);
    }
  }

  if (!_uploadRejected && _tar_eof) {
    // TODO: condición de fin de datos subidos
  }

  if (_uploadRejected || final) {
    tar_abort("tar cleanup", 0);
    if (_gz_dict != NULL) { delete _gz_dict; _gz_dict = NULL; }
    if (_gz_srcdata != NULL) { delete _gz_srcdata; _gz_srcdata = NULL; }
    if (_gz_dstdata != NULL) { delete _gz_dstdata; _gz_dstdata = NULL; }
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
  }
}

int YuboxOTAClass::_tar_cb_feedFromBuffer(unsigned char * buf, size_t size)
{
  if (size % TAR_BLOCK_SIZE != 0) {
    Serial.printf("ERR: _tar_cb_feedFromBuffer: longitud pedida %d no es múltiplo de %d\r\n", size, TAR_BLOCK_SIZE);
    return 0;
  }

  _tar_emptyChunk++;

  unsigned int gzExpandedSize = _uzLib_decomp.dest - _uzLib_decomp.dest_start;
  unsigned int copySize = gzExpandedSize - _tar_offset;
  //Serial.printf("DEBUG: _tar_cb_feedFromBuffer máximo copia posible %d bytes...\r\n", copySize);
  if (copySize > size) copySize = size;
  //Serial.printf("DEBUG: _tar_cb_feedFromBuffer copiando %d bytes...\r\n", copySize);
  memcpy(buf, _uzLib_decomp.dest_start + _tar_offset, copySize);

  // Si no hay suficientes datos para llenar size, se rellena con 0xFF
  if (copySize < size) {
    Serial.printf("WARN: _tar_cb_feedFromBuffer: se piden %d bytes pero sólo se pueden proveer %d bytes\r\n",
      size, copySize);
    memset(buf + copySize, 0xff, size - copySize);
    _tar_offset += copySize;
    return size;
  } else  {
    _tar_offset += copySize;
    return copySize;
  }
}

int YuboxOTAClass::_tar_cb_gotEntryHeader(header_translated_t * hdr, int entry_index)
{
  static const char * const firmwareSuffix = ".ino.nodemcu-32s.bin";

  //Serial.printf("DEBUG: _tar_cb_gotEntryHeader: %s INICIO\r\n", hdr->filename);
  switch (hdr->type)
  {
  case T_NORMAL:
    //Serial.printf("DEBUG: archivo ordinario tamaño 0x%08x%08x\r\n", (unsigned long)(hdr->filesize >> 32), (unsigned long)(hdr->filesize & 0xFFFFFFFFUL));

    // Verificar si el archivo que se presenta es un firmware a flashear. El firmware debe
    // tener un nombre que termine en ".ino.nodemcu-32s.bin" . Este es el valor por omisión
    // con el que se termina el nombre del archivo compilado exportado por Arduino IDE
    unsigned int fnLen = strlen(hdr->filename);
    if (fnLen > strlen(firmwareSuffix) && 0 == strcmp(hdr->filename + (fnLen - strlen(firmwareSuffix)), firmwareSuffix)) {
      //Serial.printf("DEBUG: detectado firmware: %s longitud %d bytes\r\n", hdr->filename, (unsigned long)(hdr->filesize & 0xFFFFFFFFUL));
      if (_tgzupload_foundFirmware) {
        Serial.printf("WARN: se ignora firmware duplicado: %s longitud %d bytes\r\n", hdr->filename, (unsigned long)(hdr->filesize & 0xFFFFFFFFUL));
      } else {
        _tgzupload_foundFirmware = true;
        if ((unsigned long)(hdr->filesize >> 32) != 0) {
          // El firmware excede de 4GB. Un dispositivo así de grande no existe
          _tgzupload_serverError = true;
          _tgzupload_responseMsg = _updater_errstr(UPDATE_ERROR_SIZE);
          _uploadRejected = true;
        } else if (!Update.begin(hdr->filesize, U_FLASH)) {
          _tgzupload_serverError = true;
          _tgzupload_responseMsg = _updater_errstr(Update.getError());
          _uploadRejected = true;
        } else {
          _tgzupload_currentOp = OTA_FIRMWARE_FLASH;
        }
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
  //Serial.write(block, size);
  switch (_tgzupload_currentOp) {
  case OTA_SPIFFS_WRITE:
    // TODO: implementar
    break;
  case OTA_FIRMWARE_FLASH:
    while (size > 0) {
      size_t r = Update.write(block, size);
      if (r == 0) {
        _tgzupload_serverError = true;
        _tgzupload_responseMsg = _updater_errstr(Update.getError());
        _uploadRejected = true;
        Update.abort();
        _tgzupload_currentOp = OTA_IDLE;
        break;
      }
      size -= r;
      block += r;
    }
    break;
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}

int YuboxOTAClass::_tar_cb_gotEntryEnd(header_translated_t * hdr, int entry_index)
{
  //Serial.printf("\r\nDEBUG: _tar_cb_gotEntryEnd: %s FINAL\r\n", hdr->filename);
  switch (_tgzupload_currentOp) {
  case OTA_SPIFFS_WRITE:
    _tgzupload_currentOp = OTA_IDLE;
    // TODO: implementar
    break;
  case OTA_FIRMWARE_FLASH:
    _tgzupload_currentOp = OTA_IDLE;
    if (!Update.end()) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = _updater_errstr(Update.getError());
      _uploadRejected = true;
    } else if (!Update.isFinished()) {
      _tgzupload_serverError = true;
      _tgzupload_responseMsg = "Actualización no ha podido finalizarse: ";
      _tgzupload_responseMsg += _updater_errstr(Update.getError());
      _uploadRejected = true;
    }
    break;
  }

  _tar_emptyChunk = 0;
  return _uploadRejected ? -1 : 0;
}

const char * YuboxOTAClass::_updater_errstr(uint8_t e)
{
  switch (e) {
  case UPDATE_ERROR_OK:
    return "(no hay error)";
  case UPDATE_ERROR_WRITE:
    return "Fallo al escribir al flash";
  case UPDATE_ERROR_ERASE:
    return "Fallo al borrar bloque flash";
  case UPDATE_ERROR_READ:
    return "Fallo al leer del flash";
  case UPDATE_ERROR_SPACE:
    return "No hay suficiente espacio en búfer para actualización";
  case UPDATE_ERROR_SIZE:
    return "Firmware demasiado grande para flash de dispositivo";
  case UPDATE_ERROR_STREAM:
    return "Error de lectura en stream fuente";
  case UPDATE_ERROR_MD5:
    return "Error en checksum MD5";
  case UPDATE_ERROR_MAGIC_BYTE:
    return "Firma incorrecta en firmware";
  case UPDATE_ERROR_ACTIVATE:
    return "Error en activación de firmware actualizado";
  case UPDATE_ERROR_NO_PARTITION:
    return "No puede localizarse partición para escribir firmware";
  case UPDATE_ERROR_BAD_ARGUMENT:
    return "Argumemto incorrecto";
  case UPDATE_ERROR_ABORT:
    return "Actualización abortada";
  default:
    return "(error desconocido)";
  }
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

  _uploadRejected = false;

  unsigned int httpCode = 200;
  if (clientError) httpCode = 400;
  if (serverError) httpCode = 500;

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(httpCode);
  DynamicJsonDocument json_doc(JSON_OBJECT_SIZE(2));
  json_doc["success"] = !(clientError || serverError);
  json_doc["msg"] = responseMsg.c_str();

  serializeJson(json_doc, *response);
  request->send(response);
}

YuboxOTAClass YuboxOTA;
