#include <Arduino.h>

#include <Update.h>
#include "SPIFFS.h"

#include "YuboxOTA_Flasher_ESP32.h"

#include "esp_task_wdt.h"

#define YUBOX_BUFSIZ SPI_FLASH_SEC_SIZE

YuboxOTA_Flasher_ESP32::YuboxOTA_Flasher_ESP32(void)
 : YuboxOTA_Flasher()
{
    _responseMsg = "";
    _tgzupload_currentOp = YBX_OTA_IDLE;
    _tgzupload_foundFirmware = false;
    _tgzupload_flashSuccess = false;
    _tgzupload_hasManifest = false;
    _tgzupload_filelist.clear();

    _uploadRejected = false;
    _filebuf = NULL; _filebuf_used = 0;
}

#define FREE_FILEBUF \
  do { if (_filebuf != NULL) free(_filebuf); _filebuf = NULL; } while (false)

YuboxOTA_Flasher_ESP32::~YuboxOTA_Flasher_ESP32()
{
  FREE_FILEBUF;
  cleanupFailedUpdateFiles();
}

bool YuboxOTA_Flasher_ESP32::isUpdateRejected(void)
{
    return _uploadRejected;
}

String YuboxOTA_Flasher_ESP32::getLastErrorMessage(void)
{
    return _responseMsg;
}

bool YuboxOTA_Flasher_ESP32::startUpdate(void)
{
    FREE_FILEBUF;
    _filebuf_used = 0;
    _tgzupload_flashSuccess = false;

    return true;
}

