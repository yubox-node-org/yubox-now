function setupNTPConfTab()
{
    const ntppane = getYuboxPane('ntpconfig', true);

    // Llenar los select con valores para elegir zona horaria
    const sel_tzh = ntppane.querySelector('select#ntptz_hh');
    const sel_tzm = ntppane.querySelector('select#ntptz_mm');
    for (var i = -12; i <= 14; i++) {
        let opt = document.createElement('option');
        opt.value = i;
        opt.textContent = ((i >= 0) ? '+' : '-')+(("0"+Math.abs(i)).slice(-2));
        sel_tzh.appendChild(opt);
    }
    for (var i = 0; i < 60; i++) {
        let opt = document.createElement('option');
        opt.value = i;
        opt.textContent = ("0"+Math.abs(i)).slice(-2);
        sel_tzm.appendChild(opt);
    }
    ntppane.querySelector('button[name=apply]').addEventListener('click', function () {
        const ntppane = getYuboxPane('ntpconfig', true);
        const sel_tzh = ntppane.querySelector('select#ntptz_hh');
        const sel_tzm = ntppane.querySelector('select#ntptz_mm');
    
        var postData = {
            ntphost:    ntppane.querySelector('form input#ntphost').value,
            ntptz:      parseInt(sel_tzh.value) * 3600
        };
        postData.ntptz += ((postData.ntptz >= 0) ? 1 : -1) * parseInt(sel_tzm.value) * 60;

        yuboxFetch('ntpconfig', 'conf.json', postData)
        .then((r) => {
            if (r.success) {
                // Recargar los datos recién guardados del dispositivo
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar que la conexión
                // realmente se puede realizar.
                setTimeout(yuboxLoadNTPConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('ntpconfig')
    .on('shown.bs.tab', function (e) {
        yuboxLoadNTPConfig();
    });
}

function yuboxLoadNTPConfig()
{
    const ntppane = getYuboxPane('ntpconfig', true);
    const span_connstatus = ntppane.querySelector('form span#ntp_connstatus');
    const span_timestamp = ntppane.querySelector('form span#ntp_timestamp');

    span_connstatus.classList.remove('badge-success', 'badge-danger');
    span_connstatus.classList.add('badge-secondary');
    span_connstatus.textContent = '(consultando)';
    span_timestamp.textContent = '...';

    yuboxFetch('ntpconfig', 'conf.json')
    .then((data) => {
        span_connstatus.classList.remove('badge-danger', 'badge-success', 'badge-secondary');
        span_timestamp.textContent = '';
        if (data.ntpsync) {
            span_connstatus.classList.add('badge-success');
            span_connstatus.textContent = 'SINCRONIZADO';
        } else {
            span_connstatus.classList.add('badge-danger');
            span_connstatus.textContent = 'NO SINCRONIZADO';
            span_timestamp.textContent = 'No se ha contactado a servidor NTP';
        }

        ntppane.querySelector('form input#ntphost').value = data.ntphost;

        ntppane.querySelector('select#ntptz_hh').value = (Math.trunc(data.ntptz / 3600));
        ntppane.querySelector('select#ntptz_mm').value = (Math.trunc(Math.abs((data.ntptz % 3600) / 60)));
    }, (e) => {
        span_connstatus.classList.remove('badge-danger', 'badge-success', 'badge-secondary');
        span_connstatus.classList.add('badge-danger');
        span_connstatus.textContent = 'NO SINCRONIZADO';
        span_timestamp.textContent = '(error en consulta)';

        yuboxStdAjaxFailHandler(e, 2000);
    });
}