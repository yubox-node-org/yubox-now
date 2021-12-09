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

$firmwares = array(
    array(
        'tag'       =>  'esp32',
        'desc'      =>  'YUBOX ESP32 Firmware',
        'tgzupload' =>  '/yubox-mockup/yuboxOTA.php/esp32/tgzupload',
        'rollback'  =>  '/yubox-mockup/yuboxOTA.php/esp32/rollback',
    ),
    array(
        'tag'       =>  'gato',
        'desc'      =>  'Firmware para gatos',
        'tgzupload' =>  '/yubox-mockup/yuboxOTA.php/gato/tgzupload',
        'rollback'  =>  '/yubox-mockup/yuboxOTA.php/gato/rollback',
    ),
    array(
        'tag'       =>  'nuke',
        'desc'      =>  'Firmware para central nuclear',
        'tgzupload' =>  '/yubox-mockup/yuboxOTA.php/nuke/tgzupload',
        'rollback'  =>  '/yubox-mockup/yuboxOTA.php/nuke/rollback',
    ),
);

$path_info = $_SERVER['PATH_INFO'];
$path_components = explode('/', $path_info);
$fw = NULL;
if (count($path_components) == 3) {
    $fw = $path_components[1];
    $path_info = '/'.$path_components[2];
}

switch ($path_info) {
case '/firmwarelist.json':
    print json_encode($firmwares);
    break;
case '/reboot':
    $r = array(
        /*
        'success'   =>  FALSE,
        'msg'       =>  'Reinicio rechazado porque componente XYZ'
        */
        'success'   =>  TRUE,
        'msg'       =>  'El equipo se reiniciará en unos momentos.'
    );
    print json_encode($r);
    break;
case '/tgzupload':
    if (!isset($_FILES['tgzupload'])) {
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  $fw.': No se ha especificado archivo de actualización.'
        ));
        exit();
    }

    // TODO: validar que el archivo sea realmente un tgz

    // Por ahora simular siempre éxito
    print json_encode(array(
        'success'   =>  TRUE,
        'msg'       =>  $fw.': Actualización aplicada correctamente. El equipo se reiniciará en un momento...'
    ));
    break;
case '/rollback':
    switch ($_SERVER['REQUEST_METHOD']) {
    case 'POST':
        sleep(2);
        print json_encode(array(
            'success'   =>  TRUE,
            'msg'       =>  "$fw: Firmware restaurado correctamente. El equipo se reiniciará en unos momentos."
        ));
        /*
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  "$fw: No hay firmware a restaurar, o no fue restaurado correctamente."
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
