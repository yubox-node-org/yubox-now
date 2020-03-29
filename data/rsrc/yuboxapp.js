$(document).ready(function () {
    /*
    // Mostrar que se puede capturar eventos de tab cambiado
    // https://getbootstrap.com/docs/4.4/components/navs/#events
    $('ul#yuboxMainTab a[data-toggle="tab"]').on('shown.bs.tab', function (e) {

        // SÓLO UN EJEMPLO. Hay que hacer alguna otra cosa al cambiar de tab
        console.log(e.target);
        yuboxMostrarAlertText('primary', 'Se ha mostrado el tab '+e.target.id);
    });
    */
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

    $('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').on('shown.bs.tab', function (e) {

        // Información sobre la MAC (y red conectada?)
        $.getJSON(yuboxAPI('wificonfig')+'/info')
        .done(function (data) {
            var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');

            // Mostrar los datos de la configuración actual
            wifipane.find('input#wlanmac').val(data.MAC);
        });

        scanWifiNetworks();
    });

    $('div#yuboxMainTabContent > div.tab-pane#wifi table#wifiscan > tbody').on('click', 'tr', function(e) {
        // TODO: quitar esto e inicializar diálogo modal antes de mostrar
        console.log(e);

        $('div#yuboxMainTabContent > div.tab-pane#wifi div#wifi-credentials').modal({ focus: true });
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
        data.sort(function (a, b) { return b.rssi - a.rssi; });

        var wifipane = $('div#yuboxMainTabContent > div.tab-pane#wifi');
        var tbody_wifiscan = wifipane.find('table#wifiscan > tbody');
        tbody_wifiscan.empty();
        data.forEach(function (net) {
            var tr_wifiscan = wifipane.data('wifiscan-template').clone();

            var desc_authmode = [
                '(ninguno)',
                'WEP',
                'WPA-PSK',
                'WPA2-PSK',
                'WPA-WPA2-PSK',
                'WPA2-ENTERPRISE'
            ];

            var svg_wifi = tr_wifiscan.find('td#rssi > svg.wifipower');
            var pwr = rssi2signalpercent(net.rssi);
            svg_wifi.removeClass('at-least-20 at-least-40 at-least-60 at-least-80');
            if (pwr >= 80)
                svg_wifi.addClass('at-least-80');
            else if (pwr >= 60)
                svg_wifi.addClass('at-least-60');
            else if (pwr >= 40)
                svg_wifi.addClass('at-least-40');
            else if (pwr >= 20)
                svg_wifi.addClass('at-least-20');
            tr_wifiscan.children('td#rssi').attr('title', 'Intensidad de señal: '+pwr+'%');

            tr_wifiscan.children('td#ssid').text(net.ssid);
            tr_wifiscan.children('td#auth').attr('title',
                (net.authmode >= 0 && net.authmode < desc_authmode.length)
                ? desc_authmode[net.authmode]
                : '(desconocido)');
            tr_wifiscan.find('td#auth > svg.wifiauth > path.'+(net.authmode != 0 ? 'locked' : 'unlocked')).show();

            tbody_wifiscan.append(tr_wifiscan);
        });

        // Volver a escanear redes si el tab sigue activo al recibir respuesta
        if ($('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').hasClass('active')) {
            setTimeout(scanWifiNetworks, 5 * 1000);
        }
    });
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

function yuboxMostrarAlertText(alertstyle, text)
{
    var content = $('<span/>').text(text);
    return yuboxMostrarAlert(alertstyle, content);
}

function yuboxMostrarAlert(alertstyle, content)
{
    var al = $('main > div.container > div.alert.yubox-alert-template')
        .clone()
        .removeClass('yubox-alert-template')
        .addClass('yubox-alert')
        .addClass('alert-'+alertstyle);
    al.find('button.close').before(content);

    $('main > div.container > div.yubox-alert').remove();
    $('main > div.container > div#yuboxMainTabContent').before(al);
}