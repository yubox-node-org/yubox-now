#include <Arduino.h>
#include "YuboxOTA_Streamer.h"

YuboxOTA_Streamer::YuboxOTA_Streamer(size_t outbuf_size)
{
    _outbuf = NULL;
    _outbuf_size = outbuf_size;
    _outbuf_used = 0;
    _total_output = 0;
}

bool YuboxOTA_Streamer::begin(void)
{
    if (_outbuf == NULL) {
        _outbuf = (uint8_t *)malloc(_outbuf_size);
        if (_outbuf == NULL) {
            _errMsg = "outbuf malloc failed: ";
            _errMsg += _outbuf_size;
            log_e("outbuf malloc failed, size %u", _outbuf_size);
        } else {
            log_d("outbuf allocated, size %u", _outbuf_size);
        }
    }
    return (_outbuf != NULL);
}

size_t YuboxOTA_Streamer::transferOutputData(uint8_t * outdata, size_t len)
{
    size_t copysize = (_outbuf != NULL) ? _outbuf_used : 0;
    if (copysize > len) copysize = len;
    if (copysize > 0) {
        memcpy(outdata, _outbuf, copysize);
        if (copysize < _outbuf_used) {
            memmove(_outbuf, _outbuf + copysize, _outbuf_used - copysize);
        }
        _outbuf_used -= copysize;
    }
    return copysize;
}

YuboxOTA_Streamer::~YuboxOTA_Streamer()
{
    if (_outbuf != NULL) {
        free(_outbuf);
        log_d("outbuf freed");
    }
    _outbuf = NULL;
}
