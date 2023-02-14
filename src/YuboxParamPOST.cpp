#include "YuboxParamPOST.h"

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
    }

    if (flags & YBX_POST_VAR_TRIM) aStr.trim();

    if ((flags & YBX_POST_VAR_NONEMPTY) && aStr.length() <= 0) {
        responseMsg = "Parámetro no debe estar vacío: ";
        responseMsg += (paramDesc != NULL) ? paramDesc : paramName;
        err = true;
    }

    return err;
}
