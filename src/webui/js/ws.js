/**
 * @file ws.js
 * @brief WebSocket connection & state synchronization
 */
const { WS_TIMEOUT_MS, WS_CHECK_INTERVAL_MS, WS_PING_INTERVAL_MS, WS_RECONNECT_DELAY_MS, WS_PING_THRESHOLD_MS, WS_MAX_ENTRIES } = CONFIG;
let currentWs = null;
let connectionAlive = true;
let lastMessageTime = Date.now();
let intervals = { check: null, ping: null };
let isReconnecting = false;
let reconnectTimer = null;

const scheduleReconnect = () => {
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(initWs, WS_RECONNECT_DELAY_MS);
};

const cancelReconnect = () => {
    if (reconnectTimer !== null) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
};

const setConnectionStatus = (alive) => {
  connectionAlive = alive;
  const el = document.getElementById('connection-status');
  if (el) {
    el.className = `badge bg-${alive ? 'success' : 'danger'}`;
    el.innerHTML = `<i class="bi bi-wifi${alive ? '' : '-off'}"></i>`;
  }
  ['hw-valve-toggle-btn','stepper-start-stop-btn','logs-download-btn','dir-down','dir-up'].forEach(id => {
    const btn = document.getElementById(id);
    if (btn) btn.disabled = !alive;
  });
  document.querySelectorAll('label[for="dir-down"], label[for="dir-up"]').forEach(l => {
    l.style.pointerEvents = alive ? 'auto' : 'none';
    l.classList.toggle('disabled', !alive);
  });
  if (!alive) resetUIValues();
};

const resetUIValues = () => {
  const fields = {
    'hw-temperature': '—', 'hw-adc': '—', 'hw-valve': '—',
    'hw-limit-min': '—', 'hw-limit-max': '—', 'hw-connection': '—',
    'stepperDrv-status-icon': '✗', 'stepperDrv-sg-result': '—',
    'stepperDrv-stall': '—', 'stepperDrv-motor-busy': '—', 'stepper-actual-steps': '0'
  };
  Object.entries(fields).forEach(([id, val]) => {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  });
};

const checkConnection = () => {
  const elapsed = Date.now() - lastMessageTime;
  if (elapsed > WS_TIMEOUT_MS && connectionAlive) setConnectionStatus(false);
  else if (elapsed <= WS_TIMEOUT_MS && !connectionAlive) setConnectionStatus(true);
};

const pingServer = async () => {
  try {
    const resp = await fetch('/api/status', { cache: 'no-cache' });
    if (resp.ok) {
      if (!connectionAlive && !isReconnecting) {
        console.log('Server back. Reconnecting WS...');
        if (currentWs && currentWs.readyState === WebSocket.OPEN) currentWs.close();
        initWs();
      }
      return true;
    }
  } catch (e) { console.error('pingServer: fetch failed:', e); }
  return false;
};

const startIntervals = () => {
  clearInterval(intervals.check);
  clearInterval(intervals.ping);
  intervals.check = setInterval(checkConnection, WS_CHECK_INTERVAL_MS);
  intervals.ping = setInterval(() => {
    if (!connectionAlive || Date.now() - lastMessageTime > WS_PING_THRESHOLD_MS) pingServer();
  }, WS_PING_INTERVAL_MS);
};

const millisToTime = (ms, base, baseMs) => {
  if (!base || !baseMs) return '';
  const real = new Date(base.getTime() + (ms - baseMs));
  return real.toLocaleTimeString('ru-RU', { hour:'2-digit', minute:'2-digit', second:'2-digit' });
};

const valveLabel = (v) => ({'in':'input','out':'output','unk':'unknown'})[v] || v || '—';

const formatWsEntry = (e) => {
  const ts = e.ts ? millisToTime(e.ts, APP_STATE.logs.baseDate, APP_STATE.logs.baseMillis) : '—';
  const b = e.brt;

  const cell = (content, title = '') => `<td class="text-nowrap small" ${title ? `title="${title}"` : ''}>${content}</td>`;
  const buretteStatus = b ? (b.sts === 'working'
    ? '<span class="badge bg-warning text-dark">движется</span>'
    : '<span class="badge bg-success">ожидание</span>') : '—';
  const vol = b?.vl != null ? `${b.vl.toFixed(2)} мл` : '—';
  const speed = b?.spd != null ? `${b.spd.toFixed(1)} мл/мин` : '—';
  const tempStr = e.temp != null ? `${e.temp.toFixed(1)}°C` : '—';
  const mvStr = e.mv != null ? `${Number(e.mv).toFixed(1)} мВ` : '—';

  return `<tr>${cell(ts)}${cell(buretteStatus)}${cell(vol, 'Объём')}${cell(speed, 'Скорость')}${cell(tempStr)}${cell(mvStr)}${cell(valveLabel(e.vlv))}</tr>`;
};

