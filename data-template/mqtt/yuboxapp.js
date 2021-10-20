function setupMqttTab()
{
    var mqttpane = getYuboxPane('mqtt');

    mqttpane.find('input[name=mqttauth]').click(function() {
        var mqttpane = getYuboxPane('mqtt');

        var nstat = mqttpane.find('input[name=mqttauth]:checked').val();
        if (nstat == 'on') {
            mqttpane.find('form div.mqttauth').show();
            mqttpane.find('form div.mqttauth input').prop('required', true);
        } else {
            mqttpane.find('form div.mqttauth').hide();
            mqttpane.find('form div.mqttauth input').prop('required', false);
        }
    });
    mqttpane.find('button[name=apply]').click(function () {
        var mqttpane = getYuboxPane('mqtt');

        var postData = {
            host:           mqttpane.find('input#mqtthost').val(),
            port:           mqttpane.find('input#mqttport').val(),
            user:           null,
            pass:           null
        };
        if (mqttpane.find('input[name=mqttauth]:checked').val() == 'on') {
            postData.user = mqttpane.find('input#mqttuser').val();
            postData.pass = mqttpane.find('input#mqttpass').val();
        }
        $.post(yuboxAPI('mqtt')+'/conf.json', postData)
        .done(function (r) {
            if (r.success) {
                // Recargar los datos recién guardados del dispositivo
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar que la conexión
                // realmente se puede realizar.
                setTimeout(yuboxLoadMqttConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        })
        .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
    });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('mqtt')
    .on('shown.bs.tab', function (e) {
        yuboxLoadMqttConfig();
    });
}

function yuboxLoadMqttConfig()
{
    var mqttpane = getYuboxPane('mqtt');
    mqttpane.find('form span#mqtt_connstatus')
        .removeClass('badge-success badge-danger')
        .addClass('badge-secondary')
        .text('(consultando)');
    mqttpane.find('form span#mqtt_disconnected_reason').text('...');

    $.get(yuboxAPI('mqtt')+'/conf.json')
    .done(function (data) {
        mqttpane.find('form span#mqtt_clientid').text(data.clientid);
        var span_tls_capable = mqttpane.find('form span#tls_capable');
        span_tls_capable.removeClass('badge-success badge-secondary');
        if (data.tls_capable) {
            span_tls_capable.addClass('badge-success').text('PRESENTE');
        } else {
            span_tls_capable.addClass('badge-secondary').text('AUSENTE');
        }

        var span_connstatus = mqttpane.find('form span#mqtt_connstatus');
        var span_reason = mqttpane.find('form span#mqtt_disconnected_reason');
        span_connstatus.removeClass('badge-danger badge-success badge-secondary');
        span_reason.text('');
        if (!data.want2connect) {
            span_connstatus.addClass('badge-secondary').text('NO REQUERIDO');
        } else if (data.connected) {
            span_connstatus.addClass('badge-success').text('CONECTADO');
        } else {
            var reason = '???';
            span_connstatus.addClass('badge-danger').text('DESCONECTADO');
            switch (data.disconnected_reason) {
            case 0:
                reason = 'Desconectado a nivel de red';
                break;
            case 1:
                reason = 'Versión de protocolo MQTT incompatible';
                break;
            case 2:
                reason = 'Identificador rechazado';
                break;
            case 3:
                reason = 'Servidor no disponible';
                break;
            case 4:
                reason = 'Credenciales mal formadas';
                break;
            case 5:
                reason = 'No autorizado';
                break;
            case 6:
                reason = 'No hay memoria suficiente';
                break;
            case 7:
                reason = 'Huella TLS incorrecta';
                break;
            }
            span_reason.text(reason);
        }

        mqttpane.find('form input#mqtthost').val(data.host);
        mqttpane.find('form input#mqttport').val(data.port);
        if (data.user != null) {
            mqttpane.find('form input#mqttuser').val(data.user);
            mqttpane.find('form input#mqttpass').val(data.pass);
            mqttpane.find('input[name=mqttauth]#on').click();
        } else {
            mqttpane.find('form input#mqttuser').val('');
            mqttpane.find('form input#mqttpass').val('');
            mqttpane.find('input[name=mqttauth]#off').click();
        }
    })
    .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
}
