function setupNTPConfTab()
{
    const ntppane = getYuboxPane('ntpconfig', true);
    var data = {
        yuboxoffset: null,  // Offset desde hora actual a hora YUBOX, en msec
        clocktimer: null
    };
    ntppane.data = data;

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
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar que la conexión
                // realmente se puede realizar.
                setTimeout(yuboxLoadNTPConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });
    ntppane.querySelector('button[name=browsertime]').addEventListener('click', function () {
        const ntppane = getYuboxPane('ntpconfig', true);

        let postData = {
            utctime_ms: Date.now()
        };

        yuboxFetch('ntpconfig', 'rtc.json', postData)
        .then((r) => {
            if (r.success) {
                yuboxMostrarAlertText('success', r.msg, 3000);

                // Recargar luego de 5 segundos para verificar la hora
                setTimeout(yuboxLoadNTPConfig, 5 * 1000);
            } else {
                yuboxMostrarAlertText('danger', r.msg);
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });

    ntppane.data['clocktimer'] = setInterval(() => {
        const ntppane = getYuboxPane('ntpconfig', true);
        let datetext = (ntppane.data['yuboxoffset'] == null)
            ? '(no disponible)'
            : (new Date(Date.now() + ntppane.data['yuboxoffset'])).toString();
        ntppane.querySelector('form span#utctime').textContent = datetext;
    }, 500);

    // https://getbootstrap.com/docs/5.1/components/navs-tabs/#events
    getYuboxNavTab('ntpconfig', true)
    .addEventListener('shown.bs.tab', function (e) {
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
        // Cantidad de milisegundos a sumar a timestamp browser para obtener timestamp
        // en el dispositivo. Esto asume que no hay desvíos de reloj RTC.
        ntppane.data['yuboxoffset'] = data.utctime * 1000 - Date.now();

        span_connstatus.classList.remove('badge-danger', 'badge-success', 'badge-secondary');
        span_timestamp.textContent = '';
        if (data.ntpsync) {
            span_connstatus.classList.add('badge-success');
            span_connstatus.textContent = 'SINCRONIZADO';

            // Cálculo de fecha de última sincronización NTP dispositivo
            const date_lastsync = new Date(Date.now() - data.ntpsync_msec);
            span_timestamp.textContent = 'Últ. sinc. NTP: '
                + date_lastsync.toString();
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