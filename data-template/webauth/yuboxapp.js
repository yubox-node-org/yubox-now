function setupWebAuthTab()
{
    const authpane = getYuboxPane('webauth', true);

    // https://getbootstrap.com/docs/4.4/components/navs/#events
    getYuboxNavTab('webauth').on('shown.bs.tab', function (e) {
        yuboxFetch('authconfig')
        .then((data) => {
            authpane.querySelector('input#yubox_username').value = data.username;
            authpane.querySelectorAll('input#yubox_password1, input#yubox_password2')
            .forEach((yubox_password) => { yubox_password.value = data.password; });
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });
    authpane.querySelector('button[name=apply]').addEventListener('click', function () {
        var postData = {
            password1: authpane.querySelector('input#yubox_password1').value,
            password2: authpane.querySelector('input#yubox_password2').value
        };
        if (postData.password1 != postData.password2) {
            yuboxMostrarAlertText('danger', 'Contraseña y confirmación no coinciden.', 5000);
        } else if (postData.password1 == '') {
            yuboxMostrarAlertText('danger', 'Contraseña no puede estar vacía.', 5000);
        } else {
            yuboxFetch('authconfig', '', postData)
            .then((data) => {
                if (data.success) {
                    // Al guardar correctamente las credenciales, recargar para que las pida
                    yuboxMostrarAlertText('success', data.msg, 2000);
                    setTimeout(function () {
                        window.location.reload();
                    }, 3 * 1000);
                } else {
                    yuboxMostrarAlertText('danger', data.msg, 2000);
                }
            }, (e) => { yuboxStdAjaxFailHandler(e, 2000); })
        }
    });
}
