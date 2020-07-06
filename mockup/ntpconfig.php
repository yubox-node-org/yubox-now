<?php

Header('Content-Type: application/json');

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
    );
    print json_encode($data);
    break;
}