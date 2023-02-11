function setupMqttTab()
{
    const mqttpane = getYuboxPane('mqtt', true);

    let mqttauth_click_cb = function() {
        const div_mqttauth = mqttpane.querySelector('form div.mqttauth');
        const div_mqttauth_inputs = mqttpane.querySelectorAll('form div.mqttauth input');
        const nstat = mqttpane.querySelector('input[name=mqttauth]:checked').value;
        if (nstat == 'on') {
            div_mqttauth.style.display = '';
            div_mqttauth_inputs.forEach((elem) => { elem.required = true; });
        } else {
            div_mqttauth.style.display = 'none';
            div_mqttauth_inputs.forEach((elem) => { elem.required = false; });
        }
    };
    mqttpane.querySelectorAll('input[name=mqttauth]')
        .forEach(el => { el.addEventListener('click', mqttauth_click_cb) });

    let mqttws_click_cb = function() {
        const div_mqttws = mqttpane.querySelector('form div.mqtt-ws');
        const div_mqttws_inputs = mqttpane.querySelectorAll('form div.mqtt-ws input');
        const nstat = mqttpane.querySelector('input[name=mqttws]:checked').value;
        if (nstat == '1') {
            div_mqttws.style.display = '';
            div_mqttws_inputs.forEach((elem) => { elem.required = true; });
        } else {
            div_mqttws.style.display = 'none';
            div_mqttws_inputs.forEach((elem) => { elem.required = false; });
        }
    };
    mqttpane.querySelectorAll('input[name=mqttws]')
        .forEach(el => { el.addEventListener('click', mqttws_click_cb) });

    mqttpane.querySelector('button[name=apply]').addEventListener('click', function () {
        var postData = {
            host:           mqttpane.querySelector('input#mqtthost').value,
            port:           mqttpane.querySelector('input#mqttport').value,
            topic:          mqttpane.querySelector('input#topic').value,
            tls_verifylevel:mqttpane.querySelector('input[name=tls_verifylevel]:checked').value,
            ws:             mqttpane.querySelector('input[name=mqttws]:checked').value,
            wsuri:          mqttpane.querySelector('input#wsuri').value,
            mqttmsec:       parseInt(mqttpane.querySelector('form input#mqttsec').value) * 1000
        };
        if (mqttpane.querySelector('input[name=mqttauth]:checked').value == 'on') {
            postData.user = mqttpane.querySelector('input#mqttuser').value;
            postData.pass = mqttpane.querySelector('input#mqttpass').value;
        }
        yuboxFetch('mqtt', 'conf.json', postData)
        .then((r) => {
            if (r.success) {
                // Recargar los datos recién guardados del dispositivo
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar que la conexión
                // realmente se puede realizar.
                setTimeout(yuboxLoadMqttConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });
    mqttpane.querySelector('button[name=forget]').addEventListener('click', function () {
        if (!confirm('Presione OK para OLVIDAR las credenciales MQTT y DESCONECTAR el dispositivo de cualquier conexión MQTT activa.'))
            return;

        yuboxFetchMethod('DELETE', 'mqtt', 'conf.json')
        .then((r) => {
            // Credenciales borradas
            if (r == null) {
                setTimeout(yuboxLoadMqttConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });
    mqttpane.querySelector('button[name=tls_servercert_upload]').addEventListener('click', function() {
        yuboxUploadMQTTCerts('tls_servercert', ['tls_servercert']);
    });
    mqttpane.querySelector('button[name=tls_clientcert_upload]').addEventListener('click', function() {
        yuboxUploadMQTTCerts('tls_clientcert', ['tls_clientcert', 'tls_clientkey']);
    });

    // https://getbootstrap.com/docs/5.1/components/navs-tabs/#events
    getYuboxNavTab('mqtt', true)
    .addEventListener(yuboxBSEvt('shown.bs.tab'), function (e) {
        yuboxLoadMqttConfig();
    });
}

function yuboxLoadMqttConfig_connstatus(badgeclass, msg)
{
    const mqttpane = getYuboxPane('mqtt', true);
    const span_connstatus = mqttpane.querySelector('form span#mqtt_connstatus');

    span_connstatus.classList.remove('bg-danger', 'bg-success', 'bg-secondary');
    span_connstatus.classList.add(badgeclass);
    span_connstatus.textContent = msg;
}

function yuboxLoadMqttConfig()
{
    const mqttpane = getYuboxPane('mqtt', true);
    const span_reason = mqttpane.querySelector('form span#mqtt_disconnected_reason');
    span_reason.textContent = '...';

    yuboxLoadMqttConfig_connstatus('bg-secondary', '(consultando)');
    yuboxFetch('mqtt', 'conf.json')
    .then((data) => {
        mqttpane.querySelector('form span#mqtt_clientid').textContent = data.clientid;
        const span_tls_capable = mqttpane.querySelector('form span#tls_capable');
        span_tls_capable.classList.remove('bg-success', 'bg-secondary');
        if (data.tls_capable) {
            span_tls_capable.classList.add('bg-success')
            span_tls_capable.textContent = 'PRESENTE';
        } else {
            span_tls_capable.classList.add('bg-secondary')
            span_tls_capable.textContent = 'AUSENTE';
        }

        span_reason.textContent = '';
        if (data.connected) {
            yuboxLoadMqttConfig_connstatus('bg-success', 'CONECTADO');
        } else if (!data.want2connect) {
            yuboxLoadMqttConfig_connstatus('bg-secondary', 'NO REQUERIDO');
        } else {
            yuboxLoadMqttConfig_connstatus('bg-danger', 'DESCONECTADO');
            const reasonmsg = [
                'Desconectado a nivel de red',
                'Versión de protocolo MQTT incompatible',
                'Identificador rechazado',
                'Servidor no disponible',
                'Credenciales mal formadas',
                'No autorizado',
                'No hay memoria suficiente',
                'Huella TLS incorrecta'
            ];
            span_reason.textContent = (data.disconnected_reason >= reasonmsg.length) ? '???' : reasonmsg[data.disconnected_reason];
        }

        mqttpane.querySelector('form input#mqtthost').value = data.host;
        mqttpane.querySelector('form input#mqttport').value = data.port;
        if (data.user != null) {
            mqttpane.querySelector('form input#mqttuser').value = data.user;
            mqttpane.querySelector('form input#mqttpass').value = data.pass;
            mqttpane.querySelector('input[name=mqttauth]#on').click();
        } else {
            mqttpane.querySelector('form input#mqttuser').value = '';
            mqttpane.querySelector('form input#mqttpass').value = '';
            mqttpane.querySelector('input[name=mqttauth]#off').click();
        }
        mqttpane.querySelector('form input#wsuri').value = data.wsuri;
        mqttpane.querySelector('input[name=mqttws]#'+(data.ws ? 'on' : 'off')).click();
        mqttpane.querySelector('form input#mqttsec').value = (data.mqttmsec ?? 30000) / 1000;

        // Soporte para tópico personalizado
        const div_mqtt_topic = mqttpane.querySelector('div.topic-capable');
        div_mqtt_topic.style.display = data.topic_capable ? '' : 'none';
        div_mqtt_topic.querySelector('input#topic').value = (data.topic == null) ? '' : data.topic;

        const div_mqtt_tls = mqttpane.querySelectorAll('div.mqtt-tls');
        // Nivel de soporte TLS deseado
        mqttpane.querySelector('input[name=tls_verifylevel]#tls_verifylevel_'+data.tls_verifylevel).click();

        // Archivos de certificados presentes en servidor
        const span_tls_servercert_present = mqttpane.querySelector('form span#tls_servercert_present');
        span_tls_servercert_present.classList.remove('bg-warning', 'bg-success');
        span_tls_servercert_present.classList.add(data.tls_servercert ? 'bg-success' : 'bg-warning');
        span_tls_servercert_present.textContent = (data.tls_servercert ? 'SÍ' : 'NO');
        const span_tls_clientcert_present = mqttpane.querySelector('form span#tls_clientcert_present');
        span_tls_clientcert_present.classList.remove('bg-warning', 'bg-success')
        span_tls_clientcert_present.classList.add(data.tls_clientcert ? 'bg-success' : 'bg-warning');
        span_tls_clientcert_present.textContent = (data.tls_clientcert ? 'SÍ' : 'NO');
        if (data.tls_capable) {
            // Hay soporte TLS
            div_mqtt_tls.forEach((div) => { div.style.display = ''; });
        } else {
            // No hay soporte TLS
            div_mqtt_tls.forEach((div) => { div.style.display = 'none'; });
        }
    }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
}

function yuboxUploadMQTTCerts(route_upload, filelist)
{
    const mqttpane = getYuboxPane('mqtt', true);

    if (typeof FormData == 'undefined') {
        yuboxMostrarAlertText('danger', 'Este navegador no soporta FormData para subida de datos. Actualice su navegador.', 2000);
        return;
    }
    var postData = new FormData();

    for (let k of filelist) {
        let fi = mqttpane.querySelector('input[type=file]#'+k);
        if (fi.files.length <= 0) {
            yuboxMostrarAlertText('danger', 'Es necesario elegir un archivo de certificado: '+k, 2000);
            return;
        }
        postData.append(k, fi.files[0]);
    }
    yuboxFetch('mqtt', route_upload, postData)
    .then((data) => {
        if (data.success) {
            // Al aplicar actualización debería recargarse más tarde
            yuboxMostrarAlertText('success', data.msg, 5000);
            setTimeout(function () {
                yuboxLoadMqttConfig();
            }, 5 * 1000);
        } else {
            yuboxMostrarAlertText('danger', data.msg, 6000);
        }
    }, (e) => { yuboxStdAjaxFailHandler(e, 5000); });
}