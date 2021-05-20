#!/bin/bash

HOST=$1
TGZ=$2
CRED=admin:yubox

curl -u "$CRED" -F tgzupload=@$TGZ  http://$HOST/yubox-api/yuboxOTA/esp32/tgzupload
curl -u "$CRED" -d '' http://$HOST/yubox-api/yuboxOTA/reboot
