#include <Arduino.h>
#include "YuboxOTA_Streamer_GZ.h"

YuboxOTA_Streamer_GZ::YuboxOTA_Streamer_GZ(size_t outbuf_size)
    : YuboxOTA_Streamer(outbuf_size)
{
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    memset(_last4bytes, 0, sizeof(_last4bytes));
    _gz_buffer = _gz_dict = NULL;
    _gz_buffer_size = 0;
    _gz_headerParsed = false;
    _data = NULL; _len = 0;
}

#define GZIP_DICT_SIZE 32768

bool YuboxOTA_Streamer_GZ::begin(void)
{
    if (!YuboxOTA_Streamer::begin()) return false;

    _gz_expectedExpandedSize = 0;
    _total_output = 0;
    _gz_headerParsed = false;
    _finalInputBuffer = false;
    _total_input = 0;

    /* El valor de GZIP_BUFF_SIZE es suficiente para al menos dos fragmentos de datos entrantes.
     * Se descomprime únicamente 2 * TAR_BLOCK_SIZE a la vez para simplificar el código y para que
     * no ocurra que se acabe el búfer de datos de entrada antes de llenar el búfer de salida.
     */
    if (_gz_dict == NULL) {
        _gz_dict = (unsigned char *)malloc(GZIP_DICT_SIZE);
        if (NULL == _gz_dict) {
            _errMsg = "_gz_dict malloc failed: ";
            _errMsg += GZIP_DICT_SIZE;
            return false;
        } else {
            log_d("_gz_dict allocated, size %u", GZIP_DICT_SIZE);
        }
    }

    if (_gz_buffer == NULL) {
        /* Se asume que el búfer de entrada de _outbuf_size * 1.5 debe proveer suficientes
         * datos para llenar como mínimo _outbuf_size de datos DESCOMPRIMIDOS. */
        _gz_buffer_size = (_outbuf_size + (_outbuf_size >> 1));
        _gz_buffer = (unsigned char *)malloc(_gz_buffer_size);
        if (_gz_buffer == NULL) {
            if (_gz_dict != NULL) free(_gz_dict); _gz_dict = NULL;

            _errMsg = "_gz_srcdata malloc failed: ";
            _errMsg += _gz_buffer_size;
            _gz_buffer_size = 0;
            return false;
        }
        log_d("_gz_srcdata allocated, size %u", _gz_buffer_size);
    }

    uzlib_init();
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    uzlib_uncompress_init(&_uzLib_decomp, _gz_dict, GZIP_DICT_SIZE);

    _uzLib_decomp.source = _gz_buffer;          // <-- El búfer inicial de datos
    _uzLib_decomp.source_limit = _gz_buffer;    // <-- Será movido al agregar los datos recibidos

    return true;
}

bool YuboxOTA_Streamer_GZ::_fillGzipInputBuffer(void)
{
    unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
    bool outputAvailable = !isOutputBufferFull();

    if (_data != NULL && used < _gz_buffer_size) {
        // Longitud de datos que se pueden copiar
        unsigned int copySize = _gz_buffer_size - used;
        if (copySize > _len ) copySize = _len;

        // Realizar copia de datos y verificar si búfer está lleno
        memcpy((void *)_uzLib_decomp.source_limit, _data, copySize);
        _uzLib_decomp.source_limit += copySize;
        used += copySize;
        _data += copySize;
        _len -= copySize;

        // Descartar búfer ya vaciado
        if (_len <= 0) {
            _data = NULL;
            _len = 0;
        }
    }

    // Si búfer está lleno, se puede proceder a descompresión
    return (outputAvailable && (_finalInputBuffer || (used >= _gz_buffer_size)));
}

bool YuboxOTA_Streamer_GZ::attachInputBuffer(const uint8_t * data, size_t len, bool final)
{
    // Agregar búfer recibido al búfer de entrada
    unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
    log_v("INICIO: used=%u MAX=%u", used, _gz_buffer_size);

    _data = data;
    _len = len;
    _fillGzipInputBuffer();
    used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
    log_v("LUEGO DE AGREGAR chunk: used=%u MAX=%u", used, _gz_buffer_size);

    // Actualizar los últimos 4 bytes vistos
    _total_input += len;
    if (len >= 4) {
        memcpy(_last4bytes, data + len -4, 4);
    } else {
        memmove(_last4bytes, _last4bytes + len, 4 - len);
        memcpy(_last4bytes + 4 - len, data, len);
    }

    if (final) {
        // Para el último bloque HTTP debería tenerse los 4 últimos bytes LSB que indican
        // el tamaño esperado de longitud expandida.
        if (_total_input < 4) {
            log_e("no hay suficientes datos para determinar tamaño expandido, se tienen %u bytes", _total_input);
            _errMsg = "Archivo es demasiado corto para validar longitud gzip";
            return false;
        } else {
            _gz_expectedExpandedSize =
                ((unsigned long)(_last4bytes[0])      ) |
                ((unsigned long)(_last4bytes[1]) <<  8) |
                ((unsigned long)(_last4bytes[2]) << 16) |
                ((unsigned long)(_last4bytes[3]) << 24);
            log_v("longitud esperada de datos expandidos es %lu bytes", _gz_expectedExpandedSize);
            if (_gz_expectedExpandedSize < _total_output) {
                log_e("longitud ya expandida excede longitud esperada! %lu > %lu", _total_output, _gz_expectedExpandedSize);
                _errMsg = "Longitud esperada inconsistente con datos ya expandidos";
                return false;
            }
        }

        _finalInputBuffer = true;
    }

    return true;
}

