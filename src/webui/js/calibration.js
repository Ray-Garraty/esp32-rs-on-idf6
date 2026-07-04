/**
 * @file calibration.js
 * @brief ADC & burette calibration UI
 */
const ADC_CAL_REFS = [0, -177.5, 177.5, 350.0, -350.0];
const BURETTE_IDLE_TIMEOUT_MS = 60000;
const BURETTE_POLL_INTERVAL_MS = 500;

const updateCalibrationUI = (state) => {
  if (!state) return;

  const badge = document.getElementById("adc-cal-status-badge");
  if (badge) {
    if (state.is_default) {
      badge.textContent = "○ Default";
      badge.className = "badge bg-secondary ms-2";
    } else {
      badge.textContent = "● Custom";
      badge.className = "badge bg-success ms-2";
    }
  }

  document.getElementById("adc-cal-a").textContent = state.a?.toFixed(6) ?? "—";
  document.getElementById("adc-cal-b").textContent = state.b?.toFixed(6) ?? "—";
  document.getElementById("adc-cal-r2").textContent = state.r_squared?.toFixed(4) ?? "—";
  document.getElementById("adc-cal-date").textContent = state.calibrated_at ?? "—";

  const tbody = document.getElementById("adc-cal-points-body");
  if (!tbody) return;

  const points = state.points || [];
  tbody.innerHTML = "";

  ADC_CAL_REFS.forEach((ref, i) => {
    const pt = points[i] || {};
    const raw = pt.collected ? (pt.raw_mv?.toFixed(1) ?? "—") : "—";
    const isCollected = pt.collected;
    const isMeasuring = APP_STATE.calibration.calibratingIndex === i;

    const tr = document.createElement("tr");
    const btnHtml = isMeasuring
      ? '<span class="spinner-border spinner-border-sm"></span> Calibrating...'
      : isCollected
        ? "✅"
        : `<button class="btn btn-sm btn-outline-primary" onclick="calibratePoint(${i}, ${ref})">Calibrate</button>`;

    tr.innerHTML = `
      <td class="text-center">${i + 1}</td>
      <td class="font-monospace text-center">${ref >= 0 ? "+" : ""}${ref}</td>
      <td class="font-monospace text-center" id="adc-cal-point-${i}">${raw}</td>
      <td class="text-center">${btnHtml}</td>
    `;
    tbody.appendChild(tr);
  });

  const computeBtn = document.getElementById("adc-cal-compute-btn");
  if (computeBtn) {
    const collectedCount = points.filter((p) => p?.collected).length;
    computeBtn.disabled = collectedCount < 5;
    computeBtn.onclick = collectedCount >= 5 ? computeCalibration : null;
  }

  const resetBtn = document.getElementById("adc-cal-reset-btn");
  if (resetBtn) resetBtn.disabled = state.is_default;
};

const calibratePoint = async (index, refMv) => {
  APP_STATE.calibration.calibratingIndex = index;
  updateCalibrationUI(APP_STATE.calibration.adcState);

  await sendCommand("adc.cal.measure", { ref_mv: refMv });

  APP_STATE.calibration.calibratingIndex = -1;
  await loadCalibrationStatus();
};

const computeCalibration = async () => {
  const btn = document.getElementById("adc-cal-compute-btn");
  if (btn) {
    btn.disabled = true;
    btn.textContent = "⏳ Computing...";
  }

  const result = await sendCommand("adc.cal.compute", {});

  if (result?.status === "ok") {
    const a = result.data?.a ?? 0;
    const b = result.data?.b ?? 0;
    const r2 = result.data?.r_squared ?? 0;
    const ok = window.confirm(
      `New calibration coefficients:\n\na = ${a.toFixed(6)}\nb = ${b.toFixed(6)}\nR² = ${r2.toFixed(4)}\n\nSave?`
    );
    if (ok) {
      await sendCommand("adc.cal.save", {});
      window.alert("Calibration saved successfully");
    }
    await loadCalibrationStatus();
  }

  if (btn) {
    btn.disabled = false;
    btn.textContent = "🧮 Compute & Save";
  }
};

const resetCalibration = async () => {
  if (!window.confirm("ADC calibration will be reset. Continue?")) return;
  await sendCommand("adc.cal.reset", {});
  await loadCalibrationStatus();
};

const loadCalibrationStatus = async () => {
  const result = await sendCommand("adc.cal.get", {});
  if (result?.status === "ok" && result?.data) {
    APP_STATE.calibration.adcState = result.data;
    updateCalibrationUI(result.data);
  }
};

// ─── Burette calibration ───

const loadBuretteCalStatus = async () => {
  const result = await sendCommand("burette.cal.get", {});
  if (result?.status === "ok" && result?.data) {
    updateBuretteCalUI(result.data);
  }
};

