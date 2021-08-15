function setupBlinkTab()
{
    //alert('setupBlinkTab');

    var pane = getYuboxPane('blinktest');

    pane.find('button[name=blinkupdate]').click(function() {
        var blink_interval = pane.find('input[name=blinkms]');
        if (blink_interval.val() == '') {
            //alert('Se requiere un valor');
            yuboxMostrarAlertText('danger', 'Se requiere un valor!', 3000);
        } else {
            var postData = {
                blinkms: blink_interval.val()
            };

            $.post(yuboxAPI('blinktest')+'/conf.json', postData)
            .done(function (r) {
                if (r.success) {
                    yuboxMostrarAlertText('success', r.msg, 3000);
                } else {
                    yuboxMostrarAlertText('danger', r.msg);
                }
            })
            .fail(function (e) { yuboxStdAjaxFailHandler(e, 4000); });
        }
    });
}