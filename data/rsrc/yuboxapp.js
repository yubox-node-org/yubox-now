$(document).ready(function () {
    
    // Mostrar que se puede capturar eventos de tab cambiado
    // https://getbootstrap.com/docs/4.4/components/navs/#events
    $('ul#yuboxMainTab a[data-toggle="tab"]').on('shown.bs.tab', function (e) {

        // SÓLO UN EJEMPLO. Hay que hacer alguna otra cosa al cambiar de tab
        console.log(e.target);
        yuboxMostrarAlertText('primary', 'Se ha mostrado el tab '+e.target.id);
    });
});

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