const updateBuretteCalUI = (state) => {
  if (!state) return;

  const badge = document.getElementById("bc-cal-status-badge");
  if (badge) {
    if (state.is_default) {
      badge.textContent = "○ Default";
      badge.className = "badge bg-secondary ms-2";
    } else {
      badge.textContent = "● Custom";
      badge.className = "badge bg-success ms-2";
    }
  }
  const spBadge = document.getElementById("bc-sp-cal-status-badge");
  if (spBadge) {
    if (state.is_default) {
      spBadge.textContent = "○ Default";
      spBadge.className = "badge bg-secondary ms-2";
    } else {
      spBadge.textContent = "● Custom";
      spBadge.className = "badge bg-success ms-2";
    }
  }

  const sps = document.getElementById("bc-steps-per-ml");
  if (sps) sps.textContent = state.steps_per_ml?.toFixed(1) ?? "—";
  const nom = document.getElementById("bc-nominal-vol");
  if (nom) nom.textContent = state.nominal_vol?.toFixed(2) ?? "—";
  const coeff = document.getElementById("bc-speed-coeff");
  if (coeff) coeff.textContent = state.speed_coeff?.toFixed(5) ?? "—";
  const range = document.getElementById("bc-freq-range");
  if (range) range.textContent = (state.min_freq != null && state.max_freq != null)
    ? `${state.min_freq} – ${state.max_freq}` : "—";

  const freqInput = document.getElementById("bc-vol-freq");
  if (freqInput && state.max_freq != null) {
    const def = Math.round(state.max_freq / 2);
    freqInput.placeholder = String(def);
    if (!freqInput.value) freqInput.value = def;
  }

  const resetBtn = document.getElementById("bc-vol-reset-btn");
  if (resetBtn) resetBtn.disabled = !!state.is_default;
  const spResetBtn = document.getElementById("bc-sp-reset-btn");
  if (spResetBtn) spResetBtn.disabled = !!state.is_default;

  if (state.max_freq != null) {
    const pcts = CONFIG.BC.FREQ_PERCENTAGES;
    document.querySelectorAll("#bc-speed-body tr").forEach((tr, i) => {
      const freqInput = tr.querySelector(".bc-sp-freq");
      if (freqInput && !freqInput.value) {
        freqInput.value = Math.round(state.max_freq * (pcts[i] || 0.5));
      }
    });
  }

  const dateEl = document.getElementById("bc-cal-date");
  if (dateEl) {
    if (state.calibration_date > 0) {
      const d = new Date(state.calibration_date * 1000);
      dateEl.textContent = d.toLocaleString('ru-RU', { timeZone: 'Europe/Moscow' });
    } else {
      dateEl.textContent = "—";
    }
  }

  const spDateEl = document.getElementById("bc-sp-cal-date");
  if (spDateEl) {
    if (state.calibration_date > 0) {
      const d = new Date(state.calibration_date * 1000);
      spDateEl.textContent = d.toLocaleString('ru-RU', { timeZone: 'Europe/Moscow' });
    } else {
      spDateEl.textContent = "—";
    }
  }
};

const waitForBuretteIdle = async (timeoutMs = BURETTE_IDLE_TIMEOUT_MS) => {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const resp = await fetch("/api/status");
      const data = await resp.json();
      if (data?.brt?.sts === "idle") return true;
    } catch (e) {
      console.error("waitForBuretteIdle fetch error:", e);
    }
    await new Promise((r) => setTimeout(r, BURETTE_POLL_INTERVAL_MS));
  }
  throw new Error("Timeout waiting for burette idle");
};

const runVolumeCalibration = async () => {
  const freq = parseInt(document.getElementById("bc-vol-freq")?.value);
  if (!freq || freq <= 0) { window.alert("Enter frequency"); return; }

  const btn = document.getElementById("bc-vol-run-btn");
  const spinner = document.getElementById("bc-vol-run-spinner");
  const btnText = document.getElementById("bc-vol-run-text");
  if (btn) btn.disabled = true;
  if (spinner) spinner.classList.remove("d-none");
  if (btnText) btnText.textContent = " Filling...";

  console.log(`[BC] Sending burette.cal.run {mode:"dose", freq_hz:${freq}}`);
  const result = await sendCommand("burette.cal.run", { mode: "dose", freq_hz: freq });
  console.log("[BC] burette.cal.run response:", JSON.stringify(result));

  if (result?.status === "ok") {
    await waitForBuretteIdle();
    const calResult = await sendCommand("burette.cal.getResult", {});
    console.log("[BC] burette.cal.getResult response:", JSON.stringify(calResult));
    const runResult = document.getElementById("bc-vol-run-result");
    if (runResult) runResult.style.display = "block";
    const stepsEl = document.getElementById("bc-vol-steps");
    if (stepsEl) stepsEl.textContent = calResult?.data?.steps_taken ?? "—";
    const calcBtn = document.getElementById("bc-vol-calc-btn");
    if (calcBtn) calcBtn.disabled = false;
  } else {
    console.warn("[BC] burette.cal.run rejected:", result);
  }
  if (spinner) spinner.classList.add("d-none");
  if (btnText) btnText.textContent = "▶ Run";
  if (btn) btn.disabled = false;
};

