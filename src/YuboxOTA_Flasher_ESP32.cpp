#include <Arduino.h>

#include <Update.h>
#include "SPIFFS.h"

#include "YuboxOTA_Flasher_ESP32.h"

#include "esp_task_wdt.h"

YuboxOTA_Flasher_ESP32::YuboxOTA_Flasher_ESP32(
    YuboxOTA_Flasher_FileStart_func_cb filestart_cb,
    YuboxOTA_Flasher_FileProgress_func_cb fileprogress_cb,
    YuboxOTA_Flasher_FileEnd_func_cb fileend_cb
)
{
    _filestart_cb = filestart_cb;
    _fileprogress_cb = fileprogress_cb;
    _fileend_cb = fileend_cb;

    _responseMsg = "";
    _tgzupload_currentOp = YBX_OTA_IDLE;
    _tgzupload_foundFirmware = false;
    _tgzupload_canFlash = false;
    _tgzupload_hasManifest = false;
    _tgzupload_filelist.clear();

    _uploadRejected = false;
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
    return true;
}

bool YuboxOTA_Flasher_ESP32::startFile(const char * filename, unsigned long long filesize)
{
    // Verificar si el archivo que se presenta es un firmware a flashear. El firmware debe
    // tener un nombre que termine en ".ino.nodemcu-32s.bin" . Este es el valor por omisión
    // con el que se termina el nombre del archivo compilado exportado por Arduino IDE
    unsigned int fnLen = strlen(filename);
    if (0 == strcmp(filename + (fnLen - 4), ".bin") &&
        NULL != strstr(filename, ".ino.")) {
      //Serial.printf("DEBUG: detectado firmware: %s longitud %d bytes\r\n", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      if (_tgzupload_foundFirmware) {
        Serial.printf("WARN: se ignora firmware duplicado: %s longitud %d bytes\r\n", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      } else {
        _tgzupload_foundFirmware = true;
        if ((unsigned long)(filesize >> 32) != 0) {
          // El firmware excede de 4GB. Un dispositivo así de grande no existe
          _responseMsg = "OTA Code update: ";
          _responseMsg += _updater_errstr(UPDATE_ERROR_SIZE);
          _uploadRejected = true;
        } else if (!Update.begin(filesize, U_FLASH)) {
          _responseMsg = "OTA Code update: no se puede iniciar actualización - ";
          _responseMsg += _updater_errstr(Update.getError());
          _uploadRejected = true;
        } else {
          _tgzupload_currentOp = YBX_OTA_FIRMWARE_FLASH;
          _tgzupload_bytesWritten = 0;
          _filestart_cb(filename, true, filesize);
        }
      }
    } else {
      //Serial.printf("DEBUG: detectado archivo ordinario: %s longitud %d bytes\r\n", filename, (unsigned long)(filesize & 0xFFFFFFFFUL));
      // Verificar si tengo suficiente espacio en SPIFFS para este archivo
      if (SPIFFS.totalBytes() < SPIFFS.usedBytes() + filesize) {
        Serial.printf("ERR: no hay suficiente espacio: total=%lu usado=%u\r\n", SPIFFS.totalBytes(), SPIFFS.usedBytes());
        // No hay suficiente espacio para escribir este archivo
        _responseMsg = "No hay suficiente espacio en SPIFFS para archivo: ";
        _responseMsg += filename;
        _uploadRejected = true;
      } else {
        // Abrir archivo y agregarlo a lista de archivos a procesar al final
        String tmpname = "/n,"; tmpname += filename;
        //Serial.printf("DEBUG: abriendo archivo %s ...\r\n", tmpname.c_str());
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

          // Detectar si el tar contiene el manifest.txt
          if (strcmp(filename, "manifest.txt") == 0) _tgzupload_hasManifest = true;
        }
      }
    }

    return !_uploadRejected;
}

