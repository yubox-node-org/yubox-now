<?php

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
        sleep(2);   // Simular retraso en escaneo
        if (file_exists('/tmp/wifiscan.json')) {
            $scan = json_decode(file_get_contents('/tmp/wifiscan.json'), TRUE);
        } else {
            srand(time());
            $scan = array();
            for ($i = 0; $i < 20; $i++) {
                $scan[] = array(
                    'bssid'     =>  sprintf('00:11:00:11:00:%02x', $i),
                    'ssid'      =>  sprintf('RED-PRUEBA-%02d', $i),
                    'channel'   =>  rand(0, 11),
                    'rssi'      =>  rand(-100, 0),
                    'authmode'  =>  rand(0, 5),
                );
            }
        }
        for ($i = 0; $i < count($scan); $i++) {
            $rssi = $scan[$i]['rssi'] + rand(-5, 5);
            if ($rssi > 0) $rssi = 0;
            if ($rssi < -100) $rssi = -100;
            $scan[$i]['rssi'] = $rssi;
        }
        $json = json_encode($scan);
        file_put_contents('/tmp/wifiscan.json', $json);
        print $json;
        break;
    default:
        Header('HTTP/1.1 404 Not Found');
        print json_encode('Ruta no implementada');
        exit();
}

