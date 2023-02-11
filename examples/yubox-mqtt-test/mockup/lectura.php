<?php
function jsonflush($respuesta)
{
    $r = json_encode($respuesta);
    printflush("data: $r\n\n");
}

function printflush($s)
{
    print $s;
    flush();
}

function r()
{
    $m = getrandmax();
    return rand() / $m;
}

ignore_user_abort(TRUE);
set_time_limit(0);
ob_end_clean();

Header('Content-Type: text/event-stream');
Header('Cache-Control: no-cache, must-revalidate');
Header('X-Accel-Buffering: no');

printflush("retry: 5000\n");

srand(time());

$lectormockup_pres = 101300.0;
$lectormockup_temp = 25.0;

while (connection_status() == CONNECTION_NORMAL) {
    $lectormockup_pres += (r() - 0.5) * 50.0;
    $lectormockup_temp += (r() - 0.5) * 0.5;
    jsonflush(array(
        'ts'            =>  (int)(microtime(TRUE) * 1000),
        'pressure'      => $lectormockup_pres,
        'temperature'   => $lectormockup_temp
    ));
    sleep(3);
}
