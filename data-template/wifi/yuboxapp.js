function setupWiFiTab()
{
    const wifipane = getYuboxPane('wifi', true);
    var data = {
        'sse':                  null,
        'wifiscan-template':    wifipane.querySelector('table#wifiscan > tbody > tr.template'),
        'wifinetworks-template':wifipane.querySelector('div#wifi-networks table#wifi-saved-networks > tbody > tr.template')
    }
    data['wifiscan-template'].classList.remove('template');
    data['wifiscan-template'].remove();
    data['wifinetworks-template'].classList.remove('template');
    data['wifinetworks-template'].remove();
    wifipane.data = data;

    // https://getbootstrap.com/docs/5.1/components/navs-tabs/#events
    const wifitab = getYuboxNavTab('wifi', true);
    wifitab.addEventListener('shown.bs.tab', function (e) {
        yuboxWiFi_setupWiFiScanListener();
    })
    wifitab.addEventListener('hide.bs.tab', function (e) {
        if (wifipane.data['sse'] != null) {
          wifipane.data['sse'].close();
          wifipane.data['sse'] = null;
        }
    });

    // Qué hay que hacer al hacer clic en una fila que representa la red
    wifipane.querySelector('table#wifiscan > tbody').addEventListener('click', function(e) {
        let currentTarget = null;
        for (let target = e.target; target && target != this; target = target.parentNode ) {
            if (target.matches('tr')) {
                currentTarget = target;
                break;
            }
        }
        if (currentTarget == null) return;

        var net = currentTarget.data;
        if (net.connected) {
            yuboxFetch('wificonfig', 'connection')
            .then((data) => {
                const dlg_wifiinfo = wifipane.querySelector('div#wifi-details');

                var res = evaluarIntensidadRedWifi(dlg_wifiinfo.querySelector('tr#rssi > td > svg.wifipower'), data.rssi);
                dlg_wifiinfo.querySelector('tr#rssi > td.text-muted').textContent = (res.diag + ' ('+res.pwr+' %)');

                dlg_wifiinfo.querySelectorAll('tr#auth > td > svg.wifiauth > path')
                    .forEach((el) => { el.style.display = 'none'; });
                dlg_wifiinfo.querySelector('tr#auth > td > svg.wifiauth > path.'+(net.authmode != 0 ? 'locked' : 'unlocked'))
                    .style.display = '';
                dlg_wifiinfo.querySelector('tr#auth > td.text-muted').textContent = (wifiauth_desc(net.authmode));

                dlg_wifiinfo.querySelector('tr#bssid > td.text-muted').textContent = (net.ap[0].bssid);
                dlg_wifiinfo.querySelector('tr#channel > td.text-muted').textContent = (net.ap[0].channel);

                dlg_wifiinfo.querySelector('h5#wifi-details-title').textContent = (data.ssid);
                dlg_wifiinfo.querySelector('input#ssid').value = (data.ssid);
                dlg_wifiinfo.querySelector('div#netinfo div#mac').textContent = (data.mac);
                dlg_wifiinfo.querySelector('div#netinfo div#ipv4').textContent = (data.ipv4);
                dlg_wifiinfo.querySelector('div#netinfo div#gateway').textContent = (data.gateway);
                dlg_wifiinfo.querySelector('div#netinfo div#netmask').textContent = (data.netmask);

                const div_dns = dlg_wifiinfo.querySelector('div#netinfo div#dns');
                while (div_dns.firstChild) div_dns.removeChild(div_dns.firstChild);
                for (var i = 0; i < data.dns.length; i++) {
                    let r = document.createElement('div'); r.classList.add('row');
                    let c = document.createElement('div'); c.classList.add('col');
                    c.textContent = (data.dns[i]);
                    r.appendChild(c);
                    div_dns.appendChild(r);
                }

                bootstrap.Modal.getOrCreateInstance(dlg_wifiinfo, { focus: true })
                .show();
            }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
        } else {
            yuboxWiFi_displayNetworkDialog('scanned', net);
        }
    });
    wifipane.querySelector('div#wifi-credentials select#authmode').addEventListener('change', function () {
        const dlg_wificred = wifipane.querySelector('div#wifi-credentials');
        var authmode = this.value;

        dlg_wificred.querySelectorAll('div.form-group.wifi-auth')
            .forEach((el) => { el.style.display = 'none'; });
        dlg_wificred.querySelectorAll('div.form-group.wifi-auth input')
            .forEach((el) => { el.value = ''; });
        dlg_wificred.querySelector('button[name=connect]').disabled = true;
        if (authmode == 5) {
            // Autenticación WPA-ENTERPRISE
            dlg_wificred.querySelectorAll('div.form-group.wifi-auth-eap')
                .forEach((el) => { el.style.display = ''; });
            dlg_wificred.querySelector('div.form-group.wifi-auth-eap input#password')
                .dispatchEvent(new Event('change'));
        } else if (authmode > 0) {
            // Autenticación con contraseña
            dlg_wificred.querySelectorAll('div.form-group.wifi-auth-psk')
                .forEach((el) => { el.style.display = ''; });
            dlg_wificred.querySelector('div.form-group.wifi-auth-psk input#psk')
                .dispatchEvent(new Event('change'));
        } else {
            // Red sin autenticación, activar directamente opción de conectar
            dlg_wificred.querySelector('button[name=connect]').disabled = false;
        }
    });

    // Qué hay que hacer al hacer clic en el botón de Redes Guardadas
    wifipane.querySelector('button[name=networks]').addEventListener('click', function () {
        yuboxFetch('wificonfig', 'networks')
        .then((data) => {
            const dlg_wifinetworks = wifipane.querySelector('div#wifi-networks');
            const tbody_wifinetworks = dlg_wifinetworks.querySelector('table#wifi-saved-networks > tbody');
            while (tbody_wifinetworks.firstChild) tbody_wifinetworks.removeChild(tbody_wifinetworks.firstChild);

            data.forEach(function (net) {
                let tr_wifinet = wifipane.data['wifinetworks-template'].cloneNode(true);
                tr_wifinet.data = {'ssid': net.ssid};
                tr_wifinet.querySelector('td#ssid').textContent = (net.ssid);
                if (net.identity != null) {
                    // Autenticación WPA-ENTERPRISE
                    tr_wifinet.querySelector('td#auth').title = 'Seguridad: ' + wifiauth_desc(5);
                    tr_wifinet.querySelector('td#auth > svg.wifiauth > path.locked').style.display = '';
                } else if (net.psk != null) {
                    // Autenticación PSK
                    tr_wifinet.querySelector('td#auth').title = 'Seguridad: ' + wifiauth_desc(4);
                    tr_wifinet.querySelector('td#auth > svg.wifiauth > path.locked').style.display = '';
                } else {
                    // Sin autenticación
                    tr_wifinet.querySelector('td#auth').title = 'Seguridad: ' + wifiauth_desc(0);
                    tr_wifinet.querySelector('td#auth > svg.wifiauth > path.unlocked').style.display = '';
                }
                tbody_wifinetworks.appendChild(tr_wifinet);
            });

            bootstrap.Modal.getOrCreateInstance(dlg_wifinetworks, { focus: true })
            .show();
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });

    // Qué hay que hacer al hacer clic en el botón de Agregar red .
    // NOTA: hay 2 botones que se llaman igual. Uno en la interfaz principal, y el otro
    // en el diálogo de mostrar las redes guardadas. El comportamiento a continuación
    // define la acción para AMBOS botones.
    let addnet_cb = function () {
        const dlg_wifinetworks = wifipane.querySelector('div#wifi-networks');
        if (dlg_wifinetworks.classList.contains('show')) {
            bootstrap.Modal.getOrCreateInstance(dlg_wifinetworks)
            .hide();
        }
        yuboxWiFi_displayNetworkDialog('manual', { authmode : 4, psk: null});
    };
    wifipane.querySelectorAll('button[name=addnet]')
        .forEach((el) => { el.addEventListener('click', addnet_cb); });

    // Qué hay que hacer al hacer clic en una fila de red guardada
    wifipane.querySelector('div#wifi-networks table#wifi-saved-networks > tbody').addEventListener('click', function(e) {
        let currentTarget = null;
        for (let target = e.target; target && target != this; target = target.parentNode ) {
            if (target.matches('tr td#delete button.btn-danger')) {
                currentTarget = target;
                break;
            }
        }
        if (currentTarget == null) return;

        const dlg_wifinetworks = wifipane.querySelector('div#wifi-networks');
        let tr_wifinet = currentTarget; while (tr_wifinet && !tr_wifinet.matches('tr')) tr_wifinet = tr_wifinet.parentNode;
        let ssid = tr_wifinet.data['ssid'];

        if (!confirm("Presione OK para OLVIDAR las credenciales de la red "+ssid)) return;

        yuboxFetchMethod('DELETE', 'wificonfig', 'networks/'+ssid)
        .then((data) => {
            // Credenciales borradas
            tr_wifinet.parentNode.removeChild(tr_wifinet);
        }, (e) => {
            yuboxStdAjaxFailHandlerDlg(dlg_wifinetworks.querySelector('div.modal-body'), e, 2000);
        });
    });

    // Comportamiento de controles de diálogo de ingresar credenciales red
    const dlg_wificred = wifipane.querySelector('div#wifi-credentials');
    dlg_wificred.querySelectorAll('div.form-group.wifi-auth-eap input').forEach((el) => {
        ['change', 'keypress', 'blur'].forEach((k) => { el.addEventListener(k, checkValidWifiCred_EAP); });
    });
    dlg_wificred.querySelectorAll('div.form-group.wifi-auth-psk input').forEach((el) => {
        ['change', 'keypress', 'blur'].forEach((k) => { el.addEventListener(k, checkValidWifiCred_PSK); });
    });
    dlg_wificred.querySelector('div.modal-footer button[name=connect]').addEventListener('click', function () {
        const modalbody = dlg_wificred.querySelector('div.modal-body');
        let netclass = null;
        ['scanned', 'manual'].forEach((k) => { if (modalbody.classList.contains(k)) netclass = k; });
        if (netclass == null) {
            console.warn('YUBOX Framework', 'No se encuentra netclass en div.modal-body');
            return;
        }

        let postData = {
            ssid:       dlg_wificred.querySelector('input#ssid').value,
            authmode:   parseInt(dlg_wificred.querySelector((netclass == 'scanned') ? 'input#authmode' : 'select#authmode').value),
        };
        if (netclass == 'scanned') postData['pin'] = dlg_wificred.querySelector('input[name="pin"]:checked').value;
        ((postData.authmode == 5) ? ['identity', 'password'] : (postData.authmode > 0) ? ['psk'] : [])
            .forEach((k) => { postData[k] = dlg_wificred.querySelector('div.form-group input#'+k).value; });

        if (netclass == 'scanned') {
            // Puede ocurrir que la red ya no exista según el escaneo más reciente
            let existe = (Array.from(wifipane.querySelectorAll('table#wifiscan > tbody > tr'))
                .filter((tr) => { return (tr.data['ssid'] == postData.ssid); })
                .length > 0);
            if (!existe) {
                bootstrap.Modal.getOrCreateInstance(dlg_wificred)
                .hide();
                yuboxMostrarAlertText('warning', 'La red '+postData.ssid+' ya no se encuentra disponible', 3000);
                return;
            }
        }

        // La red todavía existe en el último escaneo. Se intenta conectar.
        yuboxFetchMethod(
            (netclass == 'scanned') ? 'PUT' : 'POST',
            'wificonfig',
            (netclass == 'scanned') ? 'connection' : 'networks',
            postData
        ).then((data) => {
            // Credenciales aceptadas, se espera a que se conecte
            if (netclass == 'scanned') {
                marcarRedDesconectandose();
            }
            bootstrap.Modal.getOrCreateInstance(dlg_wificred)
            .hide();
        }, (e) => {
            yuboxStdAjaxFailHandlerDlg(dlg_wificred.querySelector('div.modal-body'), e, 2000);
        });
    });

    // Comportamiento de controles de diálogo de mostrar estado de red conectada
    const dlg_wifiinfo = wifipane.querySelector('div#wifi-details');
    dlg_wifiinfo.querySelector('div.modal-footer button[name=forget]').addEventListener('click', function () {
        yuboxFetchMethod('DELETE', 'wificonfig', 'connection')
        .then((data) => {
            // Credenciales aceptadas, se espera a que se conecte
            marcarRedDesconectandose();
            bootstrap.Modal.getOrCreateInstance(dlg_wifiinfo)
            .hide();
        }, (e) => {
            yuboxStdAjaxFailHandlerDlg(dlg_wifiinfo.querySelector('div.modal-body'), e, 2000);
        });
    });
}

function yuboxWiFi_displayNetworkDialog(netclass, net)
{
    const wifipane = getYuboxPane('wifi', true);
    const dlg_wificred = wifipane.querySelector('div#wifi-credentials');

    // Preparar diálogo para red manual o escaneada
    const modalbody = dlg_wificred.querySelector('div.modal-body');
    modalbody.classList.remove('manual', 'scanned');
    modalbody.classList.add(netclass);

    dlg_wificred.querySelector('h5#wifi-credentials-title').textContent = (netclass == 'scanned') ? net.ssid : 'Agregar red';
    if (netclass == 'scanned') {
        dlg_wificred.querySelector('input#ssid').value = (net.ssid);
        dlg_wificred.querySelector('input#key_mgmt').value = (wifiauth_desc(net.authmode));
        dlg_wificred.querySelector('input#authmode').value = (net.authmode);
        dlg_wificred.querySelector('input#bssid').value = (net.ap[0].bssid);
        dlg_wificred.querySelector('input#channel').value = (net.ap[0].channel);
    } else {
        dlg_wificred.querySelector('input#ssid').value = ('');
    }
    dlg_wificred.querySelector('input[name="pin"]#N').click();

    const sel_authmode = dlg_wificred.querySelector('select#authmode');
    if (net.authmode == 5) {
        // Autenticación WPA-ENTERPRISE
        dlg_wificred.querySelector('div.form-group.wifi-auth-eap input#identity')
            .value = ((net.identity != null) ? net.identity : '');
        dlg_wificred.querySelector('div.form-group.wifi-auth-eap input#password')
            .value = ((net.password != null) ? net.password : '');
        sel_authmode.value = (5);
    } else if (net.authmode > 0) {
        // Autenticación con contraseña
        dlg_wificred.querySelector('div.form-group.wifi-auth-psk input#psk')
            .value = ((net.psk != null) ? net.psk : '');
        sel_authmode.value = (4);
    } else {
        // Red sin autenticación
        sel_authmode.value = (0);
    }
    sel_authmode.dispatchEvent(new Event('change'));
    dlg_wificred.querySelector('div.modal-footer button[name=connect]')
        .textContent = (netclass == 'scanned') ? 'Conectar a WIFI' : 'Guardar';

    bootstrap.Modal.getOrCreateInstance(dlg_wificred, { focus: true })
    .show();
}

function checkValidWifiCred_EAP()
{
    // Activar el botón de enviar credenciales si ambos valores son no-vacíos
    const wifipane = getYuboxPane('wifi', true);
    const dlg_wificred = wifipane.querySelector('div#wifi-credentials');
    var numLlenos = Array.from(dlg_wificred.querySelectorAll('div.form-group.wifi-auth-eap input'))
        .filter(function(inp) { return (inp.value != ''); })
        .length;
    dlg_wificred.querySelector('button[name=connect]').disabled = !(numLlenos >= 2);
}

function checkValidWifiCred_PSK()
{
    // Activar el botón de enviar credenciales si la clave es de al menos 8 caracteres
    const wifipane = getYuboxPane('wifi', true);
    const dlg_wificred = wifipane.querySelector('div#wifi-credentials');
    var psk = dlg_wificred.querySelector('div.form-group.wifi-auth-psk input#psk').value;
    dlg_wificred.querySelector('button[name=connect]').disabled = !(psk.length >= 8);
}

function yuboxWiFi_isTabActive()
{
    return getYuboxNavTab('wifi', true).classList.contains('active');
}

function yuboxWiFi_setupWiFiScanListener()
{
    if (!yuboxWiFi_isTabActive()) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    const wifipane = getYuboxPane('wifi', true);
    if (!!window.EventSource) {
        var sse = new EventSource(yuboxAPI('wificonfig')+'/netscan');
        sse.addEventListener('WiFiScanResult', function (e) {
          var data = JSON.parse(e.data);
          yuboxWiFi_actualizarRedes(data);
        });
        sse.addEventListener('WiFiStatus', function (e) {
            var data = JSON.parse(e.data);
            if (!data.yubox_control_wifi) {
              yuboxMostrarAlertText('warning',
                'YUBOX Now ha cedido control del WiFi a otra librería. El escaneo WiFi podría no refrescarse, o mostrar datos desactualizados.',
                5000);
            }
        });
        sse.addEventListener('error', function (e) {
          mostrarReintentoScanWifi('Se ha perdido conexión con dispositivo para siguiente escaneo');
        });
        wifipane.data['sse'] = sse;
    } else {
        yuboxMostrarAlertText('danger', 'Este navegador no soporta Server-Sent Events, no se puede escanear WiFi.');
    }
}

function yuboxWiFi_actualizarRedes(data)
{
    const wifipane = getYuboxPane('wifi', true);

    data.sort(function (a, b) {
        if (a.connected || a.connfail) return -1;
        if (b.connected || b.connfail) return 1;
        return b.rssi - a.rssi;
    });

    const tbody_wifiscan = wifipane.querySelector('table#wifiscan > tbody');
    while (tbody_wifiscan.firstChild) tbody_wifiscan.removeChild(tbody_wifiscan.firstChild);
    const dlg_wifiinfo = wifipane.querySelector('div#wifi-details');
    var ssid_visible = null;
    if (dlg_wifiinfo.classList.contains('show')) {
        ssid_visible = dlg_wifiinfo.querySelector('input#ssid').value;
    }
    var max_rssi = null;
    data.forEach(function (net) {
        // Buscar en la lista existente la fila de quien tenga el SSID indicado.
        var tr_wifiscan;
        var f = Array.from(tbody_wifiscan.querySelectorAll('tr'))
        .filter(function(tr) { return (tr.data['ssid'] == net.ssid); });
        if (f.length > 0) {
            // Se encontró SSID duplicado. Se asume que primero aparece el más potente
            tr_wifiscan = f[0];
        } else {
            // Primera vez que aparece SSID en lista
            tr_wifiscan = wifipane.data['wifiscan-template'].cloneNode(true);
            tr_wifiscan.data = {};
            for (var k in net) {
                if (['bssid', 'channel', 'rssi'].indexOf(k) == -1) tr_wifiscan.data[k] = net[k];
            }
            tr_wifiscan.data['ap'] = [];
            tbody_wifiscan.appendChild(tr_wifiscan);
        }
        delete f;
        tr_wifiscan.data['ap'].push({
            bssid:      net.bssid,
            channel:    net.channel,
            rssi:       net.rssi
        });
        var wifidata = tr_wifiscan.data;

        // Mostrar dibujo de intensidad de señal a partir de RSSI
        var res = evaluarIntensidadRedWifi(tr_wifiscan.querySelector('td#rssi > svg.wifipower'), wifidata.ap[0].rssi);
        tr_wifiscan.querySelector('td#rssi').title = 'Intensidad de señal: '+res.pwr+' %';

        // Verificar si se está mostrando la red activa en el diálogo
        if (ssid_visible != null && ssid_visible == wifidata.ssid) {
            if (max_rssi == null || max_rssi < wifidata.ap[0].rssi) max_rssi = wifidata.ap[0].rssi;
        }

        // Mostrar estado de conexión y si hay credenciales guardadas
        tr_wifiscan.querySelector('td#ssid').textContent = wifidata.ssid;
        var connlabel = null;
        if (wifidata.connected) {
            connlabel = 'Conectado';
            tr_wifiscan.classList.add('table-success');
        } else if (wifidata.connfail) {
            connlabel = 'Ha fallado la conexión';
            tr_wifiscan.classList.add('table-danger');
        } else if (wifidata.saved) {
            // Si hay credenciales guardadas se muestra que existen
            connlabel = 'Guardada';
        }
        if (connlabel != null) {
            let sm_connlabel = document.createElement('small');
            sm_connlabel.classList.add('form-text', 'text-muted');
            sm_connlabel.textContent = connlabel;
            tr_wifiscan.querySelector('td#ssid').appendChild(sm_connlabel);
        }

        // Mostrar candado según si hay o no autenticación para la red
        tr_wifiscan.querySelector('td#auth').title =
            'Seguridad: ' + wifiauth_desc(wifidata.authmode);
        tr_wifiscan.querySelector('td#auth > svg.wifiauth > path.'+(wifidata.authmode != 0 ? 'locked' : 'unlocked'))
            .style.display = '';

        tr_wifiscan.data = wifidata;
    });

    // Verificar si se está mostrando la red activa en el diálogo
    if (ssid_visible != null && max_rssi != null) {
        var res = evaluarIntensidadRedWifi(dlg_wifiinfo.querySelector('tr#rssi > td > svg.wifipower'), max_rssi);
        dlg_wifiinfo.querySelector('tr#rssi > td.text-muted').textContent = (res.diag + ' ('+res.pwr+' %)');
    }
}

function evaluarIntensidadRedWifi(svg_wifi, rssi)
{
    var diagnostico;
    var barclass;
    var pwr = rssi2signalpercent(rssi);
    if (pwr >= 80) {
        barclass = 'at-least-4bars';
        diagnostico = 'Excelente';
    } else if (pwr >= 60) {
        barclass = 'at-least-3bars';
        diagnostico = 'Buena';
    } else if (pwr >= 40) {
        barclass = 'at-least-2bars';
        diagnostico = 'Regular';
    } else if (pwr >= 20) {
        barclass = 'at-least-1bar';
        diagnostico = 'Débil';
    } else {
        diagnostico = 'Nula';
    }
    svg_wifi.classList.remove('at-least-1bar', 'at-least-2bars', 'at-least-3bars', 'at-least-4bars');
    svg_wifi.classList.add(barclass);

    return { pwr: pwr, diag: diagnostico };
}

function mostrarReintentoScanWifi(msg)
{
    if (!yuboxWiFi_isTabActive()) {
        // El tab de WIFI ya no está visible, no se hace nada
        return;
    }

    const dv = document.createElement('div');
    dv.classList.add('clearfix');

    const spn = document.createElement('span');
    spn.classList.add('float-start');
    spn.textContent = msg;
    dv.appendChild(spn);

    const btn = document.createElement('button');
    btn.classList.add('btn', 'btn-primary', 'float-end');
    btn.textContent = 'Reintentar';
    dv.appendChild(btn);

    const al = yuboxUnwrapJQ(yuboxMostrarAlert('danger', dv));

    btn.addEventListener('click', function () {
        if (al.parentNode !== null) al.parentNode.removeChild(al);
        yuboxWiFi_setupWiFiScanListener();
    });
}

function wifiauth_desc(authmode)
{
    var desc_authmode = [
        '(ninguna)',
        'WEP',
        'WPA-PSK',
        'WPA2-PSK',
        'WPA-WPA2-PSK',
        'WPA2-ENTERPRISE'
    ];

    return (authmode >= 0 && authmode < desc_authmode.length)
        ? desc_authmode[authmode]
        : '(desconocida)';
}

function rssi2signalpercent(rssi)
{
    // El YUBOX ha reportado hasta ahora valores de RSSI de entre -100 hasta 0.
    // Se usa esto para calcular el porcentaje de fuerza de señal
    if (rssi > 0) rssi = 0;
    if (rssi < -100) rssi = -100;
    return rssi + 100;
}

function marcarRedDesconectandose(ssid)
{
    const wifipane = getYuboxPane('wifi', true);
    const tr_connected = wifipane.querySelector('table#wifiscan > tbody > tr.table-success');
    if (tr_connected == null) return;
    if (ssid != null && tr_connected.data['ssid'] != ssid) return;

    tr_connected.classList.remove('table-success');
    tr_connected.classList.add('table-warning');
    tr_connected.querySelector('td#ssid > small.text-muted').textContent = 'Desconectándose';
}
