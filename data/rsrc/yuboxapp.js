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
        console.log(data);

        // Volver a escanear redes si el tab sigue activo al recibir respuesta
        if ($('ul#yuboxMainTab a#wifi-tab[data-toggle="tab"]').hasClass('active')) {
            setTimeout(scanWifiNetworks, 5 * 1000);
        }
    });
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