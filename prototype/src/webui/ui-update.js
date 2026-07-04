const updateSgThresholdUI = () => {
  const input = document.getElementById('stepperDrv-sg-threshold');
  const btn = document.getElementById('stepperDrv-sg-threshold-btn');
  if (!input || !btn) return;
  if (APP_STATE.ui.sgEditMode) {
    input.removeAttribute('readonly');
    btn.textContent = 'Save'; btn.className = 'btn btn-sm btn-success ms-1';
  } else {
    input.setAttribute('readonly', '');
    input.blur();
    btn.textContent = 'Edit'; btn.className = 'btn btn-sm btn-primary ms-1';
  }
};
const toggleSgThresholdEdit = async () => {
  const input = document.getElementById('stepperDrv-sg-threshold');
  if (!APP_STATE.ui.sgEditMode) {
    APP_STATE.ui.sgEditMode = true; updateSgThresholdUI();
  } else {
    const val = parseInt(input.value, 10);
    if (isNaN(val) || val < 0 || val > 255) { alert('Threshold: 0–255'); return; }
    const btn = document.getElementById('stepperDrv-sg-threshold-btn');
    if (btn) btn.disabled = true;
    await sendCommand('stallGuard.setThreshold', { threshold: val });
    APP_STATE.ui.sgEditMode = false; updateSgThresholdUI();
    if (btn) btn.disabled = false;
  }
};
const updateStepperUI = (stepper) => {
  const isMoving = stepper?.status === 'busy' || stepper?.status === 'moving' || stepper?.status === 'working';
  APP_STATE.stepper.busy = isMoving;
  APP_STATE.stepper.enEnabled = stepper?.isEnabled ?? false;
  const set = (id, val, cls) => { const el = document.getElementById(id); if (el) { el.textContent = val; if (cls) el.className = cls; } };
  const hide = (id) => { const el = document.getElementById(id); if (el) el.style.display = 'none'; };
  const show = (id) => { const el = document.getElementById(id); if (el) el.style.display = ''; };
  set('stepperDrv-en-pin', APP_STATE.stepper.enEnabled ? 'LOW' : 'HIGH', APP_STATE.stepper.enEnabled ? 'badge bg-success ms-1' : 'badge bg-secondary ms-1');
  const btn = document.getElementById('stepper-start-stop-btn');
  if (btn) { btn.textContent = isMoving ? '⏹ Stop' : '▶ Start'; btn.className = isMoving ? 'btn btn-danger' : 'btn btn-success'; }
  isMoving ? show('stepper-spinner') : hide('stepper-spinner');
  const inp = document.querySelector('#dynamic-input-container input');
  if (inp) inp.disabled = isMoving;
  document.querySelectorAll('input[name="stepper-mode"]').forEach(r => r.disabled = isMoving);
  if (stepper?.direction) {
    APP_STATE.stepper.direction = stepper.direction;
    const isOut = stepper.direction === 'LIQ_OUT';
    const d = document.getElementById('dir-down'); const u = document.getElementById('dir-up');
    if (d) d.checked = !isOut; if (u) u.checked = isOut;
  }
  if (stepper?.frequency != null && stepper?.status === 'moving') {
    const f = document.getElementById('stepper-freq-input');
    if (f) f.value = stepper.frequency;
  }
};
let __prevDebug = {};
let __connUsb = false;
let __connBle = false;
const updateConnectionStatus = () => {
  const el = document.getElementById('hw-connection');
  if (!el) return;
  if (__connBle) el.innerHTML = '<span class="badge bg-success">BLE</span>';
  else if (__connUsb) el.innerHTML = '<span class="badge bg-success">USB (Tauri)</span>';
  else el.textContent = '—';
};
const updateDebugUI = (data) => {
  const prev = __prevDebug;
  const cur = {
    adcRawMv: data.adc?.raw_mv,
    usbConnected: data.usbSerialConnected,
    bleConnected: data.bleConnected,
    drvConnected: data.stepperDrv?.isConnected,
    otpw: data.stepperDrv?.otpw,
    ot: data.stepperDrv?.ot,
    sgValue: data.stepperDrv?.motor?.stallGuard?.value,
    isStalled: data.stepperDrv?.motor?.stallGuard?.isStalled,
    isMoving: data.stepperDrv?.motor?.isMoving,
    sgThreshold: data.stepperDrv?.motor?.stallGuard?.threshold,
    stepsTaken: data.buretteSteps?.taken
  };
  const diff = (key, val) => cur[key] !== prev[key];
  const set = (id, v) => { const el = document.getElementById(id); if (el) el.textContent = v ?? '—'; };
  if (diff('adcRawMv')) set('hw-adc', cur.adcRawMv);
  if (diff('usbConnected')) { __connUsb = cur.usbConnected; updateConnectionStatus(); }
  if (diff('bleConnected')) { __connBle = cur.bleConnected; updateConnectionStatus(); }
  if (diff('drvConnected')) {
    const el = document.getElementById('stepperDrv-status-icon');
    if (el) { el.className = cur.drvConnected ? 'badge bg-success ms-2' : 'badge bg-danger ms-2'; el.textContent = cur.drvConnected ? '✓' : '✗'; }
  }
  if (diff('ot') || diff('otpw') || diff('drvConnected')) {
    const el = document.getElementById('stepperDrv-overheat');
    if (el) {
      el.style.display = cur.drvConnected ? '' : 'none';
      if (cur.ot) el.innerHTML = '<i class="bi bi-thermometer-high fs-5 ms-1" style="color:#dc3545"></i>';
      else if (cur.otpw) el.innerHTML = '<i class="bi bi-thermometer-half fs-5 ms-1" style="color:#ffc107"></i>';
      else el.innerHTML = '<i class="bi bi-thermometer fs-5 ms-1" style="color:#198754"></i>';
    }
  }
  if (diff('sgValue')) set('stepperDrv-sg-result', cur.sgValue);
  if (diff('isStalled')) set('stepperDrv-stall', cur.isStalled ? '⚠️ Yes' : '✅ No');
  if (diff('isMoving')) set('stepperDrv-motor-busy', cur.isMoving ? 'Yes' : 'No');
  if (diff('sgThreshold')) {
    const inp = document.getElementById('stepperDrv-sg-threshold');
    if (inp && !APP_STATE.ui.sgEditMode) inp.value = cur.sgThreshold ?? '';
  }
  if (diff('stepsTaken')) set('stepper-actual-steps', cur.stepsTaken);
  __prevDebug = { ...cur };
};
const updateUI = (data) => {
  if (!APP_STATE.logs.baseDate && data.ts) {
    APP_STATE.logs.baseDate = new Date(); APP_STATE.logs.baseMillis = data.ts;
  }
  if (data.temp !== undefined) {
    const el = document.getElementById('hw-temperature');
    if (el) {
      if (data.temp !== null) el.textContent = `${data.temp.toFixed(1)} °C`;
      else el.textContent = 'DS18B20 not connected';
    }
  }
  const el = document.getElementById('hw-electrode');
  if (el) el.textContent = data.mv != null ? data.mv : '—';
  if (data.usbSerialConnected !== undefined) { __connUsb = data.usbSerialConnected; updateConnectionStatus(); }
  if (data.bleConnected !== undefined) { __connBle = data.bleConnected; updateConnectionStatus(); }
  if (data.vlv) {
    APP_STATE.valve.position = ({'in':'input','out':'output','unk':'unknown'})[data.vlv] || data.vlv;
    const el = document.getElementById('hw-valve');
    if (el) el.textContent = APP_STATE.valve.position;
  }
  if (data.brt) {
    updateStepperUI({
      ...(data.burette || {}),
      status: data.brt.sts,
      volume_ml: data.brt.vl,
      speed_ml_min: data.brt.spd
    });
  }
};
window.updateUI = updateUI;
window.updateDebugUI = updateDebugUI;
window.updateSgThresholdUI = updateSgThresholdUI;
window.toggleSgThresholdEdit = toggleSgThresholdEdit;
