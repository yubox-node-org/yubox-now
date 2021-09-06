#pragma once

#include "YuboxOTA_Streamer.h"

class YuboxOTA_Streamer_Identity : public YuboxOTA_Streamer
{
protected:
    const uint8_t * _data;
    size_t _len;
    unsigned long _expectedSize;

public:
    YuboxOTA_Streamer_Identity(size_t outbuf_size = 2 * TAR_BLOCK_SIZE);
    virtual ~YuboxOTA_Streamer_Identity() = default;

    virtual bool attachInputBuffer(const uint8_t * data, size_t len, bool final);
    virtual void detachInputBuffer(void);
    virtual bool transformInputBuffer(void);
    virtual bool inputStreamEOF(void);
    virtual size_t getTotalExpectedOutput(void) { return _expectedSize; }
};