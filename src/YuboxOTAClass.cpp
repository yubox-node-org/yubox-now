#include "YuboxOTAClass.h"

#define ARDUINOJSON_USE_LONG_LONG 1

#include "AsyncJson.h"
#include "ArduinoJson.h"

#include <functional>

#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib
extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

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

YuboxOTAClass::YuboxOTAClass(void)
{
  _uploadRejected = false;
  _gz_srcdata = NULL;
  _gz_dstdata = NULL;
  _gz_dict = NULL;
  _gz_actualExpandedSize = 0;
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
  int r;

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
    } else if (len >= 2 && !(data[0] == 0x1f && data[1] == 0x8b)) {
      // A pesar del nombre, los datos no parecen ser un .tar.gz válido
      _uploadRejected = true;

      // Por ahora no manejo el caso en el cual el primer segmento es de menos de 2 bytes
    }
  }
  
  if (_uploadRejected) return;

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
  }

  // Agregar búfer recibido al búfer de entrada, notando si debe empezarse a parsear gzip
  unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
  unsigned int consumed;
  bool runUnzip = false;
  if (2 * GZIP_BUFF_SIZE - used < len) {
    // No hay suficiente espacio para este bloque de datos. ESTO NO DEBERÍA PASAR
    Serial.printf("ERR: no hay suficiente espacio en _gz_srcdata: libre=%u requerido=%u\r\n", 2 * GZIP_BUFF_SIZE - used, len);
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
          _uploadRejected = true;
          break;
        }
        unsigned int gzExpandedSize = _uzLib_decomp.dest - _uzLib_decomp.dest_start;
        _gz_actualExpandedSize += gzExpandedSize;
        //Serial.printf("DEBUG: producidos %u bytes expandidos:\r\n", gzExpandedSize);

        if (gz_expectedExpandedSize != 0 && _gz_actualExpandedSize >= gz_expectedExpandedSize) {
          // TODO: condición de fin de datos subidos
        }
        // TODO: pasar búfer descomprimido a rutina tar
        Serial.write(_uzLib_decomp.dest_start, _uzLib_decomp.dest - _uzLib_decomp.dest_start);
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

  if (_uploadRejected || final) {
    if (_gz_dict != NULL) { delete _gz_dict; _gz_dict = NULL; }
    if (_gz_srcdata != NULL) { delete _gz_srcdata; _gz_srcdata = NULL; }
    if (_gz_dstdata != NULL) { delete _gz_dstdata; _gz_dstdata = NULL; }
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
  }
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

  Serial.println("DEBUG: tgzupload_POST a punto de revisar existencia de parámetro");
  if (request->hasParam("tgzupload", true, true)) {
    Serial.println("DEBUG: tgzupload_POST tgzupload SÍ existe como upload");
  } else {
    Serial.println("DEBUG: tgzupload_POST tgzupload NO existe como upload");
  }

  _uploadRejected = false;
  request->send(500, "application/json", "{\"msg\":\"DEBUG: Carga de actualización no implementado\"}");
}

YuboxOTAClass YuboxOTA;