bool YuboxOTA_Flasher_ESP32::startFile(const char * filename, unsigned long long filesize)
{
    if (_filebuf_used != 0) {
      if (!_uploadRejected) {
        _responseMsg = "Datos de búfer no han sido evacuados. Esto no debería pasar.";
        _uploadRejected = true;
      }
      return false;
    }

    // Verificar si el archivo que se presenta es un firmware a flashear. El firmware debe
    // tener un nombre que termine en ".ino.nodemcu-32s.bin" . Este es el valor por omisión
    // con el que se termina el nombre del archivo compilado exportado por Arduino IDE
    unsigned int fnLen = strlen(filename);
    if (0 == strcmp(filename + (fnLen - 4), ".bin") &&
        NULL != strstr(filename, ".ino.")) {
      log_v("Detectado firmware: %s longitud %d bytes", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      if (_tgzupload_foundFirmware) {
        log_w("Se ignora firmware duplicado: %s longitud %d bytes", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      } else {
        // Previo a la escritura de firmware, el búfer de escritura de archivo se libera
        // porque no va a ser usado, y para mitigar escenarios de falta de RAM que impidan
        // el éxito de Update.begin(), que asigna un búfer interno.
        if (_filebuf != NULL) {
          FREE_FILEBUF;
          log_d("Liberado búfer de escritura de archivo previo a inicio de escritura firmware");
        }

        _tgzupload_foundFirmware = true;
        if ((unsigned long)(filesize >> 32) != 0) {
          // El firmware excede de 4GB. Un dispositivo así de grande no existe
          _responseMsg = "OTA Code update: ";
          _responseMsg += _updater_errstr(UPDATE_ERROR_SIZE);
          _uploadRejected = true;
        } else if (!Update.begin(filesize, U_FLASH)) {
          _responseMsg = "OTA Code update: no se puede iniciar actualización - ";
          auto upderr = Update.getError();
          if (upderr != UPDATE_ERROR_OK) {
            // Hay una causa reconocida para error de OTA
            _responseMsg += _updater_errstr(upderr);
          } else {
            // El objeto updater no ha reportado la causa del error. Se presume
            // que la verdadera causa (por examen del código) es memoria insuficiente
            // para el bloque de 4096 bytes del sector de flash a escribir.
            _responseMsg += "no hay suficiente RAM - libre ";

            multi_heap_info_t info = {0};
            heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);
            _responseMsg += info.total_free_bytes;
            _responseMsg += " max alloc ";
            _responseMsg += info.largest_free_block;
          }
          _uploadRejected = true;
        } else {
          _tgzupload_currentOp = YBX_OTA_FIRMWARE_FLASH;
          _tgzupload_bytesWritten = 0;
          _filestart_cb(filename, true, filesize);
        }
      }
    } else {
      log_v("Detectado archivo ordinario: %s longitud %d bytes", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      // Verificar si tengo suficiente espacio en SPIFFS para este archivo
      if (SPIFFS.totalBytes() < SPIFFS.usedBytes() + filesize) {
        String rep = _reportFilesystemSpace();
        log_e("No hay suficiente espacio: %s", rep.c_str());
        // No hay suficiente espacio para escribir este archivo
        _responseMsg = "No hay suficiente espacio en SPIFFS para archivo: ";
        _responseMsg += filename;
        _responseMsg += " ";
        _responseMsg += rep;
        _uploadRejected = true;
      } else {
        // Abrir archivo y agregarlo a lista de archivos a procesar al final
        String tmpname = "/n,"; tmpname += filename;
        log_v("Abriendo archivo %s ...", tmpname.c_str());
        _tgzupload_rsrc = SPIFFS.open(tmpname, FILE_WRITE);
        if (!_tgzupload_rsrc) {
          _responseMsg = "Fallo al abrir archivo para escribir: ";
          _responseMsg += filename;
          _uploadRejected = true;
        } else {
          _tgzupload_filelist.push_back((String)(filename));
          _tgzupload_currentOp = YBX_OTA_SPIFFS_WRITE;
          _tgzupload_bytesWritten = 0;
          _filestart_cb(filename, false, filesize);

          // Asignar búfer de escritura de archivos, si es posible
          if (_filebuf == NULL) {
            _filebuf = (uint8_t *)malloc(YUBOX_BUFSIZ);
            if (_filebuf == NULL) {
              log_w("No se puede asignar bufer para escribir archivos! Se continúa sin búfer (más lento)...");
            } else {
              log_d("Asignado búfer de escritura de archivos, longitud %d bytes", YUBOX_BUFSIZ);
            }
          }

          // Detectar si el tar contiene el manifest.txt
          if (strcmp(filename, "manifest.txt") == 0) _tgzupload_hasManifest = true;
        }
      }
    }

    return !_uploadRejected;
}

bool YuboxOTA_Flasher_ESP32::_flushFileBuffer(const char * filename, unsigned long long filesize)
{
    size_t r;

    if (_filebuf == NULL) {
        log_e("invocación con _filebuf == NULL - no debería pasar!");
        return false;
    }

    r = _tgzupload_rsrc.write(_filebuf, _filebuf_used);
    if (r <= 0) {
        _tgzupload_rsrc.close();
        _responseMsg = "Fallo al escribir archivo: ";
        _responseMsg += filename;
        _responseMsg += " ";
        _responseMsg += _reportFilesystemSpace();
        _uploadRejected = true;
        _tgzupload_currentOp = YBX_OTA_IDLE;
        return false;
    }
    _tgzupload_bytesWritten += r;
    if (r >= _filebuf_used) {
        _filebuf_used = 0;
    } else {
        // No se ha escrito todo el búfer.
        memmove(_filebuf, _filebuf + r, _filebuf_used - r);
        _filebuf_used -= r;
    }

    _fileprogress_cb(filename, false, filesize, _tgzupload_bytesWritten);
    return true;
}

bool YuboxOTA_Flasher_ESP32::appendFileData(const char * filename, unsigned long long filesize, unsigned char * block, int size)
{
    size_t r;

    switch (_tgzupload_currentOp) {
    case YBX_OTA_SPIFFS_WRITE:
        // Esto asume que el archivo ya fue abierto previamente
        while (size > 0) {
            if (_filebuf != NULL) {
                // Copiar cuanto se pueda del block al búfer hasta llenarlo
                r = YUBOX_BUFSIZ - _filebuf_used;
                if (r > size) r = size;
                memcpy(_filebuf + _filebuf_used, block, r);
                _filebuf_used += r;
                size -= r;
                block += r;

                // ¿Ya se puede evacuar el búfer al archivo abierto?
                if (_filebuf_used >= YUBOX_BUFSIZ) {
                    if (!_flushFileBuffer(filename, filesize)) break;
                }
            } else {
                // Escribir datos directamente ya que no hay _filebuf asignado
                r = _tgzupload_rsrc.write(block, size);
                if (r <= 0) {
                    _tgzupload_rsrc.close();
                    _responseMsg = "Fallo al escribir archivo: ";
                    _responseMsg += filename;
                    _responseMsg += " ";
                    _responseMsg += _reportFilesystemSpace();
                    _uploadRejected = true;
                    _tgzupload_currentOp = YBX_OTA_IDLE;
                    break;
                }
                _tgzupload_bytesWritten += r;
                size -= r;
                block += r;
            }
        }

        // Invocar directamente callback progreso si no hay búfer
        if (_filebuf == NULL) {
            _fileprogress_cb(filename, false, filesize, _tgzupload_bytesWritten);
        }
        break;
    case YBX_OTA_FIRMWARE_FLASH:
        while (size > 0) {
            r = Update.write(block, size);
            if (r == 0) {
                _responseMsg = "OTA Code update: fallo al escribir en flash - ";
                _responseMsg += _updater_errstr(Update.getError());
                _uploadRejected = true;
                Update.abort();
                _tgzupload_currentOp = YBX_OTA_IDLE;
                break;
            }
            _tgzupload_bytesWritten += r;
            size -= r;
            block += r;
        }
        _fileprogress_cb(filename, true, filesize, _tgzupload_bytesWritten);
        break;
    }

    return !_uploadRejected;
}

bool YuboxOTA_Flasher_ESP32::finishFile(const char * filename, unsigned long long filesize)
{
    switch (_tgzupload_currentOp) {
    case YBX_OTA_SPIFFS_WRITE:
        // Si no hay _filebuf asignado, se espera que _filebuf_used ya sea 0
        while (_filebuf_used > 0 && !_uploadRejected) {
            if (!_flushFileBuffer(filename, filesize)) break;
        }

        // Esto asume que el archivo todavía sigue abierto
        _tgzupload_currentOp = YBX_OTA_IDLE;
        _tgzupload_rsrc.close();
        _fileend_cb(filename, false, filesize);
        break;
    case YBX_OTA_FIRMWARE_FLASH:
        _tgzupload_currentOp = YBX_OTA_IDLE;

        if (!_uploadRejected) {
          vTaskDelay(1);

          // Finalizar operación de flash de firmware, si es necesaria
          log_d("YUBOX OTA: firmware-commit-start");
          if (!Update.end()) {
            _responseMsg = "OTA Code update: fallo al finalizar - ";
            _responseMsg += _updater_errstr(Update.getError());
            _uploadRejected = true;
            log_e("YUBOX OTA: firmware-commit-failed: %s", _responseMsg.c_str());
          } else if (!Update.isFinished()) {
            _responseMsg = "OTA Code update: actualización no ha podido finalizarse - ";
            _responseMsg += _updater_errstr(Update.getError());
            _uploadRejected = true;
            log_e("YUBOX OTA: firmware-commit-failed: %s", _responseMsg.c_str());
          } else {
            log_d("YUBOX OTA: firmware-commit-end");
            _tgzupload_flashSuccess = true;
          }
          log_d(" ...done");

          vTaskDelay(1);
        }

        _fileend_cb(filename, true, filesize);
        break;
    }

    return !_uploadRejected;
}

String YuboxOTA_Flasher_ESP32::_reportFilesystemSpace(void)
{
    auto t = SPIFFS.totalBytes();
    auto u = SPIFFS.usedBytes();
    String report = "total=";
    report += t;
    report += " usado=";
    report += u;
    report += " libre=";
    report += (t - u);
    return report;
}

bool YuboxOTA_Flasher_ESP32::finishUpdate(void)
{
    FREE_FILEBUF;

    log_d("YUBOX OTA: DESACTIVANDO WATCHDOG EN CORE-0");
    disableCore0WDT();
    esp_task_wdt_delete(NULL);

    if (!_tgzupload_hasManifest) {
      // No existe manifest.txt, esto no era un targz de firmware
      //_tgzupload_clientError = true;
      _responseMsg = "No se encuentra manifest.txt, archivo subido no es un firmware";
      _uploadRejected = true;
    }

    if (!_uploadRejected) {
      std::vector<String> old_filelist;

      vTaskDelay(1);

      // Cargar lista de archivos viejos a preservar
      log_d("YUBOX OTA: datafiles-load-oldmanifest");
      _loadManifest(old_filelist);
      log_d(" ...done");
      vTaskDelay(1);

      // Se BORRA cualquier archivo que empiece con el prefijo "b," reservado para rollback
      log_d("YUBOX OTA: datafiles-delete-oldbackup");
      _deleteFilesWithPrefix("b,");
      log_d(" ...done");
      vTaskDelay(1);

      // Se RENOMBRA todos los archivos en old_filelist con prefijo "b,"
      log_d("YUBOX OTA: datafiles-rename-oldfiles");
      _changeFileListPrefix(old_filelist, "", "b,");
      log_d(" ...done");
      vTaskDelay(1);
      old_filelist.clear();

      // Se RENOMBRA todos los archivos en _tgzupload_filelist quitando prefijo "n,"
      log_d("YUBOX OTA: datafiles-rename-newfiles");
      _changeFileListPrefix(_tgzupload_filelist, "n,", "");
      log_d(" ...done");
      vTaskDelay(1);
      _tgzupload_filelist.clear();
      log_d("YUBOX OTA: datafiles-end ...done");
      vTaskDelay(1);
    }

    if (_uploadRejected) _firmwareAbort();
    log_d("YUBOX OTA: REACTIVANDO WATCHDOG EN CORE-0");
    enableCore0WDT();

    return !_uploadRejected;
}

void YuboxOTA_Flasher_ESP32::truncateUpdate(void)
{
    FREE_FILEBUF;
    _firmwareAbort();
}

bool YuboxOTA_Flasher_ESP32::shouldReboot(void)
{
    return (_tgzupload_foundFirmware && !_uploadRejected);
}

bool YuboxOTA_Flasher_ESP32::canRollBack(void)
{
    return Update.canRollBack();
}

bool YuboxOTA_Flasher_ESP32::doRollBack(void)
{
    if (!Update.rollBack()) {
        _responseMsg = "No hay firmware a restaurar, o no fue restaurado correctamente.";
        return false;
    }

    std::vector<String> curr_filelist;
    std::vector<String> prev_filelist;

    // Cargar lista de archivos actuales a preservar
    _loadManifest(curr_filelist);

    // Cargar lista de archivos preservados, sin su prefijo
    _listFilesWithPrefix(prev_filelist, "b,", true);

    // Se RENOMBRA todos los archivos actuales con prefijo "R,"
    _changeFileListPrefix(curr_filelist, "", "R,");

    // Se RENOMBRA todos los archivos preservados quitando prefijo "b,"
    _changeFileListPrefix(prev_filelist, "b,", "");

    // Se renombra los archivos que eran actuales con prefijo "b,"
    _changeFileListPrefix(curr_filelist, "R,", "b,");

    curr_filelist.clear();
    prev_filelist.clear();

    return true;
}

const char * YuboxOTA_Flasher_ESP32::_updater_errstr(uint8_t e)
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

void YuboxOTA_Flasher_ESP32::_firmwareAbort(void)
{
  if (_tgzupload_foundFirmware) {
    if (_tgzupload_flashSuccess) {
      // Realizar rollback del flasheo de firmware que había tenido éxito anteriormente
      Update.rollBack();
      _tgzupload_flashSuccess = false;
    } else {
      // Abortar la operación de firmware si se estaba escribiendo
      Update.abort();
    }
  }

  cleanupFailedUpdateFiles();
}

void YuboxOTA_Flasher_ESP32::cleanupFailedUpdateFiles(void)
{
  // Se BORRA cualquier archivo que empiece con el prefijo "n,"
  _deleteFilesWithPrefix("n,");
}

void YuboxOTA_Flasher_ESP32::_loadManifest(std::vector<String> & flist)
{
  if (SPIFFS.exists("/manifest.txt")) {
    // Lista de archivos a preservar para rollback futuro
    // Se asume archivo de texto ordinario con líneas formato UNIX
    File h = SPIFFS.open("/manifest.txt", FILE_READ);
    if (!h) {
      log_w("/manifest.txt existe pero no se puede abrir. Los archivos de recursos podrían ser sobreescritos.");
    } else {
      bool selfref = false;
      while (h.available()) {
        String s = h.readStringUntil('\n');
        if (s == "manifest.txt") selfref = true;
        String sn = "/"; sn += s;
        if (!SPIFFS.exists(sn)) {
          // Si el archivo no existe pero existe el correspondiente .gz se arregla aquí
          sn += ".gz";
          if (SPIFFS.exists(sn)) {
            s += ".gz";
            flist.push_back(s);
          } else {
            log_w("WARN: %s no existe de forma ordinaria o comprimida, se omite!", s.c_str());
          }
        } else {
          // Archivo existe ordinariamente
          flist.push_back(s);
        }
      }
      h.close();
      if (!selfref) flist.push_back("manifest.txt");
    }
  } else {
    log_w("/manifest.txt no existe. Los archivos de recursos podrían ser sobreescritos.");
  }
}

void YuboxOTA_Flasher_ESP32::_listFilesWithPrefix(std::vector<String> & flist, const char * p, bool strip_prefix)
{
  std::vector<String>::iterator it;
  File h = SPIFFS.open("/");
  if (!h) {
    log_w("no es posible listar directorio.");
  } else if (!h.isDirectory()) {
    log_w("no es posible listar no-directorio.");
  } else {
    String prefix = "/";
    prefix += p;

    File f = h.openNextFile();
    while (f) {
      String s = f.name();
      f.close();
      if (s.startsWith("/")) {
        // Comportamiento arduino-esp32 hasta 1.0.6
        log_v("listado %s", s.c_str());
      } else {
        // Comportamiento arduino-esp32 2.0.0-alpha
        log_v("listado {/}%s", s.c_str());
        s = "/" + s;
      }
      if (s.startsWith(prefix)) {
        log_v("- se agrega a lista...");
        flist.push_back(strip_prefix ? s.substring(prefix.length()) : s);
      }

      f = h.openNextFile();
    }
    h.close();
  }
}

void YuboxOTA_Flasher_ESP32::_deleteFilesWithPrefix(const char * p)
{
  std::vector<String> del_filelist;

  _listFilesWithPrefix(del_filelist, p, false);

  for (auto it = del_filelist.begin(); it != del_filelist.end(); it++) {
    log_v("BORRANDO %s ...", it->c_str());
    if (!SPIFFS.remove(*it)) {
      log_w("no se pudo borrar %s !", it->c_str());
    }
  }
  del_filelist.clear();
}

void YuboxOTA_Flasher_ESP32::_changeFileListPrefix(std::vector<String> & flist, const char * op, const char * np)
{
  std::vector<String>::iterator it;
  String s, sn;

  for (it = flist.begin(); it != flist.end(); it++) {
    s = "/"; s += op; s += *it;      // Ruta original del archivo
    sn = "/"; sn += np; sn += *it;  // Ruta nueva del archivo
    log_v("RENOMBRANDO %s --> %s ...", s.c_str(), sn.c_str());
    if (!SPIFFS.rename(s, sn)) {
      log_w("no se pudo renombrar %s --> %s ...", s.c_str(), sn.c_str());
    }
  }
}
