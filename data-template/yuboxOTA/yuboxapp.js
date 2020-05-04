function setupYuboxOTATab()
{
    var otapane = $('div#yuboxMainTabContent > div.tab-pane#yuboxOTA');
    var data = {
        'sse':  null
    };
    otapane.data(data);

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
        yuboxOTAUpload_init();
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
            yuboxOTAUpload_shutdown();
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
            yuboxOTAUpload_shutdown();
        });
    });
}

function yuboxOTAUpload_init()
{
    var otapane = $('div#yuboxMainTabContent > div.tab-pane#yuboxOTA');
    otapane.find('button[name=apply]').prop('disabled', true);
    yuboxOTAUpload_setProgressBar(0);
    otapane.find('div.upload-progress span#filename').text('-');
    otapane.find('div.upload-progress span#current').text('0');
    otapane.find('div.upload-progress span#total').text('0');
    otapane.find('div.upload-progress span#currupload').text('0');
    otapane.find('div.upload-progress').show();

    if (!!window.EventSource) {
        var sse = new EventSource(yuboxAPI('yuboxOTA')+'/events');
        sse.addEventListener('uploadFileStart', function (e) {
            var data = $.parseJSON(e.data);
            var totalKB = data.total / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#current').text(0);
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            otapane.find('div.progress-bar')
                .removeClass('bg-info bg-danger')
                .addClass(data.firmware ? 'bg-danger' : 'bg-info');
            yuboxOTAUpload_setProgressBar(0);
        });
        sse.addEventListener('uploadFileProgress', function (e) {
            var data = $.parseJSON(e.data);
            var totalKB = data.total / 1024.0;
            var currKB = data.current / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#current').text(currKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(totalKB > 0.0 ? 100.0 * currKB / totalKB : 0);
        });
        sse.addEventListener('uploadFileEnd', function (e) {
            var data = $.parseJSON(e.data);
            var totalKB = data.total / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#current').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(100);
        });
        otapane.data('sse', sse);
    }
}

function yuboxOTAUpload_shutdown()
{
    var otapane = $('div#yuboxMainTabContent > div.tab-pane#yuboxOTA');
    otapane.find('button[name=apply]').prop('disabled', false);
    otapane.find('div.upload-progress').hide();
    if (otapane.data('sse') != null) {
        otapane.data('sse').close();
        otapane.data('sse', null);
    }
}

function yuboxOTAUpload_setProgressBar(v)
{
    var pb = $('div#yuboxMainTabContent > div.tab-pane#yuboxOTA div.progress-bar');
    pb.css('width', v+'%');
    pb.attr('aria-valuenow', v);
    pb.text(v.toFixed(1) + ' %');
}