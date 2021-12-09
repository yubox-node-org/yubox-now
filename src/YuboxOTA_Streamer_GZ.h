#pragma once

#include "YuboxOTA_Streamer.h"

#include "uzlib/uzlib.h"     // https://github.com/pfalcon/uzlib

class YuboxOTA_Streamer_GZ : public YuboxOTA_Streamer
{
protected:
  // Datos requeridos para manejar la descompresión gzip
  struct uzlib_uncomp _uzLib_decomp;    // Estructura de descompresión de uzlib
  unsigned char * _gz_dict;             // Diccionario de símbolos gzip
  unsigned long _gz_expectedExpandedSize;
  bool _gz_headerParsed;                // Bandera de si ya se parseó cabecera
  bool _finalInputBuffer;

  unsigned char * _gz_buffer;          // Memoria de búfer de datos comprimidos
  uint32_t _gz_buffer_size;

  size_t _total_input = 0;
  unsigned char _last4bytes[4];         // Recordar los últimos 4 bytes vistos en entrada

  // La llamada attachInputBuffer copia puntero y longitud aquí
  const uint8_t * _data;
  size_t _len;

  // Transferir todos los datos que se puedan hasta llenar _gz_buffer, o hasta
  // que se acaben los datos en _data;
  bool _fillGzipInputBuffer(void);

public:
    YuboxOTA_Streamer_GZ(size_t outbuf_size = 2 * TAR_BLOCK_SIZE);
    virtual ~YuboxOTA_Streamer_GZ();

    virtual bool begin(void);
    virtual bool attachInputBuffer(const uint8_t * data, size_t len, bool final);
    virtual void detachInputBuffer(void);
    virtual bool transformInputBuffer(void);
    virtual bool inputStreamEOF(void);
    virtual size_t getTotalExpectedOutput(void) { return _gz_expectedExpandedSize; }
};