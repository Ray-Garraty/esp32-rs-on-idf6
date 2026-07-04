const { STEPPER: CFG } = CONFIG;
const buildStepperCommand = (mode, direction, value) => {
  if (mode === 'steps') return { cmd: 'burette.moveSteps', params: { steps: value, direction, freq: CFG.DEFAULT_FREQ } };
  return { cmd: 'burette.moveToStop', params: { direction, freq: value } };
};
const updateDynamicInput = () => {
  const container = document.getElementById('dynamic-input-container');
  if (!container) return;
  const isSteps = APP_STATE.stepper.mode === 'steps';
  const id = isSteps ? 'stepper-steps-input' : 'stepper-freq-input';
  const label = isSteps ? 'Steps:' : 'Frequency (Hz):';
  const val = isSteps ? CFG.DEFAULT_STEPS : CFG.DEFAULT_FREQ;
  const maxAttr = isSteps ? '' : `max="${CFG.MAX_FREQ}" `;
  container.innerHTML = `
    <div class="input-group" style="width: 200px;">
      <span class="input-group-text fw-bold py-0 px-2">${label}</span>
      <input type="number" id="${id}" class="form-control" value="${val}" min="1" ${maxAttr}style="width: 60px;">
    </div>`;
};
const stepperStartStop = async () => {
  const btn = document.getElementById('stepper-start-stop-btn');
  if (APP_STATE.stepper.busy) {
    if (btn) { btn.disabled = true; btn.textContent = '⏳ Stopping...'; }
    const res = await sendCommand('burette.stop', {});
    if (!res) showUIError('Failed to stop motor');
    else if (res.status === 'error') console.error('stepperStartStop: stop rejected', JSON.stringify(res));
  } else {
    const dir = document.querySelector('input[name="stepper-dir"]:checked')?.value || APP_STATE.stepper.direction;
    const input = document.querySelector('#dynamic-input-container input');
    const val = parseInt(input?.value || '0', 10);
    if (val <= 0) { showUIError('Enter value > 0'); return; }
    if (btn) { btn.disabled = true; btn.textContent = '⏳ Starting...'; }
    const { cmd, params } = buildStepperCommand(APP_STATE.stepper.mode, dir, val);
    const res = await sendCommand(cmd, params);
    if (!res) showUIError(`Command ${cmd} failed`);
    else if (res.status === 'error') console.error(`stepperStartStop: ${cmd} rejected`, JSON.stringify(res));
  }
  if (btn) { btn.disabled = false; }
};
const initStepperControls = () => {
  document.querySelectorAll('input[name="stepper-mode"]').forEach(r =>
    r.addEventListener('change', (e) => {
      APP_STATE.stepper.mode = e.target.value;
      updateDynamicInput();
    })
  );
  document.querySelectorAll('input[name="stepper-dir"]').forEach(r =>
    r.addEventListener('change', (e) => {
      APP_STATE.stepper.direction = e.target.value;
      sendCommand('burette.setDirection', { direction: e.target.value }).then(res => {
        if (res && res.status === 'error') console.error('stepper: setDirection rejected', JSON.stringify(res));
      });
    })
  );
  const btn = document.getElementById('stepper-start-stop-btn');
  if (btn) btn.addEventListener('click', stepperStartStop);
};
const showUIError = (msg) => { console.error(msg); alert(msg); };
window.updateDynamicInput = updateDynamicInput;
window.initStepperControls = initStepperControls;
