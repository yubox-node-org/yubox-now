#include <Arduino.h>
#include "YuboxOTA_Streamer_GZ.h"

YuboxOTA_Streamer_GZ::YuboxOTA_Streamer_GZ(size_t outbuf_size)
    : YuboxOTA_Streamer(outbuf_size)
{
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    _gz_srcdata = _gz_dict = NULL;
    _gz_headerParsed = false;
}

#define GZIP_DICT_SIZE 32768
#define GZIP_BUFF_SIZE 4096

/* De pruebas se ha visto que el máximo tamaño de segmento de datos en el upload es
 * de 1460 bytes. Si el espacio libre en _gz_srcdata es igual o menor al siguiente
 * valor, se iniciará la descompresión con los datos leídos hasta el momento.
 */
#define GZIP_FILL_WATERMARK 1500

bool YuboxOTA_Streamer_GZ::begin(void)
{
    if (!YuboxOTA_Streamer::begin()) return false;

    _gz_expectedExpandedSize = 0;
    _total_output = 0;
    _gz_headerParsed = false;
    _finalInputBuffer = false;

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

    if (_gz_srcdata == NULL) {
        _gz_srcdata = (unsigned char *)malloc(GZIP_BUFF_SIZE);
        if (_gz_srcdata == NULL) {
            if (_gz_dict != NULL) free(_gz_dict); _gz_dict = NULL;

            _errMsg = "_gz_srcdata malloc failed: ";
            _errMsg += GZIP_BUFF_SIZE;
            return false;
        }
        log_d("_gz_srcdata allocated, size %u", GZIP_BUFF_SIZE);
    }

    uzlib_init();
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
    uzlib_uncompress_init(&_uzLib_decomp, _gz_dict, GZIP_DICT_SIZE);

    _uzLib_decomp.source = _gz_srcdata;         // <-- El búfer inicial de datos
    _uzLib_decomp.source_limit = _gz_srcdata;   // <-- Será movido al agregar los datos recibidos

    return true;
}

bool YuboxOTA_Streamer_GZ::attachInputBuffer(const uint8_t * data, size_t len, bool final)
{
    // Agregar búfer recibido al búfer de entrada
    unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
    log_v("INICIO: used=%u MAX=%u", used, GZIP_BUFF_SIZE);

    if (GZIP_BUFF_SIZE - used < len) {
        // No hay suficiente espacio para este bloque de datos. ESTO NO DEBERÍA PASAR
        log_e("no hay suficiente espacio en _gz_srcdata: libre=%u requerido=%u", GZIP_BUFF_SIZE - used, len);
        _errMsg = "(internal) Falta espacio en búfer para siguiente pedazo de datos!";
        return false;
    }

    memcpy((void *)_uzLib_decomp.source_limit, data, len);
    _uzLib_decomp.source_limit += len;
    used += len;
    log_v("LUEGO DE AGREGAR chunk: used=%u MAX=%u", used, GZIP_BUFF_SIZE);

    if (final) {
        // Para el último bloque HTTP debería tenerse los 4 últimos bytes LSB que indican
        // el tamaño esperado de longitud expandida.
        if (used < 4) {
            log_e("no hay suficientes datos luego de bloque final para determinar tamaño expandido, se tienen %u bytes", used);
            _errMsg = "Archivo es demasiado corto para validar longitud gzip";
            return false;
        } else {
            _gz_expectedExpandedSize =
                ((unsigned long)(*(_uzLib_decomp.source_limit - 4))      ) |
                ((unsigned long)(*(_uzLib_decomp.source_limit - 3)) <<  8) |
                ((unsigned long)(*(_uzLib_decomp.source_limit - 2)) << 16) |
                ((unsigned long)(*(_uzLib_decomp.source_limit - 1)) << 24);
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
    unsigned int used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
    int r;

    // Ejecutar la descompresión hasta que se llene el búfer de salida, o hasta que
    // se acabe el búfer de entrada.
    bool runUnzip = (!isOutputBufferFull() && (_finalInputBuffer || (GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK)));
    while (runUnzip && !inputStreamEOF()) {
        log_v("_total_output=%lu _gz_expectedExpandedSize=%lu", _total_output, _gz_expectedExpandedSize);
        log_v("se tienen %u bytes, se ejecuta gunzip...", used);
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

        // Ajustar bufer fuente que _gz_srcdata inicie otra vez con los datos a consumir
        unsigned int consumed = _uzLib_decomp.source - _gz_srcdata;
        if (consumed != 0) {
            log_v("parseo gzip consumió %u bytes, se ajusta...", consumed);
            if (_uzLib_decomp.source < _uzLib_decomp.source_limit) {
                memmove(_gz_srcdata, _gz_srcdata + consumed, _uzLib_decomp.source_limit - _uzLib_decomp.source);
                _uzLib_decomp.source_limit -= consumed;
                _uzLib_decomp.source -= consumed;
            } else {
                _uzLib_decomp.source = _gz_srcdata;
                _uzLib_decomp.source_limit = _gz_srcdata;
            }
        } else {
            log_v("parseo gzip no consumió bytes...");
        }
        used = _uzLib_decomp.source_limit - _uzLib_decomp.source;
        runUnzip = (!isOutputBufferFull() && (_finalInputBuffer || (GZIP_BUFF_SIZE - used < GZIP_FILL_WATERMARK)));
    }

    return true;
}

void YuboxOTA_Streamer_GZ::detachInputBuffer(void)
{
    // NOOP
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
    if (_gz_srcdata != NULL) {
        free(_gz_srcdata);
        _gz_srcdata = NULL;
        log_d("_gz_srcdata freed");
    }
    memset(&_uzLib_decomp, 0, sizeof(struct uzlib_uncomp));
}
