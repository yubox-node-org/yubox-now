function setupYuboxOTATab()
{
    var otapane = $('div#yuboxMainTabContent > div.tab-pane#yuboxOTA');

    otapane.find('input[type=file]#tgzupload').change(function () {
        var lbl = $(this).next('label.custom-file-label');
        if (lbl.data('default') == undefined) {
            // Almacenar texto original para restaurar si archivo vacío
            lbl.data('default', lbl.text())
        }
        var txt = ($(this)[0].files.length > 0)
            ? $(this)[0].files[0].name
            : lbl.data('default');
        lbl.text(txt);
    });
    otapane.find('button[name=apply]').click(function () {
        var fi = otapane.find('input[type=file]#tgzupload');
        if (fi[0].files.length <= 0) {
            yuboxMostrarAlertText('danger', 'Es necesario elegir un archivo tgz para actualización.', 2000);
            return;
        }
        if (typeof FormData == 'undefined') {
            yuboxMostrarAlertText('danger', 'Este navegador no soporta FormData para subida de datos. Actualice su navegador.', 2000);
            return;
        }
        var postData = new FormData();
        postData.append('tgzupload', fi[0].files[0]);
        $.post({
            url: yuboxAPI('yuboxOTA')+'/tgzupload',
            data: postData,
            processData: false,
            contentType: false
        })
        .done(function (data) {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 5000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);
            } else {
                yuboxMostrarAlertText('danger', data.msg, 2000);
            }
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
    });
}