const calcVolumeCalibration = async () => {
  const mass = parseFloat(document.getElementById("bc-vol-mass")?.value || "0");
  const temp = parseFloat(document.getElementById("bc-vol-temp")?.value || String(CONFIG.BC.DEFAULT_TEMP_C));
  const pressure = parseFloat(document.getElementById("bc-vol-pressure")?.value || String(CONFIG.BC.DEFAULT_PRESSURE_KPA));
  if (mass <= 0) { window.alert("Enter mass"); return; }

  const result = await sendCommand("burette.cal.calcVolume", { mass_g: mass, temp_c: temp, pressure_kpa: pressure });
  if (result?.status === "ok" && result?.data) {
    const d = result.data;
    document.getElementById("bc-vol-z-factor").textContent = d.z_factor?.toFixed(6) ?? "—";
    document.getElementById("bc-vol-actual").textContent = d.actual_volume_ml?.toFixed(4) ?? "—";
    document.getElementById("bc-vol-new-sps").textContent = d.new_steps_per_ml?.toFixed(1) ?? "—";
    document.getElementById("bc-vol-error").textContent = d.relative_error_pct != null ? `${d.relative_error_pct.toFixed(2)}%` : "—";
    document.getElementById("bc-vol-calc-result").style.display = "block";
    document.getElementById("bc-vol-save-btn").disabled = false;
  }
};

const saveBuretteCal = async () => {
  const result = await sendCommand("burette.cal.save", {});
  if (result?.status === "ok") {
    window.alert("Calibration saved");
    const runResult = document.getElementById("bc-vol-run-result");
    if (runResult) runResult.style.display = "none";
    const calcResult = document.getElementById("bc-vol-calc-result");
    if (calcResult) calcResult.style.display = "none";
    const calcBtn = document.getElementById("bc-vol-calc-btn");
    if (calcBtn) calcBtn.disabled = true;
    const saveBtn = document.getElementById("bc-vol-save-btn");
    if (saveBtn) saveBtn.disabled = true;
    const spCalcResult = document.getElementById("bc-sp-calc-result");
    if (spCalcResult) spCalcResult.style.display = "none";
    const spSaveBtn = document.getElementById("bc-sp-save-btn");
    if (spSaveBtn) spSaveBtn.disabled = true;
    await loadBuretteCalStatus();
  }
};

const resetBuretteCal = async () => {
  if (!window.confirm("Burette calibration will be reset. Continue?")) return;
  await sendCommand("burette.cal.reset", {});
  await loadBuretteCalStatus();
};

const initSpeedTable = () => {
  const tbody = document.getElementById("bc-speed-body");
  if (!tbody) return;
  APP_STATE.calibration.speedRowCount = 0;
  tbody.innerHTML = "";
  for (let i = 0; i < 3; i++) addSpeedRow();
};

const addSpeedRow = () => {
  if (APP_STATE.calibration.speedRowCount >= CONFIG.BC.MAX_SPEED_ROWS) return;
  const tbody = document.getElementById("bc-speed-body");
  if (!tbody) return;
  const idx = APP_STATE.calibration.speedRowCount;
  APP_STATE.calibration.speedRowCount++;
  const tr = document.createElement("tr");
  tr.id = `bc-sp-row-${idx}`;
  tr.innerHTML = `
    <td class="text-center">${idx + 1}</td>
    <td><input type="number" class="form-control form-control-sm text-center bc-sp-freq" data-idx="${idx}" value="" min="1" step="1"></td>
    <td><input type="number" class="form-control form-control-sm text-center bc-sp-speed" data-idx="${idx}" value="" min="0" step="0.1" readonly></td>
    <td><input type="number" class="form-control form-control-sm text-center bc-sp-k" data-idx="${idx}" readonly></td>
    <td><input type="number" class="form-control form-control-sm text-center bc-sp-time" data-idx="${idx}" readonly></td>
  `;
  tbody.appendChild(tr);
};

