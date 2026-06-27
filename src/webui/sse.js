const { SSE_TIMEOUT_MS, SSE_CHECK_INTERVAL_MS, SSE_PING_INTERVAL_MS, SSE_MAX_ENTRIES } = CONFIG;
let currentEventSource = null;
let connectionAlive = true;
let lastMessageTime = Date.now();
let intervals = { check: null, ping: null };
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
  if (elapsed > SSE_TIMEOUT_MS && connectionAlive) setConnectionStatus(false);
  else if (elapsed <= SSE_TIMEOUT_MS && !connectionAlive) setConnectionStatus(true);
};
const pingServer = async () => {
  try {
    const resp = await fetch('/api/status', { cache: 'no-cache' });
    if (resp.ok) {
      if (!connectionAlive) {
        console.log('Server back. Reconnecting SSE...');
        if (currentEventSource) currentEventSource.close();
        initSse();
      }
      return true;
    }
  } catch (e) { console.error('pingServer: fetch failed:', e); }
  return false;
};
const startIntervals = () => {
  clearInterval(intervals.check);
  clearInterval(intervals.ping);
  intervals.check = setInterval(checkConnection, SSE_CHECK_INTERVAL_MS);
  intervals.ping = setInterval(() => {
    if (!connectionAlive || Date.now() - lastMessageTime > 3000) pingServer();
  }, SSE_PING_INTERVAL_MS);
};
const millisToTime = (ms, base, baseMs) => {
  if (!base || !baseMs) return '';
  const real = new Date(base.getTime() + (ms - baseMs));
  return real.toLocaleTimeString('ru-RU', { hour:'2-digit', minute:'2-digit', second:'2-digit' });
};
const valveLabel = (v) => ({'in':'input','out':'output','unk':'unknown'})[v] || v || '—';
const formatSseEntry = (e) => {
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
const renderSseLog = () => {
  const container = document.getElementById('sse-log-entries');
  if (!container) return;
  if (APP_STATE.logs.sseRawJson) {
    container.innerHTML = APP_STATE.logs.sseEntries.map(e =>
      `<div class="border-bottom py-1"><pre class="mb-0"><code>${JSON.stringify(e, null, 2)}</code></pre></div>`
    ).join('');
    return;
  }
  const header = `<table class="table table-sm table-bordered mb-0"><thead class="table-dark"><tr><th>Время</th><th>Бюретка</th><th>Объём</th><th>Скорость</th><th>Темп.</th><th>Электрод</th><th>Клапан</th></tr></thead><tbody>`;
  container.innerHTML = `${header}${APP_STATE.logs.sseEntries.map(formatSseEntry).join('')}</tbody></table>`;
};
window.initSse = () => {
  clearInterval(intervals.check);
  clearInterval(intervals.ping);
  const es = new EventSource('/api/events');
  currentEventSource = es;
  lastMessageTime = Date.now();
  startIntervals();
  es.onopen = () => setConnectionStatus(true);
  const handleEvent = (name, parser, handler) => {
    es.addEventListener(name, (e) => {
      try {
        const data = parser(e.data);
        lastMessageTime = Date.now();
        if (!connectionAlive) setConnectionStatus(true);
        handler(data);
      } catch (err) { console.error(`SSE ${name} parse error:`, err); }
    });
  };
  handleEvent('status', JSON.parse, (data) => {
    if (!data.brt) console.error('SSE status: missing brt field', JSON.stringify(data));
    updateUI(data);
    const idle = data.brt?.sts === 'idle';
    ['bc-vol-run-btn', 'bc-sp-run-btn'].forEach(id => {
      const btn = document.getElementById(id);
      if (btn) btn.disabled = !idle;
    });
    if (APP_STATE.logs.sseAutoupdate) {
      APP_STATE.logs.sseEntries = [data, ...APP_STATE.logs.sseEntries].slice(0, SSE_MAX_ENTRIES);
      renderSseLog();
    }
  });
  handleEvent('debug', JSON.parse, (data) => {
    updateDebugUI(data);
  });
  handleEvent('log', JSON.parse, addLogEntry);
  handleEvent('limitsw', JSON.parse, (data) => {
    document.getElementById('hw-limit-min').textContent = data.full ? '✅ Активирован' : '—';
    document.getElementById('hw-limit-max').textContent = data.empty ? '✅ Активирован' : '—';
  });
  es.onerror = (event) => {
    setConnectionStatus(false);
    console.error('SSE connection lost, reconnecting in 3s...', event?.message || event);
    es.close();
    setTimeout(initSse, 3000);
  };
};
window.renderSseLog = renderSseLog;