bool YuboxOTA_Flasher_ESP32::appendFileData(const char * filename, unsigned long long filesize, unsigned char * block, int size)
{
    size_t r;

    switch (_tgzupload_currentOp) {
    case YBX_OTA_SPIFFS_WRITE:
        // Esto asume que el archivo ya fue abierto previamente
        while (size > 0) {
            r = _tgzupload_rsrc.write(block, size);
            if (r == 0) {
                _tgzupload_rsrc.close();
                _responseMsg = "Fallo al escribir archivo: ";
                _responseMsg += filename;
                _uploadRejected = true;
                _tgzupload_currentOp = YBX_OTA_IDLE;
                break;
            }
            _tgzupload_bytesWritten += r;
            _fileprogress_cb(filename, false, filesize, _tgzupload_bytesWritten);
            size -= r;
            block += r;
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
            _fileprogress_cb(filename, true, filesize, _tgzupload_bytesWritten);
            size -= r;
            block += r;
        }
        break;
    }

    return !_uploadRejected;
}

bool YuboxOTA_Flasher_ESP32::finishFile(const char * filename, unsigned long long filesize)
{
    //Serial.printf("\r\nDEBUG: _tar_cb_gotEntryEnd: %s FINAL\r\n", hdr->filename);
    switch (_tgzupload_currentOp) {
    case YBX_OTA_SPIFFS_WRITE:
        // Esto asume que el archivo todavía sigue abierto
        _tgzupload_currentOp = YBX_OTA_IDLE;
        _tgzupload_rsrc.close();
        _fileend_cb(filename, false, filesize);
        break;
    case YBX_OTA_FIRMWARE_FLASH:
        _tgzupload_currentOp = YBX_OTA_IDLE;
        _tgzupload_canFlash = true;
        _fileend_cb(filename, true, filesize);
        break;
    }

    return !_uploadRejected;
}

bool YuboxOTA_Flasher_ESP32::finishUpdate(void)
{

#ifdef DEBUG_YUBOX_OTA
    Serial.println("YUBOX OTA: DESACTIVANDO WATCHDOG EN CORE-0");
#endif
    disableCore0WDT();
    esp_task_wdt_delete(NULL);

    if (!_tgzupload_hasManifest) {
      // No existe manifest.txt, esto no era un targz de firmware
      //_tgzupload_clientError = true;
      _responseMsg = "No se encuentra manifest.txt, archivo subido no es un firmware";
      _uploadRejected = true;
    }

    if (!_uploadRejected && _tgzupload_canFlash) {
      vTaskDelay(1);

      // Finalizar operación de flash de firmware, si es necesaria
#ifdef DEBUG_YUBOX_OTA
      Serial.println("YUBOX OTA: firmware-commit-start");
#endif
      if (!Update.end()) {
        _responseMsg = "OTA Code update: fallo al finalizar - ";
        _responseMsg += _updater_errstr(Update.getError());
        _uploadRejected = true;
#ifdef DEBUG_YUBOX_OTA
        Serial.print("YUBOX OTA: firmware-commit-failed ");
        Serial.print(_responseMsg);
#endif
      } else if (!Update.isFinished()) {
        _responseMsg = "OTA Code update: actualización no ha podido finalizarse - ";
        _responseMsg += _updater_errstr(Update.getError());
        _uploadRejected = true;
#ifdef DEBUG_YUBOX_OTA
        Serial.print("YUBOX OTA: firmware-commit-failed ");
        Serial.print(_responseMsg);
#endif
      } else {
#ifdef DEBUG_YUBOX_OTA
        Serial.print("YUBOX OTA: firmware-commit-end");
#endif
      }
#ifdef DEBUG_YUBOX_OTA
      Serial.println(" ...done");
#endif

      vTaskDelay(1);
    }

    if (!_uploadRejected) {
      std::vector<String> old_filelist;

      vTaskDelay(1);

      // Cargar lista de archivos viejos a preservar
#ifdef DEBUG_YUBOX_OTA
      Serial.print("YUBOX OTA: datafiles-load-oldmanifest");
#endif
      _loadManifest(old_filelist);
#ifdef DEBUG_YUBOX_OTA
      Serial.println(" ...done");
#endif
      vTaskDelay(1);

      // Se BORRA cualquier archivo que empiece con el prefijo "b," reservado para rollback
#ifdef DEBUG_YUBOX_OTA
      Serial.print("YUBOX OTA: datafiles-delete-oldbackup");
#endif
      _deleteFilesWithPrefix("b,");
#ifdef DEBUG_YUBOX_OTA
      Serial.println(" ...done");
#endif
      vTaskDelay(1);

      // Se RENOMBRA todos los archivos en old_filelist con prefijo "b,"
#ifdef DEBUG_YUBOX_OTA
      Serial.print("YUBOX OTA: datafiles-rename-oldfiles");
#endif
      _changeFileListPrefix(old_filelist, "", "b,");
#ifdef DEBUG_YUBOX_OTA
      Serial.println(" ...done");
#endif
      vTaskDelay(1);
      old_filelist.clear();

      // Se RENOMBRA todos los archivos en _tgzupload_filelist quitando prefijo "n,"
#ifdef DEBUG_YUBOX_OTA
      Serial.print("YUBOX OTA: datafiles-rename-newfiles");
#endif
      _changeFileListPrefix(_tgzupload_filelist, "n,", "");
#ifdef DEBUG_YUBOX_OTA
      Serial.println(" ...done");
#endif
      vTaskDelay(1);
      _tgzupload_filelist.clear();
#ifdef DEBUG_YUBOX_OTA
      Serial.print("YUBOX OTA: datafiles-end");
      Serial.println(" ...done");
#endif
      vTaskDelay(1);
    }

    if (_uploadRejected) _firmwareAbort();
#ifdef DEBUG_YUBOX_OTA
    Serial.println("YUBOX OTA: REACTIVANDO WATCHDOG EN CORE-0");
#endif
    enableCore0WDT();

    return !_uploadRejected;
}

