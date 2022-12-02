#pragma once
#include "esp32-hal-log.h"

class YuboxDebugClass
{
private:
    static int _debugOutputHandler(const char *format, va_list args);

public:
    void begin(void);
    void setDebugLevel(esp_log_level_t level);
};

extern YuboxDebugClass YuboxDebugConfig;
