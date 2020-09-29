#include "Yubox_internal_LastParamRewrite.hpp"

Yubox_internal_LastParamRewrite::Yubox_internal_LastParamRewrite(const char * from, const char * to)
    : AsyncWebRewrite(from, to)
{
    // Luego de ejecutar constructor base, se tienen para:
    //  from: /srcprefix/rsrc1/{ZZZZ}
    //  to: /dstprefix/rsrc2?a=b&c=d&{UUUU}
    // ...los siguientes valores de variables internas:
    //  _from: /srcprefix/rsrc1/{ZZZZ}
    //  _toUrl: /dstprefix/rsrc2
    //  _params: a=b&c=d&{UUUU}

    /* En la plantilla fuente, por ahora se ignora por completo la clave contenida
     * entre las llaves. Esto restringe el reemplazo a un solo parámetro. Además,
     * la llave de cierre debe estar obligatoriamente al final de la plantilla. */
    _paramIdx = _from.indexOf('{');
    if (_paramIdx >= 0 && _from.endsWith("}")) {
        // Se captura prefijo de URL a usar para verificar match más adelante
        _prefixURL = _from.substring(0, _paramIdx);

        // La plantilla destino está obligada a tener el parámetro al FINAL
        // o de lo contrario cualquier otro parámetro a continuación de la
        // llave de cierre será truncado. La llave de cierre se IGNORA, así
        // como cualquier contenido entre las dos llaves.
        int i = _params.indexOf('{');
        if (i >= 0) _params = _params.substring(0, i);
    } else {
        // Plantilla no se ajusta a un parámetro entre llaves al final
        _prefixURL = _from;
    }

    /* Al indicar un match, los parámetros de URL serán reportados por la función
     * AsyncWebRewrite::params(). Luego del matcheo los parámetros deben incluir
     * la porción extraída del URL examinado. */
    _paramBak = _params;
}

bool Yubox_internal_LastParamRewrite::match(AsyncWebServerRequest *request)
{
    // ¿Nos interesa este URL en particular?
    if (!request->url().startsWith(_prefixURL)) return false;

    // Recortar la porción del URL al final y adjuntarla como parámetro nuevo
    _params = _paramBak;
    if (_paramIdx >= 0) _params += request->url().substring(_paramIdx);

    return true;
}