const runSpeedSequence = async () => {
  const rows = document.querySelectorAll("#bc-speed-body tr");
  if (rows.length === 0) return;

  const runBtn = document.getElementById("bc-sp-run-btn");
  const spinner = document.getElementById("bc-sp-run-spinner");
  const runText = document.getElementById("bc-sp-run-text");
  const calcBtn = document.getElementById("bc-sp-calc-btn");
  const resetBtn = document.getElementById("bc-sp-reset-btn");
  runBtn.disabled = true;
  calcBtn.disabled = true;
  resetBtn.disabled = true;
  spinner.classList.remove("d-none");
  runText.textContent = " Running...";

  const freqs = Array.from(rows).map((tr) => {
    const inp = tr.querySelector(".bc-sp-freq");
    return inp ? parseInt(inp.value) : 0;
  });
  if (freqs.some(f => !f || f <= 0)) {
    window.alert("Invalid frequencies");
    spinner.classList.add("d-none");
    runText.textContent = "▶ Run";
    runBtn.disabled = false;
    calcBtn.disabled = false;
    resetBtn.disabled = false;
    return;
  }

  const result = await sendCommand("burette.cal.runSpeedSeq", { freqs, speed_ml_min: CONFIG.BC.DEFAULT_SPEED_ML_MIN });
  if (result?.status === "ok") {
    await waitForBuretteIdle();
    const calResult = await sendCommand("burette.cal.getResult", {});
    const calStatus = await sendCommand("burette.cal.get", {});
    const nominalVol = calStatus?.data?.nominal_vol;
    if (calResult?.data?.speeds) {
      rows.forEach((tr, i) => {
        const spd = tr.querySelector(".bc-sp-speed");
        if (spd && calResult.data.speeds[i] != null) {
          spd.value = calResult.data.speeds[i].toFixed(2);
        }
        const freq = parseFloat(tr.querySelector(".bc-sp-freq")?.value);
        const speed = calResult.data.speeds[i];
        const kEl = tr.querySelector(".bc-sp-k");
        if (kEl && freq > 0 && speed != null) {
          kEl.value = (speed / freq).toFixed(6);
        }
        const timeEl = tr.querySelector(".bc-sp-time");
        if (timeEl && nominalVol > 0 && speed > 0) {
          timeEl.value = Math.round(nominalVol / speed * CONFIG.BC.SECONDS_PER_MINUTE);
        }
      });
      if (calStatus?.status === "ok" && calStatus?.data) {
        updateBuretteCalUI(calStatus.data);
      }
    }
    spinner.classList.add("d-none");
    runText.textContent = "▶ Run";
    runBtn.disabled = false;
    calcBtn.disabled = false;
    resetBtn.disabled = false;
    return;
  }

  spinner.classList.add("d-none");
  runText.textContent = "▶ Run";
  runBtn.disabled = false;
  calcBtn.disabled = false;
  resetBtn.disabled = false;

  const statusResult = await sendCommand("burette.cal.get", {});
  if (statusResult?.status === "ok" && statusResult?.data) {
    updateBuretteCalUI(statusResult.data);
  }
};

const calcSpeedCalibration = async () => {
  const rows = document.querySelectorAll("#bc-speed-body tr");
  const measurements = Array.from(rows).reduce((acc, tr) => {
    const freq = parseFloat(tr.querySelector(".bc-sp-freq")?.value || "0");
    const speed = parseFloat(tr.querySelector(".bc-sp-speed")?.value || "0");
    return freq > 0 && speed > 0
      ? [...acc, { freq_hz: freq, speed_ml_min: speed }]
      : acc;
  }, []);
  if (measurements.length < 2) { window.alert("Need at least 2 measurements"); return; }

  const result = await sendCommand("burette.cal.calcSpeed", { measurements });
  if (result?.status === "ok" && result?.data) {
    const d = result.data;
    document.getElementById("bc-sp-k").textContent = d.k?.toFixed(6) ?? "—";
    document.getElementById("bc-sp-r2").textContent = d.r_squared?.toFixed(4) ?? "—";
    document.getElementById("bc-sp-calc-result").style.display = "block";
    document.getElementById("bc-sp-save-btn").disabled = false;
  }
};

window.calibratePoint = calibratePoint;
window.computeCalibration = computeCalibration;
window.resetCalibration = resetCalibration;
window.loadCalibrationStatus = loadCalibrationStatus;
window.loadBuretteCalStatus = loadBuretteCalStatus;
window.runVolumeCalibration = runVolumeCalibration;
window.calcVolumeCalibration = calcVolumeCalibration;
window.saveBuretteCal = saveBuretteCal;
window.resetBuretteCal = resetBuretteCal;
window.initSpeedTable = initSpeedTable;
window.addSpeedRow = addSpeedRow;
window.runSpeedSequence = runSpeedSequence;
window.calcSpeedCalibration = calcSpeedCalibration;
