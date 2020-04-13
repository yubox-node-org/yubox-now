$(document).ready(function () {
    setupWiFiTab();

    // Mostrar el tab preparado por omisión como activo
    $('ul#yuboxMainTab a.set-active').removeClass('set-active').tab('show');
});

function setupWiFiTab()
{
    var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
    var data = {
        'wifiscan-timer': null,
        'wifiscan-template':
            wifipane.find('table#wifiscan > tbody > tr.template')
            .removeClass('template')
            .detach()
    }
    wifipane.data(data);

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    $('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').on('shown.bs.tab', function (e) {
        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        if (wifipane.data('wifiscan-timer') == null) wifipane.data('wifiscan-timer', setTimeout(scanWifiNetworks, 1));
    });
    $('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').on('hide.bs.tab', function (e) {
        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        if (wifipane.data('wifiscan-timer') != null) {
            clearTimeout(wifipane.data('wifiscan-timer'));
            wifipane.data('wifiscan-timer', null)
        }
    });

    // Qué hay que hacer al hacer clic en una fila que representa la red
    $('div#yuboxMainTabContent > div.tab-pane#wifi table#wifiscan > tbody').on('click', 'tr', function(e) {
        var net = $(e.currentTarget).data();

        if (net.connected) {
            $.getJSON(yuboxAPI('wificonfig')+'/connection')
            .done(function (data) {
                var dlg_wifiinfo = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-details');

                var res = evaluarIntensidadRedWifi(dlg_wifiinfo.find('tr#rssi > td > svg.wifipower'), data.rssi);
                dlg_wifiinfo.find('tr#rssi > td.text-muted').text(res.diag + ' ('+res.pwr+' %)');

                dlg_wifiinfo.find('tr#auth > td > svg.wifiauth > path').hide();
                dlg_wifiinfo.find('tr#auth > td > svg.wifiauth > path.'+(net.authmode != 0 ? 'locked' : 'unlocked')).show();
                dlg_wifiinfo.find('tr#auth > td.text-muted').text(wifiauth_desc(net.authmode));

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
            .fail(function (e) {
                var msg;
                if (e.status == 0) {
                    msg = 'Fallo al contactar dispositivo';
                } else if (e.responseJSON == undefined) {
                    msg = 'Tipo de dato no esperado en respuesta';
                } else {
                    msg = e.responseJSON.msg;
                }
                yuboxMostrarAlertText('danger', msg, 2000);
            });
        } else {
            var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
            dlg_wificred.find('h5#wifi-credentials-title').text(net.ssid);
            dlg_wificred.find('input#ssid').val(net.ssid);
            dlg_wificred.find('input#key_mgmt').val(wifiauth_desc(net.authmode));
            dlg_wificred.find('input#authmode').val(net.authmode);
            dlg_wificred.find('div.form-group.wifi-auth').hide();
            dlg_wificred.find('div.form-group.wifi-auth input').val('');
            dlg_wificred.find('button[name=connect]').prop('disabled', true);
            if (net.authmode == 5) {
                // Autenticación WPA-ENTERPRISE
                dlg_wificred.find('div.form-group.wifi-auth-eap').show();
                dlg_wificred.find('div.form-group.wifi-auth-eap input#identity')
                    .val((net.identity != null) ? net.identity : '');
                dlg_wificred.find('div.form-group.wifi-auth-eap input#password')
                    .val((net.password != null) ? net.password : '')
                    .change();
            } else if (net.authmode > 0) {
                // Autenticación con contraseña
                dlg_wificred.find('div.form-group.wifi-auth-psk').show();
                dlg_wificred.find('div.form-group.wifi-auth-psk input#psk')
                    .val((net.psk != null) ? net.psk : '')
                    .change();
            } else {
                // Red sin autenticación, activar directamente opción de conectar
                dlg_wificred.find('button[name=connect]').prop('disabled', false);
            }
            dlg_wificred.modal({ focus: true });
        }
    });

    // Comportamiento de controles de diálogo de ingresar credenciales red
    var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
    dlg_wificred.find('div.form-group.wifi-auth-eap input')
        .change(checkValidWifiCred_EAP)
        .keypress(checkValidWifiCred_EAP)
        .blur(checkValidWifiCred_EAP);
    dlg_wificred.find('div.form-group.wifi-auth-psk input')
        .change(checkValidWifiCred_PSK)
        .keypress(checkValidWifiCred_PSK)
        .blur(checkValidWifiCred_PSK);
    dlg_wificred.find('div.modal-footer button[name=connect]').click(function () {
        var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
        var st = {
            url:    yuboxAPI('wificonfig')+'/connection',
            method: 'PUT',
            data:   {
                ssid:       dlg_wificred.find('input#ssid').val(),
                authmode:   parseInt(dlg_wificred.find('input#authmode').val())
            }
        };
        if ( st.data.authmode == 5 ) {
            // Autenticación WPA-ENTERPRISE
            st.data.identity = dlg_wificred.find('div.form-group.wifi-auth-eap input#identity').val();
            st.data.password = dlg_wificred.find('div.form-group.wifi-auth-eap input#password').val();
        } else if ( st.data.authmode > 0 ) {
            // Autenticación PSK
            st.data.psk = dlg_wificred.find('div.form-group.wifi-auth-psk input#psk').val();
        }

        // Puede ocurrir que la red ya no exista según el escaneo más reciente
        var existe = (
            $('div#yuboxMainTabContent > div.tab-pane#wifi table#wifiscan > tbody > tr')
            .filter(function() { return ($(this).data('ssid') == st.data.ssid);  })
            .length > 0);
        if (!existe) {
            dlg_wificred.modal('hide');
            yuboxMostrarAlertText('warning', 'La red '+st.data.ssid+' ya no se encuentra disponible', 3000);
            return;
        }

        // La red todavía existe en el último escaneo. Se intenta conectar.
        $.ajax(st)
        .done(function (data) {
            // Credenciales aceptadas, se espera a que se conecte
            marcarRedDesconectandose();
            dlg_wificred.modal('hide');
        })
        .fail(function (e) {
            var msg;
            if (e.status == 0) {
                msg = 'Fallo al contactar dispositivo';
            } else if (e.responseJSON == undefined) {
                msg = 'Tipo de dato no esperado en respuesta';
            } else {
                msg = e.responseJSON.msg;
            }
            yuboxDlgMostrarAlertText(dlg_wificred.find('div.modal-body'), 'danger', msg, 2000);
        });
    });

    // Comportamiento de controles de diálogo de mostrar estado de red conectada
    var dlg_wifiinfo = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-details');
    dlg_wifiinfo.find('div.modal-footer button[name=forget]').click(function () {
        var dlg_wifiinfo = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-details');
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
            var msg;
            if (e.status == 0) {
                msg = 'Fallo al contactar dispositivo';
            } else if (e.responseJSON == undefined) {
                msg = 'Tipo de dato no esperado en respuesta';
            } else {
                msg = e.responseJSON.msg;
            }
            yuboxDlgMostrarAlertText(dlg_wifiinfo.find('div.modal-body'), 'danger', msg, 2000);
        });
    });
}

