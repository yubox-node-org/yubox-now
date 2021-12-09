
function setupWiFiTab()
{
    var wifipane = getYuboxPane('wifi');
    var data = {
        'sse': null,
        'wifiscan-template':
            wifipane.find('table#wifiscan > tbody > tr.template')
            .removeClass('template')
            .detach(),
        'wifinetworks-template':
            wifipane.find('div#wifi-networks table#wifi-saved-networks > tbody > tr.template')
            .removeClass('template')
            .detach()
    }
    wifipane.data(data);

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('wifi')
    .on('shown.bs.tab', function (e) {
        yuboxWiFi_setupWiFiScanListener();
    })
    .on('hide.bs.tab', function (e) {
        var wifipane = getYuboxPane('wifi');
        if (wifipane.data('sse') != null) {
          wifipane.data('sse').close();
          wifipane.data('sse', null);
        }
    });

    // Qué hay que hacer al hacer clic en una fila que representa la red
    wifipane.find('table#wifiscan > tbody').on('click', 'tr', function(e) {
        var wifipane = getYuboxPane('wifi');
        var net = $(e.currentTarget).data();

        if (net.connected) {
            $.getJSON(yuboxAPI('wificonfig')+'/connection')
            .done(function (data) {
                var dlg_wifiinfo = wifipane.find('div#wifi-details');

                var res = evaluarIntensidadRedWifi(dlg_wifiinfo.find('tr#rssi > td > svg.wifipower'), data.rssi);
                dlg_wifiinfo.find('tr#rssi > td.text-muted').text(res.diag + ' ('+res.pwr+' %)');

                dlg_wifiinfo.find('tr#auth > td > svg.wifiauth > path').hide();
                dlg_wifiinfo.find('tr#auth > td > svg.wifiauth > path.'+(net.authmode != 0 ? 'locked' : 'unlocked')).show();
                dlg_wifiinfo.find('tr#auth > td.text-muted').text(wifiauth_desc(net.authmode));

                dlg_wifiinfo.find('tr#bssid > td.text-muted').text(net.ap[0].bssid);
                dlg_wifiinfo.find('tr#channel > td.text-muted').text(net.ap[0].channel);

                dlg_wifiinfo.find('h5#wifi-details-title').text(data.ssid);
                dlg_wifiinfo.find('input#ssid').val(data.ssid);
                dlg_wifiinfo.find('div#netinfo div#mac').text(data.mac);
                dlg_wifiinfo.find('div#netinfo div#ipv4').text(data.ipv4);
                dlg_wifiinfo.find('div#netinfo div#gateway').text(data.gateway);
                dlg_wifiinfo.find('div#netinfo div#netmask').text(data.netmask);

                var div_dns = dlg_wifiinfo.find('div#netinfo div#dns');
                div_dns.empty();
                for (var i = 0; i < data.dns.length; i++) {
                    var c = $('<div class="col"/>').text(data.dns[i]);
                    var r = $('<div class="row" />');
                    r.append(c);
                    div_dns.append(r);
                }

                dlg_wifiinfo.modal({ focus: true });
            })
            .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
        } else {
            var dlg_wificred = wifipane.find('div#wifi-credentials');

            // Preparar diálogo para red escaneada
            dlg_wificred.find('div.modal-body').removeClass('manual').addClass('scanned');

            dlg_wificred.find('h5#wifi-credentials-title').text(net.ssid);
            dlg_wificred.find('input#ssid').val(net.ssid);
            dlg_wificred.find('input#key_mgmt').val(wifiauth_desc(net.authmode));
            dlg_wificred.find('input#authmode').val(net.authmode);
            dlg_wificred.find('input#bssid').val(net.ap[0].bssid);
            dlg_wificred.find('input#channel').val(net.ap[0].channel);
            dlg_wificred.find('input[name="pin"]#N').click();

            var sel_authmode = dlg_wificred.find('select#authmode');
            if (net.authmode == 5) {
                // Autenticación WPA-ENTERPRISE
                dlg_wificred.find('div.form-group.wifi-auth-eap input#identity')
                    .val((net.identity != null) ? net.identity : '');
                dlg_wificred.find('div.form-group.wifi-auth-eap input#password')
                    .val((net.password != null) ? net.password : '');
                sel_authmode.val(5);
            } else if (net.authmode > 0) {
                // Autenticación con contraseña
                dlg_wificred.find('div.form-group.wifi-auth-psk input#psk')
                    .val((net.psk != null) ? net.psk : '');
                sel_authmode.val(4);
            } else {
                // Red sin autenticación
                sel_authmode.val(0);
            }
            sel_authmode.change();
            dlg_wificred.find('div.modal-footer button[name=connect]').text('Conectar a WIFI');
            dlg_wificred.modal({ focus: true });
        }
    });
    wifipane.find('div#wifi-credentials select#authmode').change(function () {
        var dlg_wificred = wifipane.find('div#wifi-credentials');
        var authmode = $(this).val();

        dlg_wificred.find('div.form-group.wifi-auth').hide();
        dlg_wificred.find('div.form-group.wifi-auth input').val('');
        dlg_wificred.find('button[name=connect]').prop('disabled', true);
        if (authmode == 5) {
            // Autenticación WPA-ENTERPRISE
            dlg_wificred.find('div.form-group.wifi-auth-eap').show();
            dlg_wificred.find('div.form-group.wifi-auth-eap input#password')
                .change();
        } else if (authmode > 0) {
            // Autenticación con contraseña
            dlg_wificred.find('div.form-group.wifi-auth-psk').show();
            dlg_wificred.find('div.form-group.wifi-auth-psk input#psk')
                .change();
        } else {
            // Red sin autenticación, activar directamente opción de conectar
            dlg_wificred.find('button[name=connect]').prop('disabled', false);
        }
    });

    // Qué hay que hacer al hacer clic en el botón de Redes Guardadas
    wifipane.find('button[name=networks]').click(function () {
        $.getJSON(yuboxAPI('wificonfig')+'/networks')
        .done(function (data) {
            var wifipane = getYuboxPane('wifi');
            var dlg_wifinetworks = wifipane.find('div#wifi-networks');
            var tbody_wifinetworks = dlg_wifinetworks.find('table#wifi-saved-networks > tbody');
            tbody_wifinetworks.empty();

            data.forEach(function (net) {
                var tr_wifinet = wifipane.data('wifinetworks-template').clone();
                tr_wifinet.data('ssid', net.ssid);
                tr_wifinet.children('td#ssid').text(net.ssid);
                if (net.identity != null) {
                    // Autenticación WPA-ENTERPRISE
                    tr_wifinet.children('td#auth').attr('title', 'Seguridad: ' + wifiauth_desc(5));
                    tr_wifinet.find('td#auth > svg.wifiauth > path.locked').show();
                } else if (net.psk != null) {
                    // Autenticación PSK
                    tr_wifinet.children('td#auth').attr('title', 'Seguridad: ' + wifiauth_desc(4));
                    tr_wifinet.find('td#auth > svg.wifiauth > path.locked').show();
                } else {
                    // Sin autenticación
                    tr_wifinet.children('td#auth').attr('title', 'Seguridad: ' + wifiauth_desc(0));
                    tr_wifinet.find('td#auth > svg.wifiauth > path.unlocked').show();
                }
                tbody_wifinetworks.append(tr_wifinet);
            });

            dlg_wifinetworks.modal({ focus: true });
        })
        .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
    });

    // Qué hay que hacer al hacer clic en el botón de Agregar red .
    // NOTA: hay 2 botones que se llaman igual. Uno en la interfaz principal, y el otro
    // en el diálogo de mostrar las redes guardadas. El comportamiento a continuación
    // define la acción para AMBOS botones.
    wifipane.find('button[name=addnet]').click(function () {
        var dlg_wificred = wifipane.find('div#wifi-credentials');

        // Preparar diálogo para red manual
        dlg_wificred.find('div.modal-body').removeClass('scanned').addClass('manual');

        dlg_wificred.find('h5#wifi-credentials-title').text('Agregar red');
        dlg_wificred.find('input#ssid').val('');
        dlg_wificred.find('input[name="pin"]#N').click();

        dlg_wificred.find('select#authmode').val(4);
        dlg_wificred.find('select#authmode').change();

        dlg_wificred.find('div.modal-footer button[name=connect]').text('Guardar');

        var dlg_wifinetworks = wifipane.find('div#wifi-networks');
        if (dlg_wifinetworks.is(':visible')) {
            dlg_wifinetworks.modal('hide');
        }
        dlg_wificred.modal({ focus: true });
    });

    wifipane.find('div#wifi-networks table#wifi-saved-networks > tbody').on('click', 'tr td#delete button.btn-danger', function (e) {
        var wifipane = getYuboxPane('wifi');
        var dlg_wifinetworks = wifipane.find('div#wifi-networks');
        var tr_wifinet = $(e.currentTarget).parents('tr').first();
        var ssid = tr_wifinet.data('ssid');

        if (!confirm("Presione OK para OLVIDAR las credenciales de la red "+ssid)) return;

        var st = {
            method: 'DELETE',
            url:    yuboxAPI('wificonfig')+'/networks/'+ssid
        };
        $.ajax(st)
        .done(function (data) {
            // Credenciales borradas
            tr_wifinet.detach();
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandlerDlg(dlg_wifinetworks.find('div.modal-body'), e, 2000);
        });
    });

    // Comportamiento de controles de diálogo de ingresar credenciales red
    var dlg_wificred = wifipane.find('div#wifi-credentials');
    dlg_wificred.find('div.form-group.wifi-auth-eap input')
        .change(checkValidWifiCred_EAP)
        .keypress(checkValidWifiCred_EAP)
        .blur(checkValidWifiCred_EAP);
    dlg_wificred.find('div.form-group.wifi-auth-psk input')
        .change(checkValidWifiCred_PSK)
        .keypress(checkValidWifiCred_PSK)
        .blur(checkValidWifiCred_PSK);
    dlg_wificred.find('div.modal-footer button[name=connect]').click(function () {
        var wifipane = getYuboxPane('wifi');
        var dlg_wificred = wifipane.find('div#wifi-credentials');
        var st;
        if (dlg_wificred.find('div.modal-body').hasClass('scanned')) {
            st = {
                url:    yuboxAPI('wificonfig')+'/connection',
                method: 'PUT',
                data:   {
                    ssid:       dlg_wificred.find('input#ssid').val(),
                    authmode:   parseInt(dlg_wificred.find('input#authmode').val()),
                    pin:        (dlg_wificred.find('input[name="pin"]:checked').val() == '1') ? 1 : 0
                }
            };
        } else if (dlg_wificred.find('div.modal-body').hasClass('manual')) {
            st = {
                url:    yuboxAPI('wificonfig')+'/networks',
                method: 'POST',
                data:   {
                    ssid:       dlg_wificred.find('input#ssid').val(),
                    authmode:   parseInt(dlg_wificred.find('select#authmode').val())
                }
            };
        }
        if ( st.data.authmode == 5 ) {
            // Autenticación WPA-ENTERPRISE
            st.data.identity = dlg_wificred.find('div.form-group.wifi-auth-eap input#identity').val();
            st.data.password = dlg_wificred.find('div.form-group.wifi-auth-eap input#password').val();
        } else if ( st.data.authmode > 0 ) {
            // Autenticación PSK
            st.data.psk = dlg_wificred.find('div.form-group.wifi-auth-psk input#psk').val();
        }

        if (dlg_wificred.find('div.modal-body').hasClass('scanned')) {
            // Puede ocurrir que la red ya no exista según el escaneo más reciente
            var existe = (
                wifipane.find('table#wifiscan > tbody > tr')
                .filter(function() { return ($(this).data('ssid') == st.data.ssid);  })
                .length > 0);
            if (!existe) {
                dlg_wificred.modal('hide');
                yuboxMostrarAlertText('warning', 'La red '+st.data.ssid+' ya no se encuentra disponible', 3000);
                return;
            }
        }

        // La red todavía existe en el último escaneo. Se intenta conectar.
        $.ajax(st)
        .done(function (data) {
            // Credenciales aceptadas, se espera a que se conecte
            if (dlg_wificred.find('div.modal-body').hasClass('scanned')) {
                marcarRedDesconectandose();
            }
            dlg_wificred.modal('hide');
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandlerDlg(dlg_wificred.find('div.modal-body'), e, 2000);
        });
    });

    // Comportamiento de controles de diálogo de mostrar estado de red conectada
    var dlg_wifiinfo = wifipane.find('div#wifi-details');
    dlg_wifiinfo.find('div.modal-footer button[name=forget]').click(function () {
        var wifipane = getYuboxPane('wifi');
        var dlg_wifiinfo = wifipane.find('div#wifi-details');
        var st = {
            url:    yuboxAPI('wificonfig')+'/connection',
            method: 'DELETE'
        };

        $.ajax(st)
        .done(function (data) {
            // Credenciales aceptadas, se espera a que se conecte
            marcarRedDesconectandose();
            dlg_wifiinfo.modal('hide');
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandlerDlg(dlg_wifiinfo.find('div.modal-body'), e, 2000);
        });
    });
}

