#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Bitmap de condiciones que debe cumplir la variable parseada
#define YBX_POST_VAR_REQUIRED   0x01    // Variable debe de constar como presente
#define YBX_POST_VAR_NONEMPTY   0x02    // Variable debe de ser no vacía
#define YBX_POST_VAR_TRIM       0x04    // Variable se recortará espacios vacíos usando trim() antes de evaluar
#define YBX_POST_VAR_BLANK      0x08    // En lugar de preservar valor si no-presente, cadena debe anularse.

bool parseParamPOST(bool, String &, AsyncWebServerRequest *, uint8_t, const char *, const char *, const char *, void *);
bool parseParamPOST(bool, String &, AsyncWebServerRequest *, uint8_t, const char *, const char *, String &);

#define YBX_ASSIGN_NUM_FROM_POST(TAG, DESC, FMT, REQ, VAR)     \
    clientError = parseParamPOST(clientError, responseMsg, request, (REQ), #TAG, DESC, FMT, &(VAR));

#define YBX_ASSIGN_STR_FROM_POST(TAG, DESC, REQ, VAR)          \
    clientError = parseParamPOST(clientError, responseMsg, request, (REQ), #TAG, DESC, VAR);
