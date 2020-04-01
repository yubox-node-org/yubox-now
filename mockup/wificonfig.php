<?php
define ('MOCKUP_WIFI', '/tmp/wifiscan.json');

Header('Content-Type: application/json');

if (!isset($_SERVER['PATH_INFO'])) {
    Header('HTTP/1.1 400 Bad Request');
    print json_encode('Se requiere indicar ruta');
    exit();
}

switch ($_SERVER['PATH_INFO']) {
    case '/info':
        $output = $retval = NULL;
        exec('/usr/sbin/ifconfig eth0', $output, $retval);
        if ($retval != 0) {
            Header('HTTP/1.1 500 Internal Server Error');
            print json_encode($output);
            exit();
        }
        $regs = NULL;
        $info = array();
        foreach ($output as $s) {
            if (preg_match('/ether (\S+)/', $s, $regs)) {
                $info['MAC'] = $regs[1];
            }
        }
        print json_encode($info);
        break;
    case '/scan':
        $gen = false;
        sleep(2);   // Simular retraso en escaneo
        if (file_exists(MOCKUP_WIFI)) {
            $scan = json_decode(file_get_contents(MOCKUP_WIFI), TRUE);
        } else {
            $gen = FALSE;
            srand(time());
            $scan = array();
            for ($i = 0; $i < 20; $i++) {
                $mocknet = array(
                    'bssid'     =>  sprintf('00:11:00:11:00:%02x', $i),
                    'ssid'      =>  sprintf('RED-PRUEBA-%02d', $i),
                    'channel'   =>  rand(0, 11),
                    'rssi'      =>  rand(-100, 0),
                    'authmode'  =>  rand(0, 5),
                    'connected' =>  false,
                );
                if ($mocknet['authmode'] == 5) {
                    $mocknet['identity'] = $mocknet['password'] = NULL;
                } elseif ($mocknet['authmode'] > 0) {
                    $mocknet['psk'] = NULL;
                }
                $scan[] = $mocknet;
            }
        }
        for ($i = 0; $i < count($scan); $i++) {
            $rssi = $scan[$i]['rssi'] + rand(-5, 5);
            if ($rssi > 0) $rssi = 0;
            if ($rssi < -100) $rssi = -100;
            $scan[$i]['rssi'] = $rssi;
        }
        if ($gen) {
            $max = 0;
            for ($i = 1; $i < count($scan); $i++) {
                if ($scan[$i]['rssi'] > $scan[$max]['rssi']) $max = $i;
            }
            $scan[$max]['connected'] = true;
        }
        $json = json_encode($scan);
        file_put_contents(MOCKUP_WIFI, $json);
        print $json;
        break;
    default:
        Header('HTTP/1.1 404 Not Found');
        print json_encode('Ruta no implementada');
        exit();
}
