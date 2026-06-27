const initTheme = () => {
  const saved = localStorage.getItem('ecotiter-theme') || 'light';
  document.documentElement.setAttribute('data-bs-theme', saved);
  const icon = document.getElementById('theme-icon');
  if (icon) icon.textContent = saved === 'dark' ? '☀️' : '🌙';
};
const toggleTheme = () => {
  const html = document.documentElement;
  const cur = html.getAttribute('data-bs-theme');
  const next = cur === 'dark' ? 'light' : 'dark';
  html.setAttribute('data-bs-theme', next);
  const icon = document.getElementById('theme-icon');
  if (icon) icon.textContent = next === 'dark' ? '☀️' : '🌙';
  localStorage.setItem('ecotiter-theme', next);
};
let _cmdId = 0;
const sendCommand = async (cmd, params) => {
  try {
    const body = { id: ++_cmdId, cmd, ...(params || {}) };
    const res = await fetch('/api/command', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!res.ok) throw new Error(`HTTP ${res.status}: ${await res.text().catch(() => '(no body)')}`);
    return await res.json();
  } catch (err) { console.error(`Command "${cmd}" failed:`, err); return null; }
};
const toggleValve = async () => {
  const newPos = APP_STATE.valve.position === 'input' ? 'output' : 'input';
  const btn = document.getElementById('hw-valve-toggle-btn');
  if (btn) { btn.disabled = true; btn.textContent = '...'; }
  try {
    const res = await fetch('/api/valve/set', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ position: newPos }) });
    if (res.ok) {
      const data = await res.json();
      if (data.status === 'ok') {
        APP_STATE.valve.position = data.data.position;
        const el = document.getElementById('hw-valve'); if (el) el.textContent = data.data.position;
      } else { console.error('toggleValve: server returned error', JSON.stringify(data)); }
    } else { const body = await res.text().catch(() => '(no body)'); console.error(`toggleValve: HTTP ${res.status}: ${body}`); }
  } catch (e) { console.error('toggleValve: network error:', e); }
  finally { if (btn) { btn.disabled = false; btn.textContent = 'Переключить'; } }
};
const loadInitialLogs = () => {
  fetch('/api/logs?limit=20').then(r => {
    if (!r.ok) { console.error(`loadInitialLogs: HTTP ${r.status}`); return null; }
    return r.json();
  }).then(data => {
    if (!data) return;
    if (data.entries) {
      data.entries.forEach(e => {
        const ts = new Date().toLocaleTimeString('ru-RU');
        APP_STATE.logs.messages.unshift(`[${ts}] [${e.level}] ${e.msg}`);
      });
      if (APP_STATE.logs.messages.length > CONFIG.LOG_MAX_ENTRIES) APP_STATE.logs.messages.length = CONFIG.LOG_MAX_ENTRIES;
      renderLogTextarea();
    } else { console.error('loadInitialLogs: unexpected response', JSON.stringify(data)); }
  }).catch(e => console.error('loadInitialLogs: network error:', e));
};
const initApp = () => {
  initTheme();
  updateDynamicInput();
  const ac = document.getElementById('sse-autoupdate-check'); if (ac) ac.checked = APP_STATE.logs.sseAutoupdate;
  const rc = document.getElementById('sse-raw-json'); if (rc) rc.checked = APP_STATE.logs.sseRawJson;
  initSse();
  initStepperControls();
  loadCalibrationStatus();
  loadBuretteCalStatus();
  initSpeedTable();
  loadInitialLogs();
};
document.addEventListener('DOMContentLoaded', initApp);
window.sendCommand = sendCommand;
window.toggleTheme = toggleTheme;
window.toggleValve = toggleValve;