const renderWsLog = () => {
  const container = document.getElementById('ws-log-entries');
  if (!container) return;
  if (APP_STATE.logs.wsRawJson) {
    container.innerHTML = APP_STATE.logs.wsEntries.map(e =>
      `<div class="border-bottom py-1"><pre class="mb-0"><code>${JSON.stringify(e, null, 2)}</code></pre></div>`
    ).join('');
    return;
  }
  const header = `<table class="table table-sm table-bordered mb-0"><thead class="table-dark"><tr><th>Время</th><th>Бюретка</th><th>Объём</th><th>Скорость</th><th>Темп.</th><th>Электрод</th><th>Клапан</th></tr></thead><tbody>`;
  container.innerHTML = `${header}${APP_STATE.logs.wsEntries.map(formatWsEntry).join('')}</tbody></table>`;
};

window.initWs = () => {
  cancelReconnect();

  // Detach old socket handlers and close if still alive
  if (currentWs) {
    const old = currentWs;
    old.onopen = null;
    old.onmessage = null;
    old.onclose = null;
    old.onerror = null;
    if (old.readyState === WebSocket.OPEN || old.readyState === WebSocket.CONNECTING) {
      old.close();
    }
    currentWs = null;
  }

  isReconnecting = true;
  clearInterval(intervals.check);
  clearInterval(intervals.ping);
  const ws = new WebSocket('ws://' + location.host + '/ws/stream');
  currentWs = ws;
  lastMessageTime = Date.now();

  // Connection timeout: force close if stuck in CONNECTING
  const connectTimeout = setTimeout(() => {
    if (ws === currentWs && ws.readyState === WebSocket.CONNECTING) {
      console.warn('WS connection timeout, forcing close');
      ws.close();
      scheduleReconnect();
    }
  }, WS_TIMEOUT_MS + 2000);

  ws.onopen = () => {
    clearTimeout(connectTimeout);
    lastMessageTime = Date.now();
    setConnectionStatus(true);
    isReconnecting = false;
    startIntervals();
    ws.send('{}'); // trigger server-side session creation (push-only architecture)
  };

  ws.onmessage = (event) => {
    try {
      const envelope = JSON.parse(event.data);
      lastMessageTime = Date.now();
      if (!connectionAlive) setConnectionStatus(true);

      switch (envelope.event) {
        case 'status': {
          const data = envelope.data;
          if (!data.brt) console.error('WS status: missing brt field', JSON.stringify(data));
          if (data.temp === undefined) console.error('WS status: missing temp field');
          if (data.mv === undefined) console.error('WS status: missing mv field');
          if (data.vlv === undefined) console.error('WS status: missing vlv field');
          updateUI(data);
          const idle = data.brt?.sts === 'idle';
          ['bc-vol-run-btn', 'bc-sp-run-btn'].forEach(id => {
            const btn = document.getElementById(id);
            if (btn) btn.disabled = !idle;
          });
          if (APP_STATE.logs.wsAutoupdate) {
            APP_STATE.logs.wsEntries = [data, ...APP_STATE.logs.wsEntries].slice(0, WS_MAX_ENTRIES);
            renderWsLog();
          }
          break;
        }
        case 'debug': {
          const data = envelope.data;
          if (!data.stepperDrv) console.error('WS debug: missing stepperDrv field', JSON.stringify(data));
          if (!data.buretteSteps) console.warn('WS debug: missing buretteSteps field');
          updateDebugUI(data);
          break;
        }
        case 'log':
          addLogEntry(envelope.data);
          break;
        case 'limitsw': {
          const data = envelope.data;
          if (data.full === undefined || data.empty === undefined) {
            console.error('WS limitsw: missing full/empty fields', JSON.stringify(data));
          }
          document.getElementById('hw-limit-min').textContent = data.full ? '✅ Активирован' : '—';
          document.getElementById('hw-limit-max').textContent = data.empty ? '✅ Активирован' : '—';
          break;
        }
        default:
          break;
      }
    } catch (err) { console.error('WS message parse error:', err); }
  };

  ws.onclose = () => {
    clearTimeout(connectTimeout);
    setConnectionStatus(false);
    console.error('WS connection lost, reconnecting in 3s...');
    scheduleReconnect();
  };

  ws.onerror = () => {
    console.error('WS error');
    if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
      ws.close();
    }
    scheduleReconnect();
  };
};

window.renderWsLog = renderWsLog;
