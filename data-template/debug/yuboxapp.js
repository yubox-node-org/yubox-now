function setupDebugTab()
{
    const pane = getYuboxPane('debug', true);
    var data = {
    };
    pane.data = data;

    pane.querySelector('button[name=saveconfig]').addEventListener('click', () => {
        yuboxFetch('debug', 'conf.json', {
            level: pane.querySelector('select#debug_level').value
        })
        .then(r => {
            yuboxMostrarAlertText('success', r.msg, 3000);
        }, e => yuboxStdAjaxFailHandler(e, 2000));
    });

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('debug')
    .on('shown.bs.tab', function (e) {
        yuboxFetch('debug', 'conf.json')
        .then(data => {
            pane.querySelector('select#debug_level').value = data.level;
        }, e => yuboxStdAjaxFailHandler(e, 2000));
    });
}
