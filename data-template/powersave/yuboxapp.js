function setupPowerSaveTab()
{
    const pane = getYuboxPane('powersave', true);
    pane.data = { skip_on_wakeup_deepsleep: null }

    pane.querySelectorAll('input[name=skip_wifi_after_ds]')
        .forEach(el => { el.addEventListener('click', () => {
            let nstate = pane.querySelector('input[name=skip_wifi_after_ds]:checked').value;
            nstate = (nstate != '0');
            if (pane.data.skip_on_wakeup_deepsleep != null) {
                if (pane.data.skip_on_wakeup_deepsleep != nstate) {
                    if (nstate) {
                        if (!confirm('Al activar esta opción, si el firmware entra en deep-sleep, al despertar se OMITIRÁ el arranque de WiFi con propósito de ahorrar energía. Para volver a tener acceso a este GUI se requerirá un reset u otro arranque distinto a deep-sleep.')) {
                            setTimeout(1, () => {
                                pane.querySelector('input[name=skip_wifi_after_ds]#skip_wifi_after_ds_0').click();
                            });
                            return;
                        }
                    }

                    let postData = {
                        skip_on_wakeup_deepsleep:  nstate ? 1 : 0,
                    };
                    yuboxFetch('wificonfig', 'skip_wifi_after_deepsleep', postData)
                    .then((data) => {
                        //console.log(data);
                    }, e => yuboxStdAjaxFailHandler(e, 2000));
                }
            }
            pane.data.skip_on_wakeup_deepsleep = nstate;
        })
    });
    pane.querySelector('form#cpufreq button.yubox-guardar-cpufreq').addEventListener('click', function() {
        if (pane.querySelector('select#cpufrequency').value != '240' &&
            !confirm('El reducir el reloj de CPU puede ahorrar energía pero potencialmente interfiere con tareas que requieran interacción precisa con el hardware, como SoftwareSerial.')) {
            return;
        }

        const form = pane.querySelector('form#cpufreq');
        const formData = new FormData(form);
        yuboxFetch('yuboxOTA', 'cpufreq', new URLSearchParams(formData))
        .then((data) => {
            //console.log(data);
        }, e => yuboxStdAjaxFailHandler(e, 2000));
    });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('powersave')
    .on('shown.bs.tab', function (e) {
        yuboxFetch('wificonfig', 'skip_wifi_after_deepsleep')
        .then(data => {
            pane.querySelector('input[name=skip_wifi_after_ds]#skip_wifi_after_ds_'+(data.skip_on_wakeup_deepsleep ? '1' : '0')).click();
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });

        yuboxFetch('yuboxOTA', 'cpufreq')
        .then(data => {
            pane.querySelector('select#cpufrequency').value = data.cpufrequency;
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); })
    });

}