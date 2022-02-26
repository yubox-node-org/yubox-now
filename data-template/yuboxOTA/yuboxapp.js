function setupYuboxOTATab()
{
    const otapane = getYuboxPane('yuboxOTA', true);
    var data = {
        'sse':  null
    };
    otapane.data = data;

    // https://getbootstrap.com/docs/5.1/components/navs-tabs/#events
    getYuboxNavTab('yuboxOTA', true)
    .addEventListener('shown.bs.tab', function (e) {
        // Si el control select de la lista de firmwares está DESACTIVADO,
        // se asume que esta sesión todavía está subiendo algo al equipo.
        if (otapane.querySelector('select#yuboxfirmwarelist').disabled) {
            return;
        }

        yuboxFetch('yuboxOTA', 'firmwarelist.json')
        .then((data) => {
            const sel_firmwarelist = otapane.querySelector('select#yuboxfirmwarelist');
            while (sel_firmwarelist.firstChild) sel_firmwarelist.removeChild(sel_firmwarelist.firstChild);
            for (var i = 0; i < data.length; i++) {
                let opt = document.createElement('option');
                opt.value = data[i].tag;
                opt.textContent = data[i].desc;
                opt.data = data[i];
                sel_firmwarelist.appendChild(opt);
            }
            sel_firmwarelist.value = data[0].tag;
            sel_firmwarelist.dispatchEvent(new Event('change'));
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });

    otapane.querySelector('select#yuboxfirmwarelist').addEventListener('change', function() {
        const opt = this.querySelector('option[value='+this.value+']');
        otapane.querySelectorAll('span.yubox-firmware-desc').forEach((elem) => { elem.textContent = opt.data['desc']; });

        yuboxFetchURL(opt.data['rollback'])
        .then((data) => {
            const spanRB = otapane.querySelector('span#canrollback');
            const btnRB = otapane.querySelector('button[name="rollback"]');

            spanRB.classList.remove('bg-danger', 'bg-success');
            if (data.canrollback) {
                spanRB.classList.add('bg-success');
                spanRB.textContent = 'RESTAURABLE';
                btnRB.disabled = false;
            } else {
                spanRB.classList.add('bg-danger');
                spanRB.textContent = 'NO RESTAURABLE';
                btnRB.disabled = true;
            }
        }, (e) => { yuboxStdAjaxFailHandler(e, 5000); });
    });

    otapane.querySelector('button[name=apply]').addEventListener('click', function () {
        const sel_firmwarelist = otapane.querySelector('select#yuboxfirmwarelist');
        const route_tgzupload = sel_firmwarelist.querySelector('option[value='+sel_firmwarelist.value+']').data['tgzupload'];

        var fi = otapane.querySelector('input[type=file]#tgzupload');
        if (fi.files.length <= 0) {
            yuboxMostrarAlertText('danger', 'Es necesario elegir un archivo tgz para actualización.', 2000);
            return;
        }
        if (typeof FormData == 'undefined') {
            yuboxMostrarAlertText('danger', 'Este navegador no soporta FormData para subida de datos. Actualice su navegador.', 2000);
            return;
        }
        var postData = new FormData();
        postData.append('tgzupload', fi.files[0]);
        yuboxOTAUpload_init();
        yuboxFetchURL(route_tgzupload, postData)
        .then((data) => {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 5000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);

                if (data.reboot) {
                    // Por haber recibido esta indicación, ya se sabe que el
                    // dispositivo está listo para ser reiniciado.
                    yuboxFetch('yuboxOTA', 'reboot', {})
                    .catch((e) => { yuboxStdAjaxFailHandler(e, 5000); })
                }
            } else {
                yuboxMostrarAlertText('danger', data.msg, 6000);
            }
            yuboxOTAUpload_shutdown();
        }, (e) => {
            yuboxStdAjaxFailHandler(e, 5000);
            yuboxOTAUpload_shutdown();
        });
    });
    otapane.querySelector('button[name=rollback]').addEventListener('click', function () {
        const sel_firmwarelist = otapane.querySelector('select#yuboxfirmwarelist');
        const route_rollback = sel_firmwarelist.querySelector('option[value='+sel_firmwarelist.value+']').data['rollback'];

        yuboxOTAUpload_setDisableBtns(true);
        yuboxFetchURL(route_rollback, {})
        .then((data) => {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 4000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);
            } else {
                yuboxMostrarAlertText('danger', data.msg, 2000);
            }
            yuboxOTAUpload_setDisableBtns(false);
        }, (e) => {
            yuboxStdAjaxFailHandler(e, 5000);
            yuboxOTAUpload_setDisableBtns(false);
        });
    });
    otapane.querySelector('button[name=reboot]').addEventListener('click', function () {
        yuboxOTAUpload_setDisableBtns(true);
        yuboxFetch('yuboxOTA', 'reboot', {})
        .then((data) => {
            if (data.success) {
                // Al aplicar actualización debería recargarse más tarde
                yuboxMostrarAlertText('success', data.msg, 4000);
                setTimeout(function () {
                    window.location.reload();
                }, 10 * 1000);
            } else {
                yuboxMostrarAlertText('danger', data.msg, 2000);
            }
            yuboxOTAUpload_setDisableBtns(false);
        }, (e) => {
            yuboxStdAjaxFailHandler(e, 5000);
            yuboxOTAUpload_setDisableBtns(false);
        });
    });

    // Diálogo modal de reporte de hardware
    otapane.querySelector('button[name=hwreport]').addEventListener('click', function () {
        yuboxFetch('yuboxOTA', 'hwreport.json')
        .then((data) => {
            const dlg_hwinfo = otapane.querySelector('div#hwreport');
            const hwtable = dlg_hwinfo.querySelector('table#hwinfo > tbody');

            // Formatos especiales para algunos campos
            data.ARDUINO_ESP32_GIT_VER = data.ARDUINO_ESP32_GIT_VER.toString(16);
            data.EFUSE_MAC = data.EFUSE_MAC.toString(16);
            data.CPU_MHZ = data.CPU_MHZ + ' MHz';
            data.FLASH_SPEED = (data.FLASH_SPEED / 1000000) + ' MHz';

            for (const key in data) {
                hwtable.querySelector('tr#'+key+' > td.text-muted').textContent = data[key];
            }

            bootstrap.Modal.getOrCreateInstance(dlg_hwinfo, { focus: true })
            .show();
        }, (e) => { yuboxStdAjaxFailHandler(e, 2000); });
    });
}

