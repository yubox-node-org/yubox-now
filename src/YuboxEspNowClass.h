#pragma once
#include "AsyncEspNow.h"

class YuboxEspNowClass
{
private:
    AsyncEspNow _espnow;
    
public:
    // Begin
    void begin(void){_espnow.setMode(ESP_MASTER);}

    // Obtener referencia al EspNow que se maneja, para agregar callbacks
    AsyncEspNow &getEspNowClient(void) { return _espnow; }
};

YuboxEspNowClass YuboxEspNowConfig;
