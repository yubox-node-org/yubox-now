#pragma once

extern "C" {
  #include "TinyUntar/untar.h" // https://github.com/dsoprea/TinyUntar
}

class YuboxOTA_Streamer
{
protected:
    uint8_t * _outbuf;
    size_t _outbuf_size;
    size_t _outbuf_used;
    size_t _total_output;
    String _errMsg;

public:
    YuboxOTA_Streamer(size_t outbuf_size = 2 * TAR_BLOCK_SIZE);
    virtual ~YuboxOTA_Streamer();
    String getErrorMessage() { return _errMsg; }

    // Allocate all required buffers, return TRUE if successfully allocated
    virtual bool begin(void);

    // Indicate that the buffer pointed to by data is in use by the streamer
    virtual bool attachInputBuffer(const uint8_t * data, size_t len, bool final) = 0;

    // Indicate that the previously-attached buffer will be destroyed or freed.
    // Any remaining data to be used should be copied to temporary buffers
    virtual void detachInputBuffer(void) = 0;

    // Consume input data buffer until no more data is available for input, or
    // the output buffer is full (filled up to outbuf_size bytes). Return TRUE
    // if transfer was successful, or FALSE on error.
    virtual bool transformInputBuffer(void) = 0;

    size_t getAvailableOutputLength(void) { return _outbuf_used; }

    // Check if output buffer is full
    bool isOutputBufferFull(void) { return (_outbuf_used >= _outbuf_size); }

    // Transfer up to len bytes from output buffer into specified location,
    // adjusting the output buffer information to indicate consumed data.
    // Returns number of bytes actually transferred.
    virtual size_t transferOutputData(uint8_t * outdata, size_t len);

    size_t getTotalProducedOutput(void) { return _total_output; }
    virtual size_t getTotalExpectedOutput(void) = 0;

    virtual bool inputStreamEOF(void) = 0;
};