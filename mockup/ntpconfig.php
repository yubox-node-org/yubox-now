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

$path_info = $_SERVER['PATH_INFO'];
$path_components = explode('/', $path_info);

switch ($path_info) {
case '/conf.json':
    handle_conf();
    break;
case '/rtc.json':
    handle_rtc();
    break;
default:
    Header('HTTP/1.1 404 Not Found');
    print json_encode(array(
        'success'   =>  FALSE,
        'msg'       =>  'El recurso indicado no existe o no ha sido implementado'
    ));
    exit();
    break;
}

function handle_conf()
{
    switch ($_SERVER['REQUEST_METHOD']) {
    case 'POST':
        print json_encode(array(
            'success'   =>  TRUE,
            'msg'       =>  'Servidor NTP configurado correctamente.'
        ));
        break;
    case 'GET':
    default:
        $data = array(
            'ntpsync'   =>  true,
            'ntphost'   =>  'pool.ntp.org',
            'ntptz'     =>  -5 * 3600 - 30 * 60,
            'utctime'   =>  time(),
            'ntpsync_msec'  =>  45 * 1000,  // Debería representar valor de millis() al momento de sync NTP
        );
        print json_encode($data);
        break;
    }
}

function handle_rtc()
{
    switch ($_SERVER['REQUEST_METHOD']) {
    case 'POST':

        $clientError = $serverError = FALSE;
        $responseMsg = '';

        if (!isset($_POST['utctime_ms'])) {
            $clientError = TRUE;
            $responseMsg = "Se requiere timestamp de hora de navegador";
        } elseif (!ctype_digit($_POST['utctime_ms'])) {
            $clientError = TRUE;
            $responseMsg = "Valor de timestamp de hora no es válido";
        } else {
            $responseMsg = "Hora de sistema actualizada correctamente";
        }

        $resp = [
            'success'   =>  !($clientError || $serverError),
            'msg'       =>  $responseMsg,
        ];
        if ($serverError) {
            Header('HTTP/1.1 500 Internal Server Error');
        } elseif ($clientError) {
            Header('HTTP/1.1 400 Bad Request');
        }
        print json_encode($resp);
        break;
    default:
        Header('HTTP/1.1 405 Method Not Allowed');
        Header('Allow: POST');
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  'Unimplemented request method'
        ));
        exit();
        break;
    }
}