function yuboxOTAUpload_init()
{
    yuboxOTAUpload_setDisableBtns(true);

    const otapane = getYuboxPane('yuboxOTA', true);
    yuboxOTAUpload_setProgressBar(0);
    yuboxOTAUpload_fileProgress('-', '0', '0', '0');
    otapane.querySelectorAll('div.upload-progress').forEach((elem) => { elem.style.display = ''; });

    if (!!window.EventSource) {
        var sse = new EventSource(yuboxAPI('yuboxOTA')+'/events');
        sse.addEventListener('uploadFileStart', function (e) {
            var data = JSON.parse(e.data);
            var totalKB = data.total / 1024.0;
            var currUploadKB = data.currupload / 1024.0;
            yuboxOTAUpload_fileProgress(data.filename, 0, totalKB.toFixed(1), currUploadKB.toFixed(1));
            const pb = otapane.querySelector('div.progress-bar');
            pb.classList.remove('bg-info', 'bg-danger');
            pb.classList.add(data.firmware ? 'bg-danger' : 'bg-info');
            yuboxOTAUpload_setProgressBar(0);
        });
        sse.addEventListener('uploadFileProgress', function (e) {
            var data = JSON.parse(e.data);
            var totalKB = data.total / 1024.0;
            var currKB = data.current / 1024.0;
            var currUploadKB = data.currupload / 1024.0;
            yuboxOTAUpload_fileProgress(data.filename, currKB.toFixed(1), totalKB.toFixed(1), currUploadKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(totalKB > 0.0 ? 100.0 * currKB / totalKB : 0);
        });
        sse.addEventListener('uploadFileEnd', function (e) {
            var data = JSON.parse(e.data);
            var totalKB = data.total / 1024.0;
            var currUploadKB = data.currupload / 1024.0;
            yuboxOTAUpload_fileProgress(data.filename, totalKB.toFixed(1), totalKB.toFixed(1), currUploadKB.toFixed(1));
            yuboxOTAUpload_setProgressBar(100);
        });
        sse.addEventListener('uploadPostTask', function (e) {
            var data = JSON.parse(e.data);
            var msg = data.task;
            var taskDesc = {
                'firmware-commit-start':        'Iniciando commit de firmware nuevo',
                'firmware-commit-failed':       'Falló el commit de firmware nuevo',
                'firmware-commit-end':          'Finalizado commit de firmware nuevo',
                'datafiles-load-oldmanifest':   'Cargando lista de archivos de datos viejos',
                'datafiles-delete-oldbackup':   'Borrando respaldo anterior de archivos de datos',
                'datafiles-rename-oldfiles':    'Respaldando archivos de datos viejos',
                'datafiles-rename-newfiles':    'Instalando archivos de datos nuevos',
                'datafiles-end':                'Fin de instalación de archivos de datos'
            };
            if (taskDesc[data.task] != undefined) msg = taskDesc[data.task];
            yuboxOTAUpload_setProgressBarMessage(100, msg);
        });
        otapane.data['sse'] = sse;
    } else {
      yuboxMostrarAlertText('danger', 'Este navegador no soporta Server-Sent Events, no se puede monitorear upload.');
    }
}

function yuboxOTAUpload_fileProgress(f, c, t, u)
{
    const otapane = getYuboxPane('yuboxOTA', true);
    otapane.querySelector('div.upload-progress span#filename').textContent = f;
    otapane.querySelector('div.upload-progress span#current').textContent = c;
    otapane.querySelector('div.upload-progress span#total').textContent = t;
    otapane.querySelector('div.upload-progress span#currupload').textContent = u;
}

function yuboxOTAUpload_shutdown()
{
    yuboxOTAUpload_setDisableBtns(false);
    const otapane = getYuboxPane('yuboxOTA', true);
    otapane.querySelectorAll('div.upload-progress').forEach((elem) => { elem.style.display = 'none'; });
    if (otapane.data['sse'] != null) {
        otapane.data['sse'].close();
        otapane.data['sse'] = null;
    }
}

function yuboxOTAUpload_setDisableBtns(v)
{
    getYuboxPane('yuboxOTA', true)
    .querySelectorAll('button[name=apply], button[name=rollback], button[name=reboot], select#yuboxfirmwarelist')
    .forEach((elem) => { elem.disabled = v; });
}

function yuboxOTAUpload_setProgressBar(v)
{
    yuboxOTAUpload_setProgressBarMessage(v, v.toFixed(1) + ' %');
}

function yuboxOTAUpload_setProgressBarMessage(v, msg)
{
    const otapane = getYuboxPane('yuboxOTA', true);
    const pb = otapane.querySelector('div.progress-bar');
    pb.style.width = v+'%';
    pb.attributes['aria-valuenow'] = v;
    pb.textContent = msg;
}
