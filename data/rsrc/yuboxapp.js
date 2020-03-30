$(document).ready(function () {
    setupWiFiTab();

    // Mostrar el tab preparado por omisión como activo
    $('ul#yuboxMainTab a.set-active').removeClass('set-active').tab('show');
});

function setupWiFiTab()
{
    var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
    var data = {
        'wifiscan-template':
            wifipane.find('table#wifiscan > tbody > tr.template')
            .removeClass('template')
            .detach()
    }
    wifipane.data(data);

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    $('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').on('shown.bs.tab', function (e) {
/*
        // Información sobre la MAC (y red conectada?)
        $.getJSON(yuboxAPI('wificonfig')+'/info')
        .done(function (data) {
            var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');

            // Mostrar los datos de la configuración actual
            wifipane.find('input#wlanmac').val(data.MAC);
        });
*/
        scanWifiNetworks();
    });

    $('div#yuboxMainTabContent > div.tab-pane#wifi table#wifiscan > tbody').on('click', 'tr', function(e) {
        var net = $(e.currentTarget).data();

        var dlg_wificred = $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials');
        dlg_wificred.find('h5#wifi-credentials-title').text(net.ssid);
        dlg_wificred.find('input#key_mgmt').val(wifiauth_desc(net.authmode));
        dlg_wificred.find('div.form-group.wifi-auth').hide();
        dlg_wificred.find('div.form-group.wifi-auth input').val('');
        dlg_wificred.find('button[name=connect]').prop('disabled', true);
        if (net.authmode == 5) {
            // Autenticación WPA-ENTERPRISE
            dlg_wificred.find('div.form-group.wifi-auth-eap').show();
            // Todo: llenar credenciales existentes, si están disponibles
        } else if (net.authmode > 0) {
            // Autenticación con contraseña
            dlg_wificred.find('div.form-group.wifi-auth-psk').show();
            // Todo: llenar credenciales existentes, si están disponibles
        } else {
            // Red sin autenticación, activar directamente opción de conectar
            dlg_wificred.find('button[name=connect]').prop('disabled', false);
        }
        dlg_wificred.modal({ focus: true });
    });
}

function scanWifiNetworks()
{
    if (!$('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').hasClass('active')) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    $.get(yuboxAPI('wificonfig')+'/scan')
    .done(function (data) {
        data.sort(function (a, b) {
            if (a.connected) return -1;
            if (b.connected) return 1;
            return b.rssi - a.rssi;
        });

        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        var tbody_wifiscan = wifipane.find('table#wifiscan > tbody');
        tbody_wifiscan.empty();
        data.forEach(function (net) {
            var tr_wifiscan = wifipane.data('wifiscan-template').clone();

            // Mostrar dibujo de intensidad de señal a partir de RSSI
            var svg_wifi = tr_wifiscan.find('td#rssi > svg.wifipower');
            var pwr = rssi2signalpercent(net.rssi);
            svg_wifi.removeClass('at-least-1bar at-least-2bars at-least-3bars at-least-4bars');
            if (pwr >= 80)
                svg_wifi.addClass('at-least-4bars');
            else if (pwr >= 60)
                svg_wifi.addClass('at-least-3bars');
            else if (pwr >= 40)
                svg_wifi.addClass('at-least-2bars');
            else if (pwr >= 20)
                svg_wifi.addClass('at-least-1bar');
            tr_wifiscan.children('td#rssi').attr('title', 'Intensidad de señal: '+pwr+'%');

            // Mostrar candado según si hay o no autenticación para la red
            tr_wifiscan.children('td#ssid').text(net.ssid);
            if (net.connected) {
                var sm_connlabel = $('<small class="form-text text-muted" />').text('Conectado');
                tr_wifiscan.addClass('table-success');
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
            setTimeout(scanWifiNetworks, 5 * 1000);
        }
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
    var al = $('main > div.container > div.alert.yubox-alert-template')
        .clone()
        .removeClass('yubox-alert-template')
        .addClass('yubox-alert')
        .addClass('alert-'+alertstyle);
    al.find('button.close').before(content);

    $('main > div.container > div.yubox-alert').remove();
    $('main > div.container > div#yuboxMainTabContent').before(al);
    if (timeout != undefined) {
        setTimeout(function() {
            al.remove();
        }, timeout);
    }
}

