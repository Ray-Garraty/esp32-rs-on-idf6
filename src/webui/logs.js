const LOG_LEVEL_REGEX = /\[(\w+)\]/;
const extractLevel = (entry) => LOG_LEVEL_REGEX.exec(entry)?.[1] || 'INFO';
const getFilteredLogs = (messages, filter) =>
  filter === 'ALL' ? messages : messages.filter((e) => extractLevel(e) === filter);
const addLogEntryData = (entries, data, maxLen = CONFIG.LOG_MAX_ENTRIES) => {
  const ts = new Date().toLocaleTimeString('ru-RU');
  const newEntries = [`[${ts}] [${data.level || 'INFO'}] ${data.msg || ''}`, ...entries];
  return newEntries.length > maxLen ? newEntries.slice(0, maxLen) : newEntries;
};
const renderLogTextarea = () => {
  const el = document.getElementById('log-messages');
  if (!el) return;
  el.value = getFilteredLogs(APP_STATE.logs.messages, APP_STATE.ui.logLevelFilter).join('\n');
  el.scrollTop = 0;
};
const setLogLevelFilter = (level) => {
  APP_STATE.ui.logLevelFilter = level;
  renderLogTextarea();
};
const addLogEntry = (data) => {
  APP_STATE.logs.messages = addLogEntryData(APP_STATE.logs.messages, data);
  renderLogTextarea();
};
const clearLogs = () => {
  APP_STATE.logs.messages = [];
  const el = document.getElementById('log-messages');
  if (el) el.value = '';
};
const downloadLogs = () => {
  fetch('/api/logs/download')
    .then(r => r.text())
    .then(text => {
      const blob = new Blob([text], { type: 'text/plain' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url; a.download = 'ecotiter-logs.txt'; a.click();
      URL.revokeObjectURL(url);
    })
    .catch(e => console.error("downloadLogs error:", e));
};
const toggleSseSetting = (stateKey, checkboxId, renderFn = null) => {
  APP_STATE.logs[stateKey] = !APP_STATE.logs[stateKey];
  const el = document.getElementById(checkboxId);
  if (el) el.checked = APP_STATE.logs[stateKey];
  if (renderFn) renderFn();
};
const toggleSseAutoupdate = () => toggleSseSetting('sseAutoupdate', 'sse-autoupdate-check');
const toggleSseRawJson = () => toggleSseSetting('sseRawJson', 'sse-raw-json', renderSseLog);
window.setLogLevelFilter = setLogLevelFilter;
window.addLogEntry = addLogEntry;
window.clearLogs = clearLogs;
window.downloadLogs = downloadLogs;
window.toggleSseAutoupdate = toggleSseAutoupdate;
window.toggleSseRawJson = toggleSseRawJson;
