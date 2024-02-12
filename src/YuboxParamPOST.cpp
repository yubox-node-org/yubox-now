#include "YuboxParamPOST.h"

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

bool parseParamPOST(bool prevErr, String & responseMsg, AsyncWebServerRequest * request,
    uint8_t flags, const char * paramName, const char * paramDesc, const char * fmt, void * addr)
{
    String aStr;
    bool err = parseParamPOST(prevErr, responseMsg, request, (flags | YBX_POST_VAR_TRIM), paramName, paramDesc, aStr);
    if (!err && aStr.length() > 0) {
        int n = sscanf(aStr.c_str(), fmt, addr);
        if (n <= 0) {
            responseMsg = "Formato numérico incorrecto para ";
            responseMsg += (paramDesc != NULL) ? paramDesc : paramName;
            err = true;
        } 
    }
    return err;
}

bool parseParamPOST(bool prevErr, String & responseMsg, AsyncWebServerRequest * request,
    uint8_t flags, const char * paramName, const char * paramDesc, String & aStr)
{
    if (prevErr) return prevErr;

    bool err = false;

    if (request->hasParam(paramName, true)) {
        AsyncWebParameter * p = request->getParam(paramName, true);
        aStr = p->value();
    } else if (flags & YBX_POST_VAR_REQUIRED) {
        responseMsg = "Parámetro no encontrado: ";
        responseMsg += (paramDesc != NULL) ? paramDesc : paramName;
        return true;
    } else if (flags & YBX_POST_VAR_BLANK) {
        aStr.clear();
    }

    if (flags & YBX_POST_VAR_TRIM) aStr.trim();

    if ((flags & YBX_POST_VAR_NONEMPTY) && aStr.length() <= 0) {
        responseMsg = "Parámetro no debe estar vacío: ";
        responseMsg += (paramDesc != NULL) ? paramDesc : paramName;
        err = true;
    }

    return err;
}

void sendStandardWebResponse(AsyncWebServerRequest * request, String & responseMsg, bool clientError, bool serverError)
{
    unsigned int httpCode = 200;
    if (clientError) httpCode = 400;
    if (serverError) httpCode = 500;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->setCode(httpCode);
#if ARDUINOJSON_VERSION_MAJOR <= 6
    StaticJsonDocument<JSON_OBJECT_SIZE(2)> json_doc;
#else
    JsonDocument json_doc;
#endif
    json_doc["success"] = !(clientError || serverError);
    json_doc["msg"] = responseMsg.c_str();

    serializeJson(json_doc, *response);
    request->send(response);
}