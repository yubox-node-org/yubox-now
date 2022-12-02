#include "YuboxDebugClass.h"
#include "YuboxEspNowClass.h"

typedef struct msgData
{
    char debug[200];
};

uint8_t peerAddress[] = {0x1A, 0x00, 0x00, 0x00, 0x00, 0x00};

int YuboxDebugClass::_debugOutputHandler(const char *format, va_list args)
{
    char buffer[200];
    vsnprintf(buffer, 200, format, args);

    msgData msg;
    strcpy(msg.debug, buffer);

    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.send(peerAddress, ((uint8_t *)&msg), sizeof(msg));

    return Serial.print(buffer);
}

void YuboxDebugClass::begin(void)
{
    AsyncEspNow &espNow = YuboxEspNowConfig.getEspNowClient();
    espNow.setMode(ESP_COMBO);

    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_set_vprintf(this->_debugOutputHandler);
}

void YuboxDebugClass::setDebugLevel(esp_log_level_t level)
{
    esp_log_level_set("*", level);
}

YuboxDebugClass YuboxDebugConfig;