function checkValidWifiCred_EAP()
{
    // Activar el botón de enviar credenciales si ambos valores son no-vacíos
    var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
    var numLlenos = dlg_wificred
        .find('div.form-group.wifi-auth-eap input')
        .filter(function() { return ($(this).val() != ''); })
        .length;
    dlg_wificred.find('button[name=connect]').prop('disabled', !(numLlenos >= 2));
}

function checkValidWifiCred_PSK()
{
    // Activar el botón de enviar credenciales si la clave es de al menos 8 caracteres
    var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
    var psk = dlg_wificred.find('div.form-group.wifi-auth-psk input#psk').val();
    dlg_wificred.find('button[name=connect]').prop('disabled', !(psk.length >= 8));
}

function scanWifiNetworks()
{
    if (!$('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').hasClass('active')) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    $.get(yuboxAPI('wificonfig')+'/networks')
    .done(function (data) {
        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        wifipane.data('wifiscan-timer', null);

        data.sort(function (a, b) {
            if (a.connected || a.connfail) return -1;
            if (b.connected || b.connfail) return 1;
            return b.rssi - a.rssi;
        });

        var tbody_wifiscan = wifipane.find('table#wifiscan > tbody');
        tbody_wifiscan.empty();
        var dlg_wifiinfo = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-details');
        var ssid_visible = null;
        if (dlg_wifiinfo.is(':visible')) {
            ssid_visible = dlg_wifiinfo.find('input#ssid').val();
        }
        data.forEach(function (net) {
            var tr_wifiscan = wifipane.data('wifiscan-template').clone();

            // Mostrar dibujo de intensidad de señal a partir de RSSI
            var res = evaluarIntensidadRedWifi(tr_wifiscan.find('td#rssi > svg.wifipower'), net.rssi);
            tr_wifiscan.children('td#rssi').attr('title', 'Intensidad de señal: '+res.pwr+' %');

            // Verificar si se está mostrando la red activa en el diálogo
            if (ssid_visible != null && ssid_visible == net.ssid) {
                var res = evaluarIntensidadRedWifi(dlg_wifiinfo.find('tr#rssi > td > svg.wifipower'), net.rssi);
                dlg_wifiinfo.find('tr#rssi > td.text-muted').text(res.diag + ' ('+res.pwr+' %)');
            }


            // Mostrar candado según si hay o no autenticación para la red
            tr_wifiscan.children('td#ssid').text(net.ssid);
            if (net.connected) {
                var sm_connlabel = $('<small class="form-text text-muted" />').text('Conectado');
                tr_wifiscan.addClass('table-success');
                tr_wifiscan.children('td#ssid').append(sm_connlabel);
            } else if (net.connfail) {
                var sm_connlabel = $('<small class="form-text text-muted" />').text('Ha fallado la conexión');
                tr_wifiscan.addClass('table-danger');
                tr_wifiscan.children('td#ssid').append(sm_connlabel);
            }
            tr_wifiscan.children('td#auth').attr('title',
                'Seguridad: ' + wifiauth_desc(net.authmode));
            tr_wifiscan.find('td#auth > svg.wifiauth > path.'+(net.authmode != 0 ? 'locked' : 'unlocked')).show();

            tr_wifiscan.data(net);
            tbody_wifiscan.append(tr_wifiscan);
        });

        // Volver a escanear redes si el tab sigue activo al recibir respuesta
        if ($('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').hasClass('active')) {
            wifipane.data('wifiscan-timer', setTimeout(scanWifiNetworks, 5 * 1000));
        }
    })
    .fail(function (e) {
        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        wifipane.data('wifiscan-timer', null);

        var msg;
        if (e.status == 0) {
            msg = 'Fallo al contactar dispositivo para siguiente escaneo';
        } else if (e.responseJSON == undefined) {
            msg = 'Tipo de dato no esperado en respuesta';
        } else {
            msg = e.responseJSON.msg;
        }
        mostrarReintentoScanWifi(msg);
    });
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
    var btn = $('<button class="btn btn-primary float-right" />').text('Reintentar');
    var al = yuboxMostrarAlert('danger',
        $('<div class="clearfix"/>')
        .append($('<span class="float-left" />').text(msg))
        .append(btn));
    btn.click(function () {
        al.remove();

        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        if (wifipane.data('wifiscan-timer') == null) wifipane.data('wifiscan-timer', setTimeout(scanWifiNetworks, 1));
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
    var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
    var tr_connected = wifipane.find('table#wifiscan > tbody > tr.table-success');
    if (tr_connected.length <= 0) return;
    if (ssid != null && tr_connected.data('ssid') != ssid) return;

    tr_connected.removeClass('table-success').addClass('table-warning');
    tr_connected.find('td#ssid > small.text-muted').text('Desconectándose');
}


function yuboxAPI(s)
{
    var mockup =  window.location.pathname.startsWith('/yubox-mockup/');
    return mockup
        ? '/yubox-mockup/'+s+'.php'
        : '/yubox-api/'+s;
}

function yuboxMostrarAlertText(alertstyle, text, timeout)
{
    var content = $('<span/>').text(text);
    return yuboxMostrarAlert(alertstyle, content, timeout);
}

function yuboxMostrarAlert(alertstyle, content, timeout)
{
    return yuboxDlgMostrarAlert('main > div.container', alertstyle, content, timeout);
}

function yuboxDlgMostrarAlertText(basesel, alertstyle, text, timeout)
{
    var content = $('<span/>').text(text);
    return yuboxDlgMostrarAlert(basesel, alertstyle, content, timeout);
}

function yuboxDlgMostrarAlert(basesel, alertstyle, content, timeout)
{
    var al = $(basesel).children('div.alert.yubox-alert-template')
        .clone()
        .removeClass('yubox-alert-template')
        .addClass('yubox-alert')
        .addClass('alert-'+alertstyle);
    al.find('button.close').before(content);

    $(basesel).children('div.yubox-alert').remove();
    $(basesel).children('div.alert.yubox-alert-template').after(al);
    if (timeout != undefined) {
        setTimeout(function() {
            al.remove();
        }, timeout);
    }

    return al;
}