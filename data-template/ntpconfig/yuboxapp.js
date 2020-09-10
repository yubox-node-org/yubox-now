function setupNTPConfTab()
{
    var ntppane = getYuboxPane('ntpconfig');

    // Llenar los select con valores para elegir zona horaria
    var sel_tzh = ntppane.find('select#ntptz_hh');
    var sel_tzm = ntppane.find('select#ntptz_mm');
    for (var i = -12; i <= 14; i++) {
        $('<option></option>')
            .val(i)
            .text(((i >= 0) ? '+' : '-')+(("0"+Math.abs(i)).slice(-2)))
            .appendTo(sel_tzh);
    }
    for (var i = 0; i < 60; i++) {
        $('<option></option>')
            .val(i)
            .text(("0"+Math.abs(i)).slice(-2))
            .appendTo(sel_tzm);
    }
    ntppane.find('button[name=apply]').click(function () {
        var ntppane = getYuboxPane('ntpconfig');
        var sel_tzh = ntppane.find('select#ntptz_hh');
        var sel_tzm = ntppane.find('select#ntptz_mm');
    
        var postData = {
            ntphost:    ntppane.find('form input#ntphost').val(),
            ntptz:      parseInt(sel_tzh.val()) * 3600
        };
        postData.ntptz += ((postData.ntptz >= 0) ? 1 : -1) * parseInt(sel_tzm.val()) * 60;

        $.post(yuboxAPI('ntpconfig')+'/conf.json', postData)
        .done(function (r) {
            if (r.success) {
                // Recargar los datos recién guardados del dispositivo
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar que la conexión
                // realmente se puede realizar.
                setTimeout(yuboxLoadNTPConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        })
        .fail(function (e) { yuboxStdAjaxFailHandler(e, 2000); });
    });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('ntpconfig')
    .on('shown.bs.tab', function (e) {
        yuboxLoadNTPConfig();
    });
}

function yuboxLoadNTPConfig()
{
    var ntppane = getYuboxPane('ntpconfig');
    ntppane.find('form span#ntp_connstatus')
        .removeClass('badge-success badge-danger')
        .addClass('badge-secondary')
        .text('(consultando)');
    ntppane.find('form span#ntp_timestamp').text('...');

    $.get(yuboxAPI('ntpconfig')+'/conf.json')
    .done(function (data) {
        var sel_tzh = ntppane.find('select#ntptz_hh');
        var sel_tzm = ntppane.find('select#ntptz_mm');
        var span_connstatus = ntppane.find('form span#ntp_connstatus');
        var span_timestamp = ntppane.find('form span#ntp_timestamp');

        span_connstatus.removeClass('badge-danger badge-success badge-secondary');
        span_timestamp.text('');
        if (data.ntpsync) {
            span_connstatus
                .addClass('badge-success')
                .text('SINCRONIZADO');
            //span_timestamp.text('TODO');
        } else {
            span_connstatus
                .addClass('badge-danger')
                .text('NO SINCRONIZADO');
            span_timestamp.text('No se ha contactado a servidor NTP');
        }

        ntppane.find('form input#ntphost').val(data.ntphost);
        sel_tzh.val(Math.trunc(data.ntptz / 3600));
        sel_tzm.val(Math.trunc(Math.abs((data.ntptz % 3600) / 60)));
    })
    .fail(function (e) {
        ntppane.find('form span#ntp_connstatus')
            .removeClass('badge-success badge-danger badge-secondary')
            .addClass('badge-danger')
            .text('NO SINCRONIZADO');
        ntppane.find('form span#ntp_timestamp').text('(error en consulta)');

        yuboxStdAjaxFailHandler(e, 2000);
    });
}