function checkValidWifiCred_EAP()
{
    // Activar el botón de enviar credenciales si ambos valores son no-vacíos
    var wifipane = getYuboxPane('wifi');
    var dlg_wificred = wifipane.find('div#wifi-credentials');
    var numLlenos = dlg_wificred
        .find('div.form-group.wifi-auth-eap input')
        .filter(function() { return ($(this).val() != ''); })
        .length;
    dlg_wificred.find('button[name=connect]').prop('disabled', !(numLlenos >= 2));
}

function checkValidWifiCred_PSK()
{
    // Activar el botón de enviar credenciales si la clave es de al menos 8 caracteres
    var wifipane = getYuboxPane('wifi');
    var dlg_wificred = wifipane.find('div#wifi-credentials');
    var psk = dlg_wificred.find('div.form-group.wifi-auth-psk input#psk').val();
    dlg_wificred.find('button[name=connect]').prop('disabled', !(psk.length >= 8));
}

function yuboxWiFi_setupWiFiScanListener()
{
    if (!getYuboxNavTab('wifi').hasClass('active')) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    var wifipane = getYuboxPane('wifi');
    if (!!window.EventSource) {
        var sse = new EventSource(yuboxAPI('wificonfig')+'/netscan');
        sse.addEventListener('WiFiScanResult', function (e) {
          var data = $.parseJSON(e.data);
          yuboxWiFi_actualizarRedes(data);
        });
        sse.addEventListener('WiFiStatus', function (e) {
            var data = $.parseJSON(e.data);
            if (!data.yubox_control_wifi) {
              yuboxMostrarAlertText('warning',
                'YUBOX Now ha cedido control del WiFi a otra librería. El escaneo WiFi podría no refrescarse, o mostrar datos desactualizados.',
                5000);
            }
        });
        sse.addEventListener('error', function (e) {
          mostrarReintentoScanWifi('Se ha perdido conexión con dispositivo para siguiente escaneo');
        });
        wifipane.data('sse', sse);
    } else {
        yuboxMostrarAlertText('danger', 'Este navegador no soporta Server-Sent Events, no se puede escanear WiFi.');
    }
}

