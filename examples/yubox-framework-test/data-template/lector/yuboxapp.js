function setupLectorTab()
{
    var lectorpane = getYuboxPane('lector', true);
    var data = {
        'sse':  null,
        'chart': null,
    };
    lectorpane.data = data;

    var ctx = lectorpane.querySelector('canvas#atmospheric').getContext('2d');
    var chtData = {
        datasets:   [{
            label:  'Presión',
            borderColor: "rgb(255, 99, 132)",
            backgroundColor: "rgb(255, 99, 132)",
            fill: false,
            data: [],
            yAxisID: 'press'
        },{
            label:  'Temperatura',
            borderColor: "rgb(54, 162, 235)",
            backgroundColor: "rgb(54, 162, 235)",
            fill: false,
            data: [],
            yAxisID: 'temp'
        }]
    };
    var cht = new Chart(ctx, {
        type:       'line',
        data:       chtData,
        options:    {
            responsive: true,
            hoverMode: 'index',
            stacked: false,
            title: {
                display: true,
                text: 'Presión y temperatura'
            },
            scales: {
                xAxes:  [{
                    type:   'time'
                }],
                yAxes:  [{
                    type: 'linear',
                    display: true,
                    position: 'left',
                    id: 'press'
                },{
                    type: 'linear',
                    display: true,
                    position: 'right',
                    id: 'temp'
                }]
            }
        }
    });
    lectorpane.data['chart'] = cht;

    if (!!window.EventSource) {
        var sse = new EventSource(yuboxAPI('lectura')+'/events');
        sse.addEventListener('message', function (e) {
            var data = JSON.parse(e.data);
            yuboxLector_actualizar(new Date(data.ts), data.pressure, data.temperature);
        });
        lectorpane.data['sse'] = sse;
    }
}

function yuboxLector_actualizar(d, p, t)
{
    var lectorpane = getYuboxPane('lector', true);
    lectorpane.querySelector('h3#temp').textContent = t.toFixed(2);
    lectorpane.querySelector('h3#press').textContent = p.toFixed(2);
    var cht = lectorpane.data['chart'];
    cht.data.datasets[0].data.push({x: d, y: p});
    cht.data.datasets[1].data.push({x: d, y: t});
    if (d.valueOf() - cht.data.datasets[0].data[0].x.valueOf() > 10 * 60 * 1000) {
        cht.data.datasets[0].data.shift();
        cht.data.datasets[1].data.shift();
    }
    cht.update();
}
