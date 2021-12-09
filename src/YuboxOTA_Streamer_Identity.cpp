#include <Arduino.h>
#include "YuboxOTA_Streamer_Identity.h"

YuboxOTA_Streamer_Identity::YuboxOTA_Streamer_Identity(size_t outbuf_size)
    : YuboxOTA_Streamer(outbuf_size)
{
    _data = NULL;
    _len = 0;
    _expectedSize = 0;
}

bool YuboxOTA_Streamer_Identity::attachInputBuffer(const uint8_t * data, size_t len, bool final)
{
    _data = data;
    _len = len;

    if (final) _expectedSize = _total_output + len;
    return true;
}

bool YuboxOTA_Streamer_Identity::transformInputBuffer(void)
{
    auto n = _outbuf_size - _outbuf_used;
    if (n > _len) n = _len;
    if (n > 0) {
        memcpy(_outbuf + _outbuf_used, _data, n);
        _data += n;
        _len -= n;
        _outbuf_used += n;
        _total_output += n;
    }
    return true;
}

void YuboxOTA_Streamer_Identity::detachInputBuffer(void)
{
    if (_len > 0) log_w("todavía no se transfieren %u bytes, se pierden!", _len);
    _data = NULL;
    _len = 0;
}

bool YuboxOTA_Streamer_Identity::inputStreamEOF(void)
{
    return (_expectedSize != 0 && (_total_output >= _expectedSize));
}