function yuboxWiFi_actualizarRedes(data)
{
    var wifipane = getYuboxPane('wifi');

    data.sort(function (a, b) {
        if (a.connected || a.connfail) return -1;
        if (b.connected || b.connfail) return 1;
        return b.rssi - a.rssi;
    });

    var tbody_wifiscan = wifipane.find('table#wifiscan > tbody');
    tbody_wifiscan.empty();
    var dlg_wifiinfo = wifipane.find('div#wifi-details');
    var ssid_visible = null;
    if (dlg_wifiinfo.is(':visible')) {
        ssid_visible = dlg_wifiinfo.find('input#ssid').val();
    }
    var max_rssi = null;
    data.forEach(function (net) {
        // Buscar en la lista existente la fila de quien tenga el SSID indicado.
        var tr_wifiscan;
        var f = tbody_wifiscan.children('tr').filter(function(idx) { return ($(this).data('ssid') == net.ssid); });
        if (f.length > 0) {
            // Se encontró SSID duplicado. Se asume que primero aparece el más potente
            tr_wifiscan = $(f[0]);
        } else {
            // Primera vez que aparece SSID en lista
            tr_wifiscan = wifipane.data('wifiscan-template').clone();
            for (var k in net) {
                if (['bssid', 'channel', 'rssi'].indexOf(k) == -1) tr_wifiscan.data(k, net[k]);
            }
            tr_wifiscan.data('ap', []);
            tbody_wifiscan.append(tr_wifiscan);
        }
        delete f;
        tr_wifiscan.data('ap').push({
            bssid:      net.bssid,
            channel:    net.channel,
            rssi:       net.rssi
        });
        var wifidata = tr_wifiscan.data();

        // Mostrar dibujo de intensidad de señal a partir de RSSI
        var res = evaluarIntensidadRedWifi(tr_wifiscan.find('td#rssi > svg.wifipower'), wifidata.ap[0].rssi);
        tr_wifiscan.children('td#rssi').attr('title', 'Intensidad de señal: '+res.pwr+' %');

        // Verificar si se está mostrando la red activa en el diálogo
        if (ssid_visible != null && ssid_visible == wifidata.ssid) {
            if (max_rssi == null || max_rssi < wifidata.ap[0].rssi) max_rssi = wifidata.ap[0].rssi;
        }

        // Mostrar estado de conexión y si hay credenciales guardadas
        tr_wifiscan.children('td#ssid').text(wifidata.ssid);
        if (wifidata.connected) {
            var sm_connlabel = $('<small class="form-text text-muted" />').text('Conectado');
            tr_wifiscan.addClass('table-success');
            tr_wifiscan.children('td#ssid').append(sm_connlabel);
        } else if (wifidata.connfail) {
            var sm_connlabel = $('<small class="form-text text-muted" />').text('Ha fallado la conexión');
            tr_wifiscan.addClass('table-danger');
            tr_wifiscan.children('td#ssid').append(sm_connlabel);
        } else if (wifidata.saved) {
            // Si hay credenciales guardadas se muestra que existen
            var sm_connlabel = $('<small class="form-text text-muted" />').text('Guardada');
            tr_wifiscan.children('td#ssid').append(sm_connlabel);
        }

        // Mostrar candado según si hay o no autenticación para la red
        tr_wifiscan.children('td#auth').attr('title',
            'Seguridad: ' + wifiauth_desc(wifidata.authmode));
        tr_wifiscan.find('td#auth > svg.wifiauth > path.'+(wifidata.authmode != 0 ? 'locked' : 'unlocked')).show();

        tr_wifiscan.data(wifidata);
    });

    // Verificar si se está mostrando la red activa en el diálogo
    if (ssid_visible != null && max_rssi != null) {
        var res = evaluarIntensidadRedWifi(dlg_wifiinfo.find('tr#rssi > td > svg.wifipower'), max_rssi);
        dlg_wifiinfo.find('tr#rssi > td.text-muted').text(res.diag + ' ('+res.pwr+' %)');
    }
}

