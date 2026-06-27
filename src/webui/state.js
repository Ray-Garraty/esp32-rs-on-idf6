const CONFIG = {
  LOG_MAX_ENTRIES: 100,
  SSE_MAX_ENTRIES: 5,
  SSE_CHECK_INTERVAL_MS: 500,
  SSE_PING_INTERVAL_MS: 2000,
  SSE_TIMEOUT_MS: 1500,
  STEPPER: {
    DEFAULT_FREQ: 300,
    DEFAULT_STEPS: 1000,
    MAX_FREQ: 1000
  },
  BC: {
    MAX_SPEED_ROWS: 10,
    FREQ_PERCENTAGES: [0.25, 0.50, 0.75]
  }
};
const APP_STATE = {
  stepper: { direction: 'LIQ_OUT', busy: false, enEnabled: false, mode: 'continuous' },
  valve: { position: 'input' },
  logs: { messages: [], sseEntries: [], sseAutoupdate: true, sseRawJson: false, baseDate: null, baseMillis: 0 },
  ui: { sgEditMode: false, motorStoppedAt: null, logLevelFilter: 'ALL' },
  calibration: { speedRowCount: 0, adcState: null, calibratingIndex: -1 }
};
const setState = (path, value, onUpdate = null) => {
  const keys = path.split('.');
  let target = APP_STATE;
  for (let i = 0; i < keys.length - 1; i++) target = target[keys[i]];
  target[keys[keys.length - 1]] = value;
  if (onUpdate) onUpdate();
};
window.APP_STATE = APP_STATE;
window.CONFIG = CONFIG;
