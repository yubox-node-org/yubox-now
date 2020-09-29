#pragma once

#include <ESPAsyncWebServer.h>
/**
 * ESTA CLASE ES PARA USO INTERNO DE YUBOX SOLAMENTE
 * -------------------------------------------------
 * 
 * Esta clase implementa una reescritura de ruta compatible con ESPAsyncWebServer para
 * poder soportar la transformación siguiente:
 * /yubox-api/XXXX/resourcelist/resourceitem --> /yubox-api/XXXX/resourcelist?YYY=resourceitem
 * Esta clase NO ES PARTE del API oficial de YUBOX, y puede estar sujeta a cambios sin
 * previo aviso.
 * 
 * NO DEPENDA DEL COMPORTAMIENTO DE ESTA BIBLIOTECA EN PROYECTOS INDEPENDIENTES.
 */

class Yubox_internal_LastParamRewrite : public AsyncWebRewrite
{
protected:
    String _prefixURL;
    String _paramBak;
    int _paramIdx;

public:
    Yubox_internal_LastParamRewrite(const char * from, const char * to);
    bool match(AsyncWebServerRequest *request) override;
};