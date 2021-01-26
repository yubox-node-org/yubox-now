function setupYuboxOTATab()
{
    var otapane = getYuboxPane('yuboxOTA');
    var data = {
        'sse':  null
    };
    otapane.data(data);

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('yuboxOTA').on('shown.bs.tab', function (e) {
        var otapane = getYuboxPane('yuboxOTA');

        $.get(yuboxAPI('yuboxOTA')+'/rollback')
        .done(function (data) {
            var spanRB = otapane.find('span#canrollback');
            var btnRB = otapane.find('button[name="rollback"]');

            spanRB.removeClass('badge-danger badge-success');
            if (data.canrollback) {
                spanRB
                    .addClass('badge-success')
                    .text('RESTAURABLE');
                btnRB.prop('disabled', false);
            } else {
                spanRB
                    .addClass('badge-danger')
                    .text('NO RESTAURABLE');
                btnRB.prop('disabled', true);
            }
        })
        .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
    });

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

                if (data.reboot) {
                    // Por haber recibido esta indicación, ya se sabe que el
                    // dispositivo está listo para ser reiniciado.
                    $.post(yuboxAPI('yuboxOTA')+'/reboot', {})
                    .fail(function (e) {
                        yuboxStdAjaxFailHandler(e, 3000);
                    });
                }
            } else {
                yuboxMostrarAlertText('danger', data.msg, 6000);
            }
            yuboxOTAUpload_shutdown();
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandler(e, 2000);
            yuboxOTAUpload_shutdown();
        });
    });
    otapane.find('button[name=rollback]').click(function () {
        otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', true);
        $.post(yuboxAPI('yuboxOTA')+'/rollback', {})
        .done(function (data) {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 4000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);
            } else {
                yuboxMostrarAlertText('danger', data.msg, 2000);
            }
            otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', false);
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandler(e, 2000);
            otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', false);
        });
    });
    otapane.find('button[name=reboot]').click(function () {
        otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', true);
        $.post(yuboxAPI('yuboxOTA')+'/reboot', {})
        .done(function (data) {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 4000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);
            } else {
                yuboxMostrarAlertText('danger', data.msg, 2000);
            }
            otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', false);
        })
        .fail(function (e) {
            yuboxStdAjaxFailHandler(e, 2000);
            otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', false);
        });
    });
}

function yuboxOTAUpload_init()
{
    var otapane = getYuboxPane('yuboxOTA');
    otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', true);
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
            var currUploadKB = data.currupload / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#current').text(0);
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#currupload').text(currUploadKB.toFixed(1));
            otapane.find('div.progress-bar')
                .removeClass('bg-info bg-danger')
                .addClass(data.firmware ? 'bg-danger' : 'bg-info');
            yuboxOTAUpload_setProgressBar(0);
        });
        sse.addEventListener('uploadFileProgress', function (e) {
            var data = $.parseJSON(e.data);
            var totalKB = data.total / 1024.0;
            var currKB = data.current / 1024.0;
            var currUploadKB = data.currupload / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#current').text(currKB.toFixed(1));
            otapane.find('div.upload-progress span#currupload').text(currUploadKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(totalKB > 0.0 ? 100.0 * currKB / totalKB : 0);
        });
        sse.addEventListener('uploadFileEnd', function (e) {
            var data = $.parseJSON(e.data);
            var totalKB = data.total / 1024.0;
            var currUploadKB = data.currupload / 1024.0;
            otapane.find('div.upload-progress span#filename').text(data.filename);
            otapane.find('div.upload-progress span#current').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#total').text(totalKB.toFixed(1));
            otapane.find('div.upload-progress span#currupload').text(currUploadKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(100);
        });
        sse.addEventListener('uploadPostTask', function (e) {
	        var data = $.parseJSON(e.data);
	        var msg = data.task;
	        var taskDesc = {
                'firmware-commit-start':		'Iniciando commit de firmware nuevo',
                'firmware-commit-failed':		'Falló el commit de firmware nuevo',
                'firmware-commit-end':			'Finalizado commit de firmware nuevo',
                'datafiles-load-oldmanifest':	'Cargando lista de archivos de datos viejos',
                'datafiles-delete-oldbackup':	'Borrando respaldo anterior de archivos de datos',
                'datafiles-rename-oldfiles':	'Respaldando archivos de datos viejos',
                'datafiles-rename-newfiles':	'Instalando archivos de datos nuevos',
                'datafiles-end':				'Fin de instalación de archivos de datos'
	        };
	        if (taskDesc[data.task] != undefined) msg = taskDesc[data.task];
	        yuboxOTAUpload_setProgressBarMessage(100, msg);
        });
        otapane.data('sse', sse);
    } else {
      yuboxMostrarAlertText('danger', 'Este navegador no soporta Server-Sent Events, no se puede monitorear upload.');
    }
}

function yuboxOTAUpload_shutdown()
{
    var otapane = getYuboxPane('yuboxOTA');
    otapane.find('button[name=apply], button[name=rollback], button[name=reboot]').prop('disabled', false);
    otapane.find('div.upload-progress').hide();
    if (otapane.data('sse') != null) {
        otapane.data('sse').close();
        otapane.data('sse', null);
    }
}

function yuboxOTAUpload_setProgressBar(v)
{
	yuboxOTAUpload_setProgressBarMessage(v, v.toFixed(1) + ' %');
}

function yuboxOTAUpload_setProgressBarMessage(v, msg)
{
    var otapane = getYuboxPane('yuboxOTA');
    var pb = otapane.find('div.progress-bar');
    pb.css('width', v+'%');
    pb.attr('aria-valuenow', v);
    pb.text(msg);
}
