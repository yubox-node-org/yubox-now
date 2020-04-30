<?php
define ('MOCKUP_AUTH', '/tmp/auth.json');

Header('Content-Type: application/json');

switch ($_SERVER['REQUEST_METHOD']) {
case 'POST':
    if ($_POST['password1'] != $_POST['password2']) {
        Header('HTTP/1.1 400 Bad Request');
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  'Contraseña y confirmación no coinciden.'
        ));
        exit();
    }
    if ($_POST['password1'] == '') {
        Header('HTTP/1.1 400 Bad Request');
        print json_encode(array(
            'success'   =>  FALSE,
            'msg'       =>  'Contraseña no puede estar vacía.'
        ));
        exit();
    }
    $auth = array(
        'username'  =>  'admin',
        'password'  =>  $_POST['password1'],
    );
    $json = json_encode($auth);
    file_put_contents(MOCKUP_AUTH, $json);
    print json_encode(array(
        'success'   =>  TRUE,
        'msg'       =>  'Contraseña cambiada correctamente.'
    ));
    break;
case 'GET':
default:
    $auth = array(
        'username'  =>  'admin',
        'password'  =>  'yubox',
    );
    if (file_exists(MOCKUP_AUTH)) {
        $auth = json_decode(file_get_contents(MOCKUP_AUTH), TRUE);
    }
    print json_encode($auth);
    break;
}
