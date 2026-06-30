// EcoTiter WebUI — SSE event handler and status display
(function () {
    'use strict';

    const statusEl = document.getElementById('device-status');
    const tempDisplay = document.getElementById('temp-display');
    const mvDisplay = document.getElementById('mv-display');
    const valveDisplay = document.getElementById('valve-display');
    const buretteDisplay = document.getElementById('burette-display');
    const wifiDetail = document.getElementById('wifi-detail');

    function updateStatus(data) {
        // Update temperature
        if (data.temp !== null && data.temp !== undefined) {
            tempDisplay.textContent = data.temp.toFixed(1) + ' °C';
        } else {
            tempDisplay.textContent = '-- °C';
        }

        // Update ADC
        if (data.mv !== undefined) {
            mvDisplay.textContent = data.mv + ' mV';
        }

        // Update valve
        if (data.vlv) {
            const label = data.vlv === 'in' ? 'Input' : 'Output';
            valveDisplay.textContent = label;
        }

        // Update burette
        if (data.brt) {
            const sts = data.brt.sts || 'idle';
            const vl = data.brt.vl || 0;
            const spd = data.brt.spd || 0;
            buretteDisplay.textContent = sts + ' (' + vl.toFixed(1) + ' mL @ ' + spd.toFixed(1) + ' mL/min)';
        }

        // Update connection info
        if (data.wifi) {
            const wifi = data.wifi;
            if (wifi.ap_mode) {
                wifiDetail.textContent = 'AP mode (' + wifi.ip + ')';
            } else if (wifi.connected) {
                wifiDetail.textContent = wifi.ssid + ' (' + wifi.rssi + ' dBm)';
            } else {
                wifiDetail.textContent = 'Disconnected';
            }
        }

        // Update device status
        if (data.brt && data.brt.sts) {
            statusEl.innerHTML = '<p class="status-' + data.brt.sts + '">' + data.brt.sts + '</p>';
        }
    }

    // Connect to SSE
    function connectSSE() {
        const source = new EventSource('/api/events');

        source.addEventListener('status', function (e) {
            try {
                const data = JSON.parse(e.data);
                updateStatus(data);
            } catch (err) {
                console.error('SSE parse error:', err);
            }
        });

        source.addEventListener('open', function () {
            console.log('SSE connected');
            statusEl.innerHTML = '<p class="status-ok">Connected</p>';
        });

        source.addEventListener('error', function () {
            console.error('SSE error, reconnecting...');
            statusEl.innerHTML = '<p class="status-error">Reconnecting...</p>';
            if (source.readyState === EventSource.CLOSED) {
                setTimeout(connectSSE, 3000);
            }
        });
    }

    // Initialise on page load
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', connectSSE);
    } else {
        connectSSE();
    }

    // Fallback: fetch /api/status every 5s if SSE fails
    setInterval(function () {
        fetch('/api/status')
            .then(function (r) { return r.json(); })
            .then(function (data) { updateStatus(data); })
            .catch(function (err) { console.error('Status fetch error:', err); });
    }, 5000);
})();
