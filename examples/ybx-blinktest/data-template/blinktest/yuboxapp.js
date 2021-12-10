function setupBlinkTab()
{
    const pane = getYuboxPane('blinktest', true);

    pane.querySelector('button[name=blinkupdate]').addEventListener('click', function() {
        const blink_interval = pane.querySelector('input[name=blinkms]');
        if (blink_interval.value == '') {
            yuboxMostrarAlertText('danger', 'Se requiere un valor!', 3000);
        } else {
            var postData = {
                blinkms: blink_interval.value
            };

            yuboxFetch('blinktest', 'conf.json', postData)
            .then((r) => {
                if (r.success) {
                    yuboxMostrarAlertText('success', r.msg, 3000);
                } else {
                    yuboxMostrarAlertText('danger', r.msg);
                }
            }, (e) => { yuboxStdAjaxFailHandler(e, 4000); });
        }
    });
}