void YuboxOTA_Flasher_ESP32::truncateUpdate(void)
{
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
    _listFilesWithPrefix(prev_filelist, "b,");

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
    // Abortar la operación de firmware si se estaba escribiendo
    Update.abort();
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
      Serial.println("WARN: /manifest.txt existe pero no se puede abrir. Los archivos de recursos podrían ser sobreescritos.");
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
            Serial.printf("WARN: %s no existe de forma ordinaria o comprimida, se omite!\r\n", s.c_str());
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
    Serial.println("WARN: /manifest.txt no existe. Los archivos de recursos podrían ser sobreescritos.");
  }
}

void YuboxOTA_Flasher_ESP32::_listFilesWithPrefix(std::vector<String> & flist, const char * p)
{
  std::vector<String>::iterator it;
  File h = SPIFFS.open("/");
  if (!h) {
    Serial.println("WARN: no es posible listar directorio.");
  } else if (!h.isDirectory()) {
    Serial.println("WARN: no es posible listar no-directorio.");
  } else {
    String prefix = "/";
    prefix += p;

    File f = h.openNextFile();
    while (f) {
      String s = f.name();
      f.close();
      //Serial.printf("DEBUG: listado %s\r\n", s.c_str());
      if (s.startsWith(prefix)) {
        //Serial.println("DEBUG: se agrega a lista a borrar...");
        flist.push_back(s.substring(prefix.length()));
      }

      f = h.openNextFile();
    }
    h.close();
  }
}

void YuboxOTA_Flasher_ESP32::_deleteFilesWithPrefix(const char * p)
{
  std::vector<String> del_filelist;
  std::vector<String>::iterator it;
  File h = SPIFFS.open("/");
  if (!h) {
    Serial.println("WARN: no es posible listar directorio.");
  } else if (!h.isDirectory()) {
    Serial.println("WARN: no es posible listar no-directorio.");
  } else {
    String prefix = "/";
    prefix += p;

    File f = h.openNextFile();
    while (f) {
      String s = f.name();
      f.close();
      //Serial.printf("DEBUG: listado %s\r\n", s.c_str());
      if (s.startsWith(prefix)) {
        //Serial.println("DEBUG: se agrega a lista a borrar...");
        del_filelist.push_back(s);
      }

      f = h.openNextFile();
    }
    h.close();
  }
  for (it = del_filelist.begin(); it != del_filelist.end(); it++) {
    //Serial.printf("DEBUG: BORRANDO %s ...\r\n", it->c_str());
    if (!SPIFFS.remove(*it)) {
      Serial.printf("WARN: no se pudo borrar %s !\r\n", it->c_str());
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
    //Serial.printf("DEBUG: RENOMBRANDO %s --> %s ...\r\n", s.c_str(), sn.c_str());
    if (!SPIFFS.rename(s, sn)) {
      Serial.printf("WARN: no se pudo renombrar %s --> %s ...\r\n", s.c_str(), sn.c_str());
    }
  }
}
