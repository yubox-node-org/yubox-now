<?php
Header('Content-Type: application/json');

if (!isset($_SERVER['PATH_INFO'])) {
    Header('HTTP/1.1 400 Bad Request');
    print json_encode(array(
        'success'   =>  FALSE,
        'msg'       =>  'Se requiere indicar ruta'
    ));
    exit();
}

switch ($_SERVER['PATH_INFO']) {
case '/tgzupload':
    if (!isset($_FILES['tgzupload'])) {
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  'No se ha especificado archivo de actualización.'
        ));
        exit();
    }

    // TODO: validar que el archivo sea realmente un tgz

    // Por ahora simular siempre éxito
    print json_encode(array(
        'success'   =>  TRUE,
        'msg'       =>  'Actualización aplicada correctamente. El equipo se reiniciará en un momento...'
    ));
    break;
case '/rollback':
    switch ($_SERVER['REQUEST_METHOD']) {
    case 'POST':
        sleep(2);
        print json_encode(array(
            'success'   =>  TRUE,
            'msg'       =>  "Firmware restaurado correctamente. El equipo se reiniciará en unos momentos."
        ));
        /*
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  "No hay firmware a restaurar, o no fue restaurado correctamente."
        ));
        */
        break;
    case 'GET':
    default:
        print json_encode(array(
            'canrollback'   =>  TRUE,
        ));
        break;
    }
	break;
default:
    Header('HTTP/1.1 404 Not Found');
    print json_encode(array(
        'success'   =>  FALSE,
        'msg'       =>  'Ruta no implementada'
    ));
    exit();
    break;
}
