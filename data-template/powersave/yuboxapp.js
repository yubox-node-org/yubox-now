function setupPowerSaveTab()
{
    const pane = getYuboxPane('powersave', true);
    pane.data = { skip_on_wakeup_deepsleep: null }

    pane.querySelectorAll('input[name=skip_wifi_after_ds]')
        .forEach(el => { el.addEventListener('click', () => {
        console.log('input[name=skip_wifi_after_ds] CLICK');
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
    }) });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('powersave')
    .on('shown.bs.tab', function (e) {
        yuboxFetch('wificonfig', 'skip_wifi_after_deepsleep')
        .then(data => {
            pane.querySelector('input[name=skip_wifi_after_ds]#skip_wifi_after_ds_'+(data.skip_on_wakeup_deepsleep ? '1' : '0')).click();
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });

}