function evaluarIntensidadRedWifi(svg_wifi, rssi)
{
    var diagnostico;
    var pwr = rssi2signalpercent(rssi);
    svg_wifi.removeClass('at-least-1bar at-least-2bars at-least-3bars at-least-4bars');
    if (pwr >= 80) {
        svg_wifi.addClass('at-least-4bars');
        diagnostico = 'Excelente';
    } else if (pwr >= 60) {
        svg_wifi.addClass('at-least-3bars');
        diagnostico = 'Buena';
    } else if (pwr >= 40) {
        svg_wifi.addClass('at-least-2bars');
        diagnostico = 'Regular';
    } else if (pwr >= 20) {
        svg_wifi.addClass('at-least-1bar');
        diagnostico = 'Débil';
    } else {
        diagnostico = 'Nula';
    }

    return { pwr: pwr, diag: diagnostico };
}

function mostrarReintentoScanWifi(msg)
{
    if (!getYuboxNavTab('wifi').hasClass('active')) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    var btn = $('<button class="btn btn-primary float-right" />').text('Reintentar');
    var al = yuboxMostrarAlert('danger',
        $('<div class="clearfix"/>')
        .append($('<span class="float-left" />').text(msg))
        .append(btn));
    btn.click(function () {
        al.remove();
        yuboxWiFi_setupWiFiScanListener();
    });
}

function wifiauth_desc(authmode)
{
    var desc_authmode = [
        '(ninguna)',
        'WEP',
        'WPA-PSK',
        'WPA2-PSK',
        'WPA-WPA2-PSK',
        'WPA2-ENTERPRISE'
    ];

    return (authmode >= 0 && authmode < desc_authmode.length)
        ? desc_authmode[authmode]
        : '(desconocida)';
}

function rssi2signalpercent(rssi)
{
    // El YUBOX ha reportado hasta ahora valores de RSSI de entre -100 hasta 0.
    // Se usa esto para calcular el porcentaje de fuerza de señal
    if (rssi > 0) rssi = 0;
    if (rssi < -100) rssi = -100;
    return rssi + 100;
}

function marcarRedDesconectandose(ssid)
{
    var wifipane = getYuboxPane('wifi');
    var tr_connected = wifipane.find('table#wifiscan > tbody > tr.table-success');
    if (tr_connected.length <= 0) return;
    if (ssid != null && tr_connected.data('ssid') != ssid) return;

    tr_connected.removeClass('table-success').addClass('table-warning');
    tr_connected.find('td#ssid > small.text-muted').text('Desconectándose');
}
