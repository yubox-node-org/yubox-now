<?php
define ('MOCKUP_MQTT', '/tmp/yuboxmqtt.json');

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
case '/tls_servercert':
case '/tls_clientcert':
    handle_cert(substr($path_info, 1));
    break;
}

function load_conf()
{
    $info = [
        'want2connect'          =>  TRUE,
        'connected'             =>  FALSE,
        'disconnected_reason'   =>  0,      // Desconectado a nivel de red
        'clientid'              =>  'YUBOX-MOCKUP',

        'host'                  =>  NULL,
        'user'                  =>  NULL,
        'pass'                  =>  NULL,
        'port'                  =>  1883,

        'tls_capable'           =>  TRUE,
        'tls_verifylevel'       =>  0,
        'tls_servercert'        =>  FALSE,
        'tls_clientcert'        =>  FALSE,
    ];
    if (file_exists(MOCKUP_MQTT)) {
        $info = json_decode(file_get_contents(MOCKUP_MQTT), TRUE);
    }

    return $info;
}

function handle_conf()
{
    $info = load_conf();

    switch ($_SERVER['REQUEST_METHOD']) {
    case 'GET':
        print json_encode($info);
        break;
    case 'POST':
        $clientError = $serverError = FALSE;
        $responseMsg = '';

        if (!$clientError && !isset($_POST['host'])) {
            $clientError = true;
            $responseMsg = "Se requiere host de broker MQTT";;
        }
        if (!$clientError) {
            $info['host'] = trim(strtolower($_POST['host']));
            if (strlen($info['host']) <= 0) {
                $clientError = true;
                $responseMsg = "Se requiere host de broker MQTT";;
            }
            // TODO: validar hostname válido
        }
        if (!$clientError) {
            $info['user'] = (isset($_POST['user']) && trim($_POST['user']) != '') ? trim($_POST['user']) : NULL;
            $info['pass'] = (isset($_POST['pass']) && trim($_POST['pass']) != '') ? trim($_POST['pass']) : NULL;
            if (is_null($info['user'])) {
                $info['pass'] = NULL;
            } elseif (is_null($info['pass'])) {
                $clientError = true;
                $responseMsg = "Credenciales requieren contraseña no vacía";
            }
        }

        if (!$clientError && isset($_POST['port'])) {
            if (!ctype_digit($_POST['port'])) {
                $clientError = true;
                $responseMsg = "Formato numérico incorrecto para port";
            } else {
                $info['port'] = (int)$_POST['port'];
            }
        }

        if ($info['tls_capable']) {
            if (!$clientError && isset($_POST['tls_verifylevel'])) {
                if (!ctype_digit($_POST['tls_verifylevel'])) {
                    $clientError = true;
                    $responseMsg = "Formato numérico incorrecto para tls_verifylevel";
                } else {
                    $info['tls_verifylevel'] = (int)$_POST['tls_verifylevel'];
                    if ($info['tls_verifylevel'] > 3) $info['tls_verifylevel'] = 3;
                }
            }
        }

        $resp = [
            'success'   =>  !($clientError || $serverError),
            'msg'       =>  ($clientError || $serverError) ? $responseMsg : "Parámetros actualizados correctamente",
        ];
        if ($serverError) {
            Header('HTTP/1.1 500 Internal Server Error');
        } elseif ($clientError) {
            Header('HTTP/1.1 400 Bad Request');
        } else {
            $info['connected'] = (strlen($info['host']) > 0);
            file_put_contents(MOCKUP_MQTT, json_encode($info));
        }
        print json_encode($resp);
        break;
    default:
        Header('HTTP/1.1 405 Method Not Allowed');
        Header('Allow: GET, POST');
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  'Unimplemented request method'
        ));
        exit();
        break;
    }
}

function handle_cert($cert)
{
    $info = load_conf();

    switch ($_SERVER['REQUEST_METHOD']) {
    case 'POST':
        $clientError = $serverError = FALSE;
        $responseMsg = '';

        if (isset($_FILES['tls_clientcert']) && isset($_FILES['tls_clientkey'])) {
            $info['tls_clientcert'] = TRUE;
        } elseif (isset($_FILES['tls_servercert'])) {
            $info['tls_servercert'] = TRUE;
        } else {
            $clientError = TRUE;
            $responseMsg = "No se ha especificado archivo de certificado.";
        }

        $resp = [
            'success'   =>  !($clientError || $serverError),
            'msg'       =>  ($clientError || $serverError) ? $responseMsg : "Certificado actualizado correctamente.",
        ];
        if ($serverError) {
            Header('HTTP/1.1 500 Internal Server Error');
        } elseif ($clientError) {
            Header('HTTP/1.1 400 Bad Request');
        } else {
            file_put_contents(MOCKUP_MQTT, json_encode($info));
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