bool YuboxOTA_Streamer_GZ::transformInputBuffer(void)
{
    int r;

    // Ejecutar la descompresión hasta que se llene el búfer de salida, o hasta que
    // se acabe el búfer de entrada.
    bool runUnzip = _fillGzipInputBuffer();
    while (runUnzip && !inputStreamEOF()) {
        log_v("_total_output=%lu _gz_expectedExpandedSize=%lu", _total_output, _gz_expectedExpandedSize);
        log_v("se tienen %u bytes, se ejecuta gunzip...", _uzLib_decomp.source_limit - _uzLib_decomp.source);
        if (!_gz_headerParsed) {
            // Se requiere parsear cabecera gzip para validación
            r = uzlib_gzip_parse_header(&_uzLib_decomp);
            if (r != TINF_OK) {
                // Fallo al parsear la cabecera gzip
                log_e("fallo al parsear cabecera gzip");
                _errMsg = "Archivo no parece ser un archivo tar.gz, o está corrupto";
                return false;
            }
            // Cabecera gzip OK, se ajustan búferes
            _gz_headerParsed = true;
        } else {
            _uzLib_decomp.dest_start = _outbuf;
            _uzLib_decomp.dest = _outbuf + _outbuf_used;
            _uzLib_decomp.dest_limit = _outbuf + _outbuf_size;
            if (_finalInputBuffer && (_gz_expectedExpandedSize - _total_output) < _outbuf_size - _outbuf_used) {
                _uzLib_decomp.dest_limit = _outbuf + _outbuf_used + (_gz_expectedExpandedSize - _total_output);
            }

            while (_uzLib_decomp.dest < _uzLib_decomp.dest_limit) {
                r = uzlib_uncompress(&_uzLib_decomp);
                if (r != TINF_DONE && r != TINF_OK) {
                    log_e("fallo al descomprimir gzip (err=%d)", r);
                    _errMsg = "Archivo corrupto o truncado (gzip), no puede descomprimirse";
                    return false;
                }
            }
            _total_output += (_uzLib_decomp.dest - _uzLib_decomp.dest_start) - _outbuf_used;
            log_v("producidos %u bytes expandidos:", (_uzLib_decomp.dest - _uzLib_decomp.dest_start) - _outbuf_used);
            _outbuf_used = _uzLib_decomp.dest - _uzLib_decomp.dest_start;
        }

        // Ajustar bufer fuente que _gz_buffer inicie otra vez con los datos a consumir
        unsigned int consumed = _uzLib_decomp.source - _gz_buffer;
        if (consumed != 0) {
            log_v("parseo gzip consumió %u bytes, se ajusta...", consumed);
            if (_uzLib_decomp.source < _uzLib_decomp.source_limit) {
                memmove(_gz_buffer, _gz_buffer + consumed, _uzLib_decomp.source_limit - _uzLib_decomp.source);
                _uzLib_decomp.source_limit -= consumed;
                _uzLib_decomp.source -= consumed;
            } else {
                _uzLib_decomp.source = _gz_buffer;
                _uzLib_decomp.source_limit = _gz_buffer;
            }
        } else {
            log_v("parseo gzip no consumió bytes...");
        }
        runUnzip = _fillGzipInputBuffer();
    }

    return true;
}

void YuboxOTA_Streamer_GZ::detachInputBuffer(void)
{
    // En este punto, se debió haber ya consumido el búfer inicial y asignado NULL
    if (_data != NULL) {
        log_w("quedan %d bytes sin consumir de entrada, SE PIERDEN!", _len);
    }
    _data = NULL;
    _len = 0;
}

bool YuboxOTA_Streamer_GZ::inputStreamEOF(void)
{
    return (_gz_expectedExpandedSize != 0 && (_total_output >= _gz_expectedExpandedSize));
}

YuboxOTA_Streamer_GZ::~YuboxOTA_Streamer_GZ()
{
    if (_gz_dict != NULL) {
        free(_gz_dict);
        _gz_dict = NULL;
        log_d("_gz_dict freed");
    }
    if (_gz_buffer != NULL) {
        free(_gz_buffer);
        _gz_buffer = NULL;
        log_d("_gz_buffer freed");
    }
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
}
