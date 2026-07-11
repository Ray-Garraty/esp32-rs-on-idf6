#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <array>

using namespace std::literals::string_view_literals;

namespace ecotiter::interface::webui {

namespace detail {

static constexpr std::string_view MINIMAL_CSS = R"css(
*,::before,::after{box-sizing:border-box}:root{--bg-dark-primary:#1a1a2e;--bg-dark-secondary:#16213e;--bg-dark-accent:#0f3460;--bg-input-dark:#0d1117;--border-dark:#30363d;--text-dark:#e0e0e0;--focus-border:#86b7fe}
body{margin:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:1rem;line-height:1.5;color:#212529;background:#fff}
[data-bs-theme="dark"] body{background:var(--bg-dark-primary);color:var(--text-dark)}
.container{max-width:1140px;margin:0 auto;padding:0 12px}
.row{display:flex;flex-wrap:wrap;margin:0 -6px}
.row>.col-3{flex:0 0 25%;max-width:25%;padding:0 6px}
.row>.col-4{flex:0 0 33.33%;max-width:33.33%;padding:0 6px}
.row>.col-6{flex:0 0 50%;max-width:50%;padding:0 6px}
.row>.col-md-6{flex:0 0 100%;max-width:100%;padding:0 6px}
@media(min-width:768px){.row>.col-md-6{flex:0 0 50%;max-width:50%}}
.d-flex{display:flex}.d-inline-block{display:inline-block}.d-none{display:none}.flex-wrap{flex-wrap:wrap}.flex-grow-1{flex-grow:1}.gap-1{gap:4px}.gap-2{gap:8px}.gap-3{gap:16px}.align-items-center{align-items:center}.justify-content-center{justify-content:center}.justify-content-end{justify-content:flex-end}
.w-100{width:100%}.h-100{height:100%}
.text-center{text-align:center}.text-end{text-align:right}.text-nowrap{white-space:nowrap}.fw-bold{font-weight:700}.fw-medium{font-weight:500}.small{font-size:.875em}.font-monospace{font-family:'SF Mono',SFMono-Regular,ui-monospace,Menlo,Consolas,monospace}
.py-1{padding-top:4px;padding-bottom:4px}.py-2{padding-top:8px;padding-bottom:8px}.py-4{padding-top:24px;padding-bottom:24px}
.px-2{padding-left:8px;padding-right:8px}
.mb-0{margin-bottom:0}.mb-2{margin-bottom:8px}.mb-3{margin-bottom:16px}.mb-4{margin-bottom:24px}
.ms-1{margin-left:4px}.ms-2{margin-left:8px}.me-1{margin-right:4px}.me-2{margin-right:8px}.me-3{margin-right:16px}.mt-2{margin-top:8px}
.border-bottom{border-bottom:1px solid #dee2e6}
[data-bs-theme="dark"] .border-bottom{border-color:var(--border-dark)}
.badge{display:inline-block;padding:.35em .65em;font-size:.75em;font-weight:700;line-height:1;text-align:center;white-space:nowrap;border-radius:.375rem;color:#fff}
.bg-success{background-color:#198754}.bg-danger{background-color:#dc3545}.bg-warning{background-color:#ffc107;color:#000}.bg-secondary{background-color:#6c757d}.bg-primary{background-color:#0d6efd}
.btn{display:inline-block;padding:6px 12px;font-size:1rem;font-weight:400;line-height:1.5;text-align:center;text-decoration:none;border:1px solid transparent;border-radius:6px;cursor:pointer;user-select:none;vertical-align:middle}
.btn:disabled{pointer-events:none;opacity:.65}
.btn-sm{padding:4px 8px;font-size:.875rem;border-radius:4px}
.btn-primary{color:#fff;background:#0d6efd;border-color:#0d6efd}.btn-primary:hover{background:#0b5ed7}
.btn-success{color:#fff;background:#198754;border-color:#198754}.btn-success:hover{background:#157347}
.btn-danger{color:#fff;background:#dc3545;border-color:#dc3545}.btn-danger:hover{background:#bb2d3b}
.btn-outline-primary{color:#0d6efd;border-color:#0d6efd;background:transparent}.btn-outline-primary:hover{color:#fff;background:#0d6efd}
.btn-outline-danger{color:#dc3545;border-color:#dc3545;background:transparent}.btn-outline-danger:hover{color:#fff;background:#dc3545}
.btn-group{display:inline-flex;vertical-align:middle}.btn-group .btn{border-radius:0}.btn-group .btn:first-child{border-radius:6px 0 0 6px}.btn-group .btn:last-child{border-radius:0 6px 6px 0}
.btn-check{position:absolute;opacity:0;pointer-events:none}
.btn-check+.btn-outline-primary:hover{color:#fff;background:#0d6efd}
.btn-check:checked+.btn-outline-primary{color:#fff;background:#0d6efd;border-color:#0d6efd}
.form-control{display:block;width:100%;padding:6px 12px;font-size:1rem;line-height:1.5;color:#212529;background:#fff;border:1px solid #dee2e6;border-radius:6px;transition:border-color .15s}
.form-control:focus{border-color:#86b7fe;outline:0;box-shadow:0 0 0 3px rgba(13,110,253,.25)}
.form-control-sm{padding:4px 8px;font-size:.875rem;border-radius:4px}
.form-control[readonly]{background:#e9ecef;pointer-events:none}
[data-bs-theme="dark"] .form-control{background:var(--bg-dark-primary);border-color:var(--bg-dark-accent);color:var(--text-dark)}
textarea.form-control{resize:none;font-family:'SF Mono',SFMono-Regular,ui-monospace,Menlo,Consolas,monospace}
.form-select{display:block;width:auto;padding:4px 28px 4px 8px;font-size:.875rem;line-height:1.5;color:#212529;background:#fff url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3e%3cpath fill='none' stroke='%23343a40' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M2 5l6 6 6-6'/%3e%3c/svg%3e") no-repeat right 8px center/12px;border:1px solid #dee2e6;border-radius:6px;appearance:none}
.form-check{display:flex;min-height:24px;padding-left:24px;margin-bottom:0;align-items:center}
.form-check-input{width:16px;height:16px;margin-left:-24px;margin-top:3px;vertical-align:top;border:1px solid #dee2e6;border-radius:3px;flex-shrink:0}
.form-check-input:checked{background:#0d6efd;border-color:#0d6efd}
.form-check-label{font-size:.875rem;font-weight:500}
.form-label{margin-bottom:4px;font-size:.875rem}
.input-group{display:flex;align-items:stretch;width:100%}.input-group .form-control{position:relative;flex:1 1 auto;width:1%}.input-group-text{display:flex;align-items:center;padding:6px 12px;font-size:1rem;font-weight:400;color:#212529;background:#e9ecef;border:1px solid #dee2e6;border-radius:6px}
.table{width:100%;margin-bottom:16px;border-collapse:collapse;caption-side:bottom;font-size:.875rem}
.table>tbody{vertical-align:inherit}
.table th,.table td{padding:8px;border-bottom:1px solid #dee2e6;text-align:left}
.table-bordered,.table-bordered th,.table-bordered td{border:1px solid #dee2e6}
.table-sm th,.table-sm td{padding:4px}
.table-dark{--bs-table-bg:#212529;--bs-table-color:#fff;color:var(--bs-table-color);background:var(--bs-table-bg)}
.table-dark th,.table-dark td{border-color:#373b3e}
[data-bs-theme="dark"] .table-bordered,[data-bs-theme="dark"] .table-bordered th,[data-bs-theme="dark"] .table-bordered td,[data-bs-theme="dark"] .table th,[data-bs-theme="dark"] .table td{border-color:var(--border-dark)}
.accordion-button{position:relative;display:flex;align-items:center;width:100%;padding:16px 20px;font-size:1.25rem;font-weight:700;color:#212529;background:transparent;border:0;border-radius:0;cursor:pointer;text-align:left}
.accordion-button:not(.collapsed){background:#e7f1ff;color:#0c63e4;box-shadow:inset 0 -1px 0 rgba(0,0,0,.125)}
.accordion-button:focus{box-shadow:none;border-color:transparent}
.accordion-button::after{content:'';width:20px;height:20px;margin-left:auto;background:url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3e%3cpath fill='none' stroke='%23212529' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M2 5l6 6 6-6'/%3e%3c/svg%3e") no-repeat center;transition:transform .2s;flex-shrink:0}
.accordion-button:not(.collapsed)::after{transform:rotate(-180deg)}
.accordion-collapse{overflow:hidden;transition:height .35s ease}.accordion-collapse:not(.show){display:none}
.accordion-body{padding:16px 20px}.card-body{padding:16px 20px}
.accordion-item{border:1px solid #dee2e6;border-radius:8px;overflow:hidden}
.accordion-flush .accordion-item{border-radius:0;border-left:0;border-right:0}
[data-bs-theme="dark"] .accordion-item{background:var(--bg-dark-secondary);border-color:var(--bg-dark-accent)}
[data-bs-theme="dark"] .accordion-button{background:var(--bg-dark-secondary);color:var(--text-dark)}
[data-bs-theme="dark"] .accordion-button:not(.collapsed){background:var(--bg-dark-accent);color:#fff}
[data-bs-theme="dark"] .accordion-button::after{background:url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3e%3cpath fill='none' stroke='%23e0e0e0' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='M2 5l6 6 6-6'/%3e%3c/svg%3e") no-repeat center}
.theme-toggle-btn{position:fixed;top:12px;right:12px;z-index:1050;width:44px;height:44px;border-radius:50%;border:2px solid #dee2e6;background:#fff;font-size:20px;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:all .3s;box-shadow:0 2px 8px rgba(0,0,0,.15)}
.theme-toggle-btn:hover{transform:scale(1.1)}
[data-bs-theme="dark"] .theme-toggle-btn{background:var(--bg-dark-secondary);border-color:var(--bg-dark-accent);box-shadow:0 2px 8px rgba(0,0,0,.4)}
#stepperDrv-sg-threshold{font-size:.85rem;padding:.2rem .4rem;height:auto;width:70px}
#stepperDrv-sg-threshold[readonly]{background:transparent;border:none;color:inherit}
#stepperDrv-sg-threshold:not([readonly]){background:#fff;border-color:var(--focus-border);color:#212529}
[data-bs-theme="dark"] #stepperDrv-sg-threshold{background:var(--bg-input-dark);border-color:var(--border-dark);color:var(--text-input)}
[data-bs-theme="dark"] #stepperDrv-sg-threshold[readonly]{background:transparent;border-color:transparent}
#log-messages{font-size:.75rem}#sse-log-entries pre,#sse-log-entries code{font-size:.7rem}#sse-log-entries{min-height:50px;border:1px dashed #ccc;padding:4px}
[data-bs-theme="dark"] #log-messages{background:var(--bg-input-dark);color:var(--text-dark);border-color:var(--border-dark)}
.spinner-border{display:inline-block;width:24px;height:24px;border:3px solid currentColor;border-right-color:transparent;border-radius:50%;animation:spinner-border .75s linear infinite;vertical-align:middle}
.spinner-border-sm{width:16px;height:16px;border-width:2px}
@keyframes spinner-border{to{transform:rotate(360deg)}}
.pre-wrap{white-space:pre-wrap;word-break:break-word}
.cursor-pointer{cursor:pointer}
)css"sv;

static constexpr std::string_view INDEX_HTML = R"idxhtml(<!doctype html>
<html lang="en" data-bs-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>EcoTiter Dashboard</title>
<link rel="stylesheet" href="/style.css">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.3/font/bootstrap-icons.min.css">
<style>
.theme-toggle-btn span{font-size:22px;line-height:1}
</style>
</head>
<body>
<button class="theme-toggle-btn" id="theme-toggle" onclick="toggleTheme()" title="Toggle theme"><span id="theme-icon">&#9790;</span></button>
<div class="container py-4">
<h3 class="mb-4 text-center d-flex justify-content-center align-items-center gap-2">EcoTiter Burette <span id="connection-status" class="badge bg-success"><i class="bi-wifi"></i></span></h3>
<div class="accordion accordion-flush" id="main-accordion">

<!-- 1. Peripheral Status -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button" data-bs-toggle="collapse" data-bs-target="#collapse-hardware" aria-expanded="true">Status</button></h2>
<div id="collapse-hardware" class="accordion-collapse collapse show">
<div class="card-body">
<table class="table table-sm table-bordered mb-0">
<tbody>
<tr><th style="width:40%">Temperature</th><td id="hw-temperature">--</td></tr>
<tr><th>ADC (mV)</th><td id="hw-adc">--</td></tr>
<tr><th>Electrode (mV)</th><td id="hw-electrode">--</td></tr>
<tr><th>Valve</th><td><span id="hw-valve">--</span> <button id="hw-valve-toggle-btn" class="btn btn-sm btn-primary ms-2" onclick="toggleValve()">Toggle</button></td></tr>
<tr><th>Limit FULL</th><td id="hw-limit-full">--</td></tr>
<tr><th>Limit EMPTY</th><td id="hw-limit-empty">--</td></tr>
<tr><th>Connection</th><td id="hw-connection">--</td></tr>
</tbody></table>
</div></div></div>

<!-- 2. Stepper Driver -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-stepperdrv">Stepper Driver <span id="stepperDrv-status-icon" class="badge bg-danger ms-1" style="display:none">&#10007;</span><span id="stepperDrv-overheat" style="display:none"></span></button></h2>
<div id="collapse-stepperdrv" class="accordion-collapse collapse">
<div class="accordion-body">
<div class="row"><div class="col-md-6">
<p><strong>StallGuard result:</strong> <span id="stepperDrv-sg-result">--</span></p>
<p class="mb-1"><strong>StallGuard threshold:</strong> <input type="number" id="stepperDrv-sg-threshold" class="form-control d-inline-block" min="0" max="255" readonly> <button id="stepperDrv-sg-threshold-btn" class="btn btn-sm btn-primary ms-1" onclick="toggleSgThresholdEdit()">Edit</button></p>
</div><div class="col-md-6">
<p><strong>Stall detected:</strong> <span id="stepperDrv-stall">--</span></p>
<p><strong>Motor busy:</strong> <span id="stepperDrv-motor-busy">--</span></p>
</div></div></div></div></div>

<!-- 3. Stepper Control -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-stepper">Stepper Motor Control</button></h2>
<div id="collapse-stepper" class="accordion-collapse collapse">
<div class="accordion-body">
<div class="d-flex align-items-center gap-2 flex-wrap mb-3">
<div class="form-check form-check-inline mb-0"><input class="form-check-input" type="radio" name="stepper-mode" id="mode-steps" value="steps"> <label class="form-label fw-bold mb-0" for="mode-steps">By steps</label></div>
<div class="form-check form-check-inline mb-0"><input class="form-check-input" type="radio" name="stepper-mode" id="mode-continuous" value="continuous" checked> <label class="form-label fw-bold mb-0" for="mode-continuous">Continuous</label></div>
<span class="fw-bold ms-2">Steps: </span><span id="stepper-actual-steps" class="fw-bold text-primary">0</span>
</div>
<div class="d-flex align-items-center gap-2 flex-wrap mb-3">
<div id="dynamic-input-container"></div>
<button id="stepper-start-stop-btn" class="btn btn-success">&#9654; Start</button>
<div id="stepper-spinner" class="spinner-border text-primary" role="status" style="display:none"></div>
<div class="btn-group" role="group">
<input type="radio" class="btn-check" name="stepper-dir" id="dir-down" value="LIQ_IN" checked> <label class="btn btn-outline-primary btn-md" for="dir-down">&#9660; IN</label>
<input type="radio" class="btn-check" name="stepper-dir" id="dir-up" value="LIQ_OUT"> <label class="btn btn-outline-primary btn-md" for="dir-up">&#9650; OUT</label>
</div>
<span class="fw-bold me-1">EN:</span> <span id="stepperDrv-en-pin" class="badge bg-secondary ms-1">HIGH</span>
</div>
</div></div></div>

<!-- 4. System Log -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button" data-bs-toggle="collapse" data-bs-target="#collapse-logs" aria-expanded="true">System Log</button></h2>
<div id="collapse-logs" class="accordion-collapse collapse show">
<div class="accordion-body">
<div class="d-flex justify-content-end gap-2 mb-2">
<select id="log-level-filter" class="form-select" onchange="setLogLevelFilter(this.value)"><option value="ALL" selected>ALL</option><option value="DEBUG">DEBUG</option><option value="INFO">INFO</option><option value="WARN">WARN</option><option value="ERROR">ERROR</option></select>
<button id="logs-download-btn" class="btn btn-sm btn-primary" onclick="downloadLogs()">Download</button>
<button class="btn btn-sm btn-danger" onclick="clearLogs()">Clear</button>
</div>
<textarea id="log-messages" class="form-control font-monospace" rows="8" readonly></textarea>
</div></div></div>

<!-- 5. WS Events -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-ws">WS Events</button></h2>
<div id="collapse-ws" class="accordion-collapse collapse">
<div class="accordion-body">
<div class="d-flex justify-content-end gap-3 mb-2">
<div class="form-check form-check-inline mb-0"><input class="form-check-input" type="checkbox" id="ws-raw-json" onchange="toggleWsRawJson()"> <label class="form-check-label" for="ws-raw-json">Raw JSON</label></div>
<div class="form-check form-check-inline mb-0"><input class="form-check-input" type="checkbox" id="ws-autoupdate-check" checked onchange="toggleWsAutoupdate()"> <label class="form-check-label" for="ws-autoupdate-check">Auto-update</label></div>
</div>
<div id="ws-log-entries"></div>
</div></div></div>

<!-- 6. ADC Calibration -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-adc-cal">ADC Calibration <span id="adc-cal-status-badge" class="badge bg-secondary ms-2">Default</span></button></h2>
<div id="collapse-adc-cal" class="accordion-collapse collapse">
<div class="accordion-body px-2 py-2">
<table class="table table-sm table-bordered mb-2 text-center" id="adc-cal-points-table">
<thead class="table-dark"><tr><th class="text-center" style="width:10%">#</th><th class="text-center" style="width:25%">Reference (mV)</th><th class="text-center" style="width:30%">Signal ADC (mV)</th><th class="text-center" style="width:35%">Action</th></tr></thead>
<tbody id="adc-cal-points-body"></tbody></table>
<div class="row mb-2 g-1"><div class="col-6"><span class="fw-bold">a:</span> <span id="adc-cal-a" class="font-monospace">1.000000</span></div><div class="col-6"><span class="fw-bold">b:</span> <span id="adc-cal-b" class="font-monospace">0.000000</span></div></div>
<div class="row mb-2 g-1"><div class="col-6"><span class="fw-bold">R&#178;:</span> <span id="adc-cal-r2">--</span></div><div class="col-6"><span class="fw-bold">Calibrated:</span> <span id="adc-cal-date">--</span></div></div>
<div class="d-flex gap-2 mt-2"><button id="adc-cal-compute-btn" class="btn btn-success flex-grow-1" disabled>Compute &amp; Save</button><button id="adc-cal-reset-btn" class="btn btn-outline-danger" onclick="resetCalibration()">Reset</button></div>
</div></div></div>

<!-- 7. Pinout Table -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-pinout">Pinout Table</button></h2>
<div id="collapse-pinout" class="accordion-collapse collapse">
<div class="accordion-body p-0">
<table class="table table-sm table-bordered text-center"><thead class="table-dark"><tr><th>GPIO</th><th>Device</th><th>Note</th></tr></thead>
<tbody>
<tr><td><strong>21</strong></td><td>TMC2209 STEP</td><td>RMT peripheral</td></tr>
<tr><td><strong>5</strong></td><td>TMC2209 DIR</td><td>GPIO26 is PSRAM CS1</td></tr>
<tr><td><strong>27</strong></td><td>TMC2209 EN</td><td>Active LOW</td></tr>
<tr><td><strong>14</strong></td><td>Valve</td><td>GPIO output</td></tr>
<tr><td><strong>6</strong></td><td>DS18B20</td><td>4.7k pull-up</td></tr>
<tr><td><strong>4</strong></td><td>ADC pH electrode</td><td>ADC1_CH3</td></tr>
<tr><td><strong>7</strong></td><td>Limit FULL</td><td>GPIO ISR pos-edge</td></tr>
<tr><td><strong>15</strong></td><td>Limit EMPTY</td><td>GPIO ISR pos-edge</td></tr>
<tr><td><strong>48</strong></td><td>RGB LED (WS2812)</td><td>RMT TX channel</td></tr>
<tr><td><strong>1/3</strong></td><td>UART</td><td>Serial console</td></tr>
</tbody></table>
</div></div></div>

<!-- 8. Burette Volume Calibration -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-bc-volume">Volume Calibration <span id="bc-cal-status-badge" class="badge bg-secondary ms-2">Default</span></button></h2>
<div id="collapse-bc-volume" class="accordion-collapse collapse">
<div class="accordion-body px-2 py-2">
<table class="table table-sm table-bordered mb-2"><tbody><tr class="text-center">
<th class="text-end" style="width:16%">steps_per_ml</th><td style="width:18%"><span id="bc-steps-per-ml">--</span></td>
<th class="text-end" style="width:16%">nominal_vol (ml)</th><td style="width:18%"><span id="bc-nominal-vol">--</span></td>
<th class="text-end" style="width:12%">Date</th><td style="width:20%"><span id="bc-cal-date">--</span></td>
</tr></tbody></table>
<div class="row g-2 mb-2 text-center">
<div class="col-3"><label class="form-label mb-0 small fw-medium">Freq (Hz)</label><input type="number" class="form-control form-control-sm text-center" id="bc-vol-freq" min="1" step="1"></div>
<div class="col-3"><label class="form-label mb-0 small fw-medium">Mass (g)</label><input type="number" class="form-control form-control-sm text-center" id="bc-vol-mass" step="0.01" min="0"></div>
<div class="col-3"><label class="form-label mb-0 small fw-medium">Temp (&#176;C)</label><input type="number" class="form-control form-control-sm text-center" id="bc-vol-temp" value="25" step="0.1"></div>
<div class="col-3"><label class="form-label mb-0 small fw-medium">Pressure (kPa)</label><input type="number" class="form-control form-control-sm text-center" id="bc-vol-pressure" value="101.3" step="0.1"></div>
</div>
<div class="d-flex gap-2 mb-2">
<button class="btn btn-primary btn-sm flex-fill" id="bc-vol-run-btn" onclick="runVolumeCalibration()"><span class="spinner-border spinner-border-sm d-none" id="bc-vol-run-spinner"></span><span id="bc-vol-run-text">&#9654; Run</span></button>
<button class="btn btn-success btn-sm flex-fill" id="bc-vol-calc-btn" onclick="calcVolumeCalibration()" disabled>Calculate</button>
<button class="btn btn-outline-danger btn-sm flex-fill" id="bc-vol-reset-btn" onclick="resetBuretteCal()">Reset</button>
</div>
<div id="bc-vol-run-result" class="mb-2" style="display:none"><table class="table table-sm table-bordered mb-0"><tr><th>Steps taken</th><td id="bc-vol-steps">--</td></tr></table></div>
<div id="bc-vol-calc-result" style="display:none"><table class="table table-sm table-bordered mb-2"><tr><th>Z-factor</th><td id="bc-vol-z-factor">--</td></tr><tr><th>Actual vol (ml)</th><td id="bc-vol-actual">--</td></tr><tr><th>New steps_per_ml</th><td id="bc-vol-new-sps">--</td></tr><tr><th>Error (%)</th><td id="bc-vol-error">--</td></tr></table>
<button class="btn btn-primary btn-sm w-100 mb-2" id="bc-vol-save-btn" onclick="saveBuretteCal()" disabled>Save</button></div>
</div></div></div>

<!-- 9. Burette Speed Calibration -->
<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#collapse-bc-speed">Speed Calibration <span id="bc-sp-cal-status-badge" class="badge bg-secondary ms-2">Default</span></button></h2>
<div id="collapse-bc-speed" class="accordion-collapse collapse">
<div class="accordion-body px-2 py-2">
<table class="table table-sm table-bordered mb-2"><tbody><tr class="text-center">
<th class="text-end" style="width:16%">speed_coeff</th><td style="width:17%"><span id="bc-speed-coeff">--</span></td>
<th class="text-end" style="width:16%">Freq range (Hz)</th><td style="width:17%"><span id="bc-freq-range">--</span></td>
<th class="text-end" style="width:12%">Date</th><td style="width:22%"><span id="bc-sp-cal-date">--</span></td>
</tr></tbody></table>
<table class="table table-sm table-bordered mb-2 text-center" id="bc-speed-table"><thead class="table-dark"><tr><th style="width:7%">#</th><th style="width:26%">Freq (Hz)</th><th style="width:23%">Speed (ml/min)</th><th style="width:23%">k</th><th style="width:21%">Time (s)</th></tr></thead>
<tbody id="bc-speed-body"></tbody></table>
<div class="d-flex gap-2 mb-2">
<button class="btn btn-primary btn-sm flex-fill" id="bc-sp-run-btn" onclick="runSpeedSequence()"><span class="spinner-border spinner-border-sm d-none" id="bc-sp-run-spinner"></span><span id="bc-sp-run-text">&#9654; Run</span></button>
<button class="btn btn-success btn-sm flex-fill" id="bc-sp-calc-btn" onclick="calcSpeedCalibration()" disabled>Calculate</button>
<button class="btn btn-outline-danger btn-sm flex-fill" id="bc-sp-reset-btn" onclick="resetBuretteCal()">Reset</button>
</div>
<div id="bc-sp-calc-result" style="display:none"><table class="table table-sm table-bordered mb-2"><tr><th>k (ml/min/Hz)</th><td id="bc-sp-k">--</td></tr><tr><th>R&#178;</th><td id="bc-sp-r2">--</td></tr></table>
<button class="btn btn-primary btn-sm w-100 mb-2" id="bc-sp-save-btn" onclick="saveBuretteCal()" disabled>Save</button></div>
</div></div></div>

</div></div>
<script src="js/state.js"></script>
<script src="js/ws.js"></script>
<script src="js/ui-update.js"></script>
<script src="js/logs.js"></script>
<script src="js/stepper.js"></script>
<script src="js/calibration.js"></script>
<script src="js/init.js"></script>
<script>
document.querySelectorAll('.accordion-button').forEach(function(btn){
btn.addEventListener('click',function(){var t=document.querySelector(this.dataset.bsTarget);if(t){t.classList.toggle('show');this.classList.toggle('collapsed');this.setAttribute('aria-expanded',t.classList.contains('show'));}});
});
</script>
</body>
</html>)idxhtml"sv;

static constexpr std::string_view CAPTIVE_HTML = R"htmlraw(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>EcoTiter WiFi Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.container{background:white;border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,0.3);max-width:400px;width:100%;padding:40px}
h1{color:#333;margin-bottom:10px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:8px;color:#555;font-weight:500}
.input-wrap{position:relative;display:flex;align-items:center}
.input-wrap input{width:100%;padding:12px 16px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;padding-right:44px}
.input-wrap input:focus{outline:none;border-color:#667eea}
.toggle-pass{position:absolute;right:12px;background:none;border:none;font-size:20px;cursor:pointer;color:#999;padding:4px;line-height:1;width:auto;border-radius:0}
.toggle-pass:hover{color:#667eea;opacity:1;background:none}
button{width:100%;padding:14px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer}
button:hover{opacity:0.9}
.status{margin-top:20px;padding:12px;border-radius:8px;display:none}
.status.info{background:#e3f2fd;color:#1976d2;display:block}
.status.success{background:#e8f5e9;color:#388e3c;display:block}
.status.error{background:#ffebee;color:#d32f2f;display:block}
</style>
</head>
<body>
<div class="container">
<h1>EcoTiter</h1>
<p style="color:#666;margin-bottom:20px">WiFi Configuration</p>
<form id="wifi-form">
<div class="form-group"><label for="ssid">Network (SSID)</label><input type="text" id="ssid" required placeholder="Enter WiFi SSID"></div>
<div class="form-group"><label for="password">Password</label><div class="input-wrap"><input type="password" id="password" placeholder="Enter password"><button type="button" class="toggle-pass" id="toggle-pass" onclick="togglePass()" aria-label="Show password">&#128065;</button></div></div>
<button type="submit">Connect</button>
</form>
<div id="status" class="status"></div>
</div>
<script>
function togglePass(){var p=document.getElementById('password');var b=document.getElementById('toggle-pass');if(p.type==='password'){p.type='text';b.textContent='&#128066;';b.setAttribute('aria-label','Hide password');}else{p.type='password';b.textContent='&#128065;';b.setAttribute('aria-label','Show password');}}
document.getElementById('wifi-form').addEventListener('submit',async function(e){e.preventDefault();var ssid=document.getElementById('ssid').value;var pass=document.getElementById('password').value;var status=document.getElementById('status');var btn=this.querySelector('button');btn.disabled=true;btn.textContent='Connecting...';status.className='status info';status.textContent='Connecting...';try{var r=await fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})});var j=await r.json();if(j.success){status.className='status success';status.innerHTML='Connected!<br>Device restarting...';btn.textContent='Done!';}else{status.className='status error';status.textContent='Error: '+(j.message||'Failed');btn.disabled=false;btn.textContent='Connect';}}catch(e){status.className='status error';status.textContent='Connection lost. Device may be restarting.';btn.disabled=false;btn.textContent='Connect';}});
</script>
</body>
</html>)htmlraw"sv;

static constexpr std::string_view STATE_JS = R"js(
var CONFIG={LOG_MAX_ENTRIES:100,WS_MAX_ENTRIES:5,WS_CHECK_INTERVAL_MS:2000,WS_PING_INTERVAL_MS:2000,WS_TIMEOUT_MS:4000,STEPPER:{DEFAULT_FREQ:300,DEFAULT_STEPS:1000,MAX_FREQ:1000},BC:{MAX_SPEED_ROWS:10,FREQ_PERCENTAGES:[0.25,0.50,0.75]}};
var APP_STATE={stepper:{direction:'LIQ_OUT',busy:false,enEnabled:false,mode:'continuous'},valve:{position:'input'},logs:{messages:[],wsEntries:[],wsAutoupdate:true,wsRawJson:false,baseDate:null,baseMillis:0},ui:{sgEditMode:false,motorStoppedAt:null,logLevelFilter:'ALL'},calibration:{speedRowCount:0,adcState:null,calibratingIndex:-1}};
var setState=function(path,value,onUpdate){var keys=path.split('.');var target=APP_STATE;for(var i=0;i<keys.length-1;i++)target=target[keys[i]];target[keys[keys.length-1]]=value;if(onUpdate)onUpdate();};
window.APP_STATE=APP_STATE;window.CONFIG=CONFIG;
)js"sv;

static constexpr std::string_view WS_JS = R"js(
var ws=null;var lastMsgTime=Date.now();var wsReconnectTimer=null;
var intervals={check:null,ping:null};
var _connAlive=false;

function setConnectionStatus(alive){
  _connAlive=alive;
  var el=document.getElementById('connection-status');
  if(el){el.className='badge bg-'+(alive?'success':'danger');el.innerHTML=alive?'<i class="bi-wifi"></i>':'<i class="bi-wifi-off"></i>';}
  ['hw-valve-toggle-btn','stepper-start-stop-btn','logs-download-btn','dir-down','dir-up'].forEach(function(id){var btn=document.getElementById(id);if(btn)btn.disabled=!alive;});
  ['dir-down','dir-up'].forEach(function(id){var lbl=document.querySelector('label[for="'+id+'"]');if(lbl){lbl.style.pointerEvents=alive?'auto':'none';}});
  if(!alive)resetUIValues();
}

function resetUIValues(){
  var fields={'hw-temperature':'--','hw-adc':'--','hw-valve':'--','hw-limit-full':'--','hw-limit-empty':'--','hw-connection':'--','stepperDrv-status-icon':'&#10007;','stepperDrv-sg-result':'--','stepperDrv-stall':'--','stepperDrv-motor-busy':'--','stepper-actual-steps':'0'};
  for(var k in fields){var el=document.getElementById(k);if(el)el.textContent=fields[k];}
}

function checkConnection(){var elapsed=Date.now()-lastMsgTime;if(elapsed>CONFIG.WS_TIMEOUT_MS&&_connAlive)setConnectionStatus(false);else if(elapsed<=CONFIG.WS_TIMEOUT_MS&&!_connAlive)setConnectionStatus(true);}
function pingServer(){try{fetch('/api/status',{cache:'no-cache'}).then(function(r){if(r.ok&&!_connAlive){console.log('Server back. Reconnecting WS...');if(ws)ws.close();connectWs();}}).catch(function(e){});}catch(e){}}

function connectWs(){
  if(ws&&ws.readyState===WebSocket.OPEN)return;
  clearInterval(intervals.check);clearInterval(intervals.ping);
  var proto=window.location.protocol==='https:'?'wss:':'ws:';
  var url=proto+'//'+window.location.host+'/ws/stream';
  ws=new WebSocket(url);
  lastMsgTime=Date.now();

  ws.onopen=function(){setConnectionStatus(true);lastMsgTime=Date.now();intervals.check=setInterval(checkConnection,CONFIG.WS_CHECK_INTERVAL_MS);intervals.ping=setInterval(function(){if(!_connAlive||Date.now()-lastMsgTime>3000)pingServer();},CONFIG.WS_PING_INTERVAL_MS);};

  ws.onmessage=function(e){
    lastMsgTime=Date.now();if(!_connAlive)setConnectionStatus(true);
    try{
      var data=JSON.parse(e.data);
      if(data.event&&data.event==='log'&&data.data){
        addLogEntry(data.data);
      }else if(data.event&&data.event==='limitsw'){
        if(data.full!==undefined)document.getElementById('hw-limit-full').textContent=data.full?'Activated':'--';
        if(data.empty!==undefined)document.getElementById('hw-limit-empty').textContent=data.empty?'Activated':'--';
      }else{
        updateUI(data);updateDebugUI(data);
        if(APP_STATE.logs.wsAutoupdate){
          APP_STATE.logs.wsEntries=[data].concat(APP_STATE.logs.wsEntries).slice(0,CONFIG.WS_MAX_ENTRIES);
          renderWsLog();
        }
      }
    }catch(err){}
  };

  ws.onclose=function(){setConnectionStatus(false);if(wsReconnectTimer)clearTimeout(wsReconnectTimer);wsReconnectTimer=setTimeout(connectWs,3000);};
  ws.onerror=function(){ws.close();};
}

function sendWs(data){if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify(data));}
function toggleWsAutoupdate(){APP_STATE.logs.wsAutoupdate=!APP_STATE.logs.wsAutoupdate;document.getElementById('ws-autoupdate-check').checked=APP_STATE.logs.wsAutoupdate;}
function toggleWsRawJson(){APP_STATE.logs.wsRawJson=!APP_STATE.logs.wsRawJson;document.getElementById('ws-raw-json').checked=APP_STATE.logs.wsRawJson;renderWsLog();}

function millisToTime(ms,base,baseMs){if(!base||!baseMs)return '';return new Date(base.getTime()+(ms-baseMs)).toLocaleTimeString();}
function valveLabel(v){return({'in':'input','out':'output','unk':'unknown'})[v]||v||'--';}

function formatWsEntry(e){
  var ts=e.ts?millisToTime(e.ts,APP_STATE.logs.baseDate,APP_STATE.logs.baseMillis):'--';var b=e.brt;
  var cell=function(c,t){return '<td class="text-nowrap small"'+(t?' title="'+t+'"':'')+'>'+c+'</td>';};
  var buretteStatus=b?(b.sts==='working'?'<span class="badge bg-warning text-dark">moving</span>':'<span class="badge bg-success">idle</span>'):'--';
  var vol=(b&&b.vl!=null)?b.vl.toFixed(2)+' ml':'--';
  var speed=(b&&b.spd!=null)?b.spd.toFixed(1)+' ml/min':'--';
  var tempStr=(e.temp!=null)?e.temp.toFixed(1)+'&#176;C':'--';
  var mvStr=(e.mv!=null)?Number(e.mv).toFixed(1)+' mV':'--';
  return '<tr>'+cell(ts)+cell(buretteStatus)+cell(vol,'Volume')+cell(speed,'Speed')+cell(tempStr)+cell(mvStr)+cell(valveLabel(e.vlv))+'</tr>';
}

function renderWsLog(){
  var container=document.getElementById('ws-log-entries');if(!container)return;
  if(APP_STATE.logs.wsRawJson){
    container.innerHTML=APP_STATE.logs.wsEntries.map(function(e){return '<div class="border-bottom py-1"><pre class="mb-0"><code>'+JSON.stringify(e,null,2)+'</code></pre></div>';}).join('');
    return;
  }
  var header='<table class="table table-sm table-bordered mb-0"><thead class="table-dark"><tr><th>Time</th><th>Burette</th><th>Volume</th><th>Speed</th><th>Temp</th><th>Electrode</th><th>Valve</th></tr></thead><tbody>';
  container.innerHTML=header+APP_STATE.logs.wsEntries.map(formatWsEntry).join('')+'</tbody></table>';
}

window.connectWs=connectWs;window.renderWsLog=renderWsLog;window.toggleWsAutoupdate=toggleWsAutoupdate;window.toggleWsRawJson=toggleWsRawJson;
)js"sv;

static constexpr std::string_view UI_UPDATE_JS = R"js(
var __prevDebug={};var __connUsb=false;var __connBle=false;

function updateConnectionStatus(){
  var el=document.getElementById('hw-connection');if(!el)return;
  if(__connBle)el.innerHTML='<span class="badge bg-success">BLE</span>';
  else if(__connUsb)el.innerHTML='<span class="badge bg-success">USB</span>';
  else el.textContent='--';
}

function updateUI(data){
  if(APP_STATE.logs.baseDate===null&&data.ts){APP_STATE.logs.baseDate=new Date();APP_STATE.logs.baseMillis=data.ts;}
  setText('hw-temperature',data.temp!==null&&data.temp!==undefined?data.temp.toFixed(1)+' \u00b0C':'--');
  setText('hw-adc',data.mv!==undefined?data.mv+' mV':'--');
  setText('hw-electrode',data.mv!==undefined?data.mv+' mV':'--');
  if(data.usbSerialConnected!==undefined){__connUsb=data.usbSerialConnected;updateConnectionStatus();}
  if(data.bleConnected!==undefined){__connBle=data.bleConnected;updateConnectionStatus();}
  if(data.vlv!==undefined){APP_STATE.valve.position=({'in':'input','out':'output','unk':'unknown'})[data.vlv]||data.vlv;document.getElementById('hw-valve').textContent=APP_STATE.valve.position;}
  if(data.brt){updateStepperUI({status:data.brt.sts,volume_ml:data.brt.vl,speed_ml_min:data.brt.spd});}
}

function updateDebugUI(data){
  var cur={adcRawMv:data.mv,usbConnected:data.usbSerialConnected,bleConnected:data.bleConnected,drvConnected:data.stepperDrv&&data.stepperDrv.isConnected,otpw:data.stepperDrv&&data.stepperDrv.otpw,ot:data.stepperDrv&&data.stepperDrv.ot,sgValue:data.stepperDrv&&data.stepperDrv.motor&&data.stepperDrv.motor.stallGuard&&data.stepperDrv.motor.stallGuard.value,isStalled:data.stepperDrv&&data.stepperDrv.motor&&data.stepperDrv.motor.stallGuard&&data.stepperDrv.motor.stallGuard.isStalled,isMoving:data.stepperDrv&&data.stepperDrv.motor&&data.stepperDrv.motor.isMoving,sgThreshold:data.stepperDrv&&data.stepperDrv.motor&&data.stepperDrv.motor.stallGuard&&data.stepperDrv.motor.stallGuard.threshold,stepsTaken:data.buretteSteps&&data.buretteSteps.taken};
  var diff=function(k){return cur[k]!==__prevDebug[k];};
  var set=function(id,v){var el=document.getElementById(id);if(el)el.textContent=v??'--';};
  if(diff('usbConnected')){__connUsb=cur.usbConnected;updateConnectionStatus();}
  if(diff('bleConnected')){__connBle=cur.bleConnected;updateConnectionStatus();}
  if(diff('drvConnected')){var el=document.getElementById('stepperDrv-status-icon');if(el){el.style.display='';el.className='badge bg-'+(cur.drvConnected?'success':'danger')+' ms-1';el.textContent=cur.drvConnected?'&#10003;':'&#10007;';}}
  if(diff('ot')||diff('otpw')||diff('drvConnected')){var el=document.getElementById('stepperDrv-overheat');if(el){el.style.display=cur.drvConnected?'':'none';if(cur.ot)el.innerHTML='<span style="color:#dc3545">&#9822;</span>';else if(cur.otpw)el.innerHTML='<span style="color:#ffc107">&#9822;</span>';else if(cur.drvConnected)el.innerHTML='<span style="color:#198754">&#9822;</span>';}}
  if(diff('sgValue'))set('stepperDrv-sg-result',cur.sgValue);
  if(diff('isStalled'))set('stepperDrv-stall',cur.isStalled?'Yes':'No');
  if(diff('isMoving'))set('stepperDrv-motor-busy',cur.isMoving?'Yes':'No');
  if(diff('sgThreshold')){var inp=document.getElementById('stepperDrv-sg-threshold');if(inp&&!APP_STATE.ui.sgEditMode)inp.value=cur.sgThreshold??'';}
  if(diff('stepsTaken'))set('stepper-actual-steps',cur.stepsTaken);
  __prevDebug={};for(var k in cur)__prevDebug[k]=cur[k];
}

function updateStepperUI(stepper){
  var isMoving=stepper&&stepper.status&&(stepper.status==='busy'||stepper.status==='moving'||stepper.status==='working');
  APP_STATE.stepper.busy=!!isMoving;
  APP_STATE.stepper.enEnabled=stepper&&stepper.isEnabled!==undefined?stepper.isEnabled:false;
  var enEl=document.getElementById('stepperDrv-en-pin');if(enEl){enEl.textContent=APP_STATE.stepper.enEnabled?'LOW':'HIGH';enEl.className='badge bg-'+(APP_STATE.stepper.enEnabled?'success':'secondary')+' ms-1';}
  var btn=document.getElementById('stepper-start-stop-btn');if(btn){btn.textContent=isMoving?'Stop':'Start';btn.className=isMoving?'btn btn-danger':'btn btn-success';}
  var sp=document.getElementById('stepper-spinner');if(sp)sp.style.display=isMoving?'':'none';
  var inp=document.querySelector('#dynamic-input-container input');if(inp)inp.disabled=!!isMoving;
  document.querySelectorAll('input[name="stepper-mode"]').forEach(function(r){r.disabled=!!isMoving;});
  if(stepper&&stepper.direction){APP_STATE.stepper.direction=stepper.direction;var isOut=stepper.direction==='LIQ_OUT';var d=document.getElementById('dir-down');var u=document.getElementById('dir-up');if(d)d.checked=!isOut;if(u)u.checked=isOut;}
}

function updateSgThresholdUI(){
  var input=document.getElementById('stepperDrv-sg-threshold');var btn=document.getElementById('stepperDrv-sg-threshold-btn');if(!input||!btn)return;
  if(APP_STATE.ui.sgEditMode){input.removeAttribute('readonly');btn.textContent='Save';btn.className='btn btn-sm btn-success ms-1';}
  else{input.setAttribute('readonly','');input.blur();btn.textContent='Edit';btn.className='btn btn-sm btn-primary ms-1';}
}

function toggleSgThresholdEdit(){var input=document.getElementById('stepperDrv-sg-threshold');if(!APP_STATE.ui.sgEditMode){APP_STATE.ui.sgEditMode=true;updateSgThresholdUI();}else{var val=parseInt(input.value,10);if(isNaN(val)||val<0||val>255){alert('Threshold: 0-255');return;}var btn=document.getElementById('stepperDrv-sg-threshold-btn');if(btn)btn.disabled=true;sendCommand('stallGuard.setThreshold',{threshold:val}).then(function(){APP_STATE.ui.sgEditMode=false;updateSgThresholdUI();if(btn)btn.disabled=false;});}}

function setText(id,v){var el=document.getElementById(id);if(el)el.textContent=v;}

window.updateUI=updateUI;window.updateDebugUI=updateDebugUI;window.updateSgThresholdUI=updateSgThresholdUI;window.toggleSgThresholdEdit=toggleSgThresholdEdit;window.setText=setText;
)js"sv;

static constexpr std::string_view LOGS_JS = R"js(
var LOG_LEVEL_REGEX=/\[(\w+)\]/;

function extractLevel(entry){var m=LOG_LEVEL_REGEX.exec(entry);return m?m[1]:'INFO';}
function getFilteredLogs(msgs,filter){return filter==='ALL'?msgs:msgs.filter(function(e){return extractLevel(e)===filter;});}
function addLogEntryData(entries,data,maxLen){var ts=new Date().toLocaleTimeString();var entry='['+ts+'] ['+(data.level||'INFO')+'] '+(data.msg||'');var newEntries=[entry].concat(entries);return newEntries.length>maxLen?newEntries.slice(0,maxLen):newEntries;}

function renderLogTextarea(){
  var el=document.getElementById('log-messages');if(!el)return;
  el.value=getFilteredLogs(APP_STATE.logs.messages,APP_STATE.ui.logLevelFilter).join('\n');el.scrollTop=0;
}

function setLogLevelFilter(level){APP_STATE.ui.logLevelFilter=level;renderLogTextarea();}
function addLogEntry(data){APP_STATE.logs.messages=addLogEntryData(APP_STATE.logs.messages,data);renderLogTextarea();}
function clearLogs(){APP_STATE.logs.messages=[];var el=document.getElementById('log-messages');if(el)el.value='';}

function downloadLogs(){
  fetch('/api/logs/download').then(function(r){return r.text();}).then(function(text){var blob=new Blob([text],{type:'text/plain'});var url=URL.createObjectURL(blob);var a=document.createElement('a');a.href=url;a.download='ecotiter-logs.txt';a.click();URL.revokeObjectURL(url);}).catch(function(e){console.error('downloadLogs error:',e);});
}

window.setLogLevelFilter=setLogLevelFilter;window.addLogEntry=addLogEntry;window.clearLogs=clearLogs;window.downloadLogs=downloadLogs;
)js"sv;

static constexpr std::string_view STEPPER_JS = R"js(
var STEPPER_CFG=CONFIG.STEPPER;

function buildStepperCommand(mode,direction,value){
  if(mode==='steps')return{cmd:'burette.moveSteps',params:{steps:value,direction:direction,freq:STEPPER_CFG.DEFAULT_FREQ}};
  return{cmd:'burette.moveToStop',params:{direction:direction,freq:value}};
}

function updateDynamicInput(){
  var container=document.getElementById('dynamic-input-container');if(!container)return;
  var isSteps=APP_STATE.stepper.mode==='steps';
  var id=isSteps?'stepper-steps-input':'stepper-freq-input';
  var label=isSteps?'Steps:':'Freq (Hz):';
  var val=isSteps?STEPPER_CFG.DEFAULT_STEPS:STEPPER_CFG.DEFAULT_FREQ;
  container.innerHTML='<div class="input-group" style="width:200px"><span class="input-group-text fw-bold py-0 px-2">'+label+'</span><input type="number" id="'+id+'" class="form-control" value="'+val+'" min="1" style="width:60px"></div>';
}

function stepperStartStop(){
  var btn=document.getElementById('stepper-start-stop-btn');
  if(APP_STATE.stepper.busy){
    if(btn){btn.disabled=true;btn.textContent='Stopping...';}
    sendCommand('burette.stop',{}).then(function(res){if(!res)showUIError('Failed to stop motor');else if(res.status==='error')console.error('stepperStartStop: stop rejected',JSON.stringify(res));if(btn)btn.disabled=false;});
  }else{
    var dir=(document.querySelector('input[name="stepper-dir"]:checked')||{}).value||APP_STATE.stepper.direction;
    var input=document.querySelector('#dynamic-input-container input');var val=parseInt(input?input.value:'0',10);
    if(val<=0){showUIError('Enter value > 0');return;}
    if(btn){btn.disabled=true;btn.textContent='Starting...';}
    var cmd=buildStepperCommand(APP_STATE.stepper.mode,dir,val);
    sendCommand(cmd.cmd,cmd.params).then(function(res){if(!res)showUIError('Command '+cmd.cmd+' failed');else if(res.status==='error')console.error('stepperStartStop: '+cmd.cmd+' rejected',JSON.stringify(res));if(btn)btn.disabled=false;});
  }
}

function initStepperControls(){
  document.querySelectorAll('input[name="stepper-mode"]').forEach(function(r){r.addEventListener('change',function(e){APP_STATE.stepper.mode=e.target.value;updateDynamicInput();});});
  document.querySelectorAll('input[name="stepper-dir"]').forEach(function(r){r.addEventListener('change',function(e){APP_STATE.stepper.direction=e.target.value;sendCommand('burette.setDirection',{direction:e.target.value}).then(function(res){if(res&&res.status==='error')console.error('stepper: setDirection rejected',JSON.stringify(res));});});});
  var btn=document.getElementById('stepper-start-stop-btn');if(btn)btn.addEventListener('click',stepperStartStop);
}

function showUIError(msg){console.error(msg);alert(msg);}

window.updateDynamicInput=updateDynamicInput;window.initStepperControls=initStepperControls;
)js"sv;

static constexpr std::string_view CALIBRATION_JS = R"js(
var ADC_CAL_REFS=[0,-177.5,177.5,350.0,-350.0];

function updateCalibrationUI(state){
  if(!state)return;
  var badge=document.getElementById("adc-cal-status-badge");
  if(badge){if(state.is_default){badge.textContent="Default";badge.className="badge bg-secondary ms-2";}else{badge.textContent="Custom";badge.className="badge bg-success ms-2";}}
  document.getElementById("adc-cal-a").textContent=state.a!==undefined?state.a.toFixed(6):"--";
  document.getElementById("adc-cal-b").textContent=state.b!==undefined?state.b.toFixed(6):"--";
  document.getElementById("adc-cal-r2").textContent=state.r_squared!==undefined?state.r_squared.toFixed(4):"--";
  document.getElementById("adc-cal-date").textContent=state.calibrated_at||"--";
  var tbody=document.getElementById("adc-cal-points-body");if(!tbody)return;
  var points=state.points||[];
  tbody.innerHTML="";
  ADC_CAL_REFS.forEach(function(ref,i){
    var pt=points[i]||{};var raw=pt.collected?(pt.raw_mv!==undefined?pt.raw_mv.toFixed(1):"--"):"--";
    var isMeasuring=APP_STATE.calibration.calibratingIndex===i;
    var tr=document.createElement("tr");
    tr.innerHTML='<td class="text-center">'+(i+1)+'</td><td class="font-monospace text-center">'+(ref>=0?"+":"")+ref+'</td><td class="font-monospace text-center" id="adc-cal-point-'+i+'">'+raw+'</td><td class="text-center">'+(isMeasuring?'<span class="spinner-border spinner-border-sm"></span> Calibrating...':pt.collected?'&#10003;':'<button class="btn btn-sm btn-outline-primary" onclick="calibratePoint('+i+','+ref+')">Calibrate</button>')+'</td>';
    tbody.appendChild(tr);
  });
  var computeBtn=document.getElementById("adc-cal-compute-btn");
  if(computeBtn){var collected=points.filter(function(p){return p&&p.collected;}).length;computeBtn.disabled=collected<5;computeBtn.onclick=collected>=5?computeCalibration:null;}
  var resetBtn=document.getElementById("adc-cal-reset-btn");if(resetBtn)resetBtn.disabled=!!state.is_default;
}

function calibratePoint(index,refMv){
  APP_STATE.calibration.calibratingIndex=index;updateCalibrationUI(APP_STATE.calibration.adcState);
  sendCommand("adc.cal.measure",{ref_mv:refMv}).then(function(){APP_STATE.calibration.calibratingIndex=-1;loadCalibrationStatus();});
}

function computeCalibration(){
  var btn=document.getElementById("adc-cal-compute-btn");if(btn){btn.disabled=true;btn.textContent="Computing...";}
  sendCommand("adc.cal.compute",{}).then(function(result){
    if(result&&result.status==="ok"){
      var a=result.data?a.data.a:0;var b=result.data?result.data.b:0;var r2=result.data?result.data.r_squared:0;
      if(confirm("New calibration coefficients:\n\na = "+a.toFixed(6)+"\nb = "+b.toFixed(6)+"\nR\u00b2 = "+r2.toFixed(4)+"\n\nSave?")){
        sendCommand("adc.cal.save",{}).then(function(){alert("Calibration saved successfully");loadCalibrationStatus();});
      }else{loadCalibrationStatus();}
    }else{loadCalibrationStatus();}
    if(btn){btn.disabled=false;btn.textContent="Compute & Save";}
  });
}

function resetCalibration(){if(!confirm("Reset ADC calibration?"))return;sendCommand("adc.cal.reset",{}).then(function(){loadCalibrationStatus();});}

function loadCalibrationStatus(){
  sendCommand("adc.cal.get",{}).then(function(result){if(result&&result.status==="ok"&&result.data){APP_STATE.calibration.adcState=result.data;updateCalibrationUI(result.data);}});
}

// Burette calibration
function loadBuretteCalStatus(){
  sendCommand("burette.cal.get",{}).then(function(result){if(result&&result.status==="ok"&&result.data)updateBuretteCalUI(result.data);});
}

function updateBuretteCalUI(state){
  if(!state)return;
  var badge=document.getElementById("bc-cal-status-badge");
  if(badge){badge.textContent=state.is_default?"Default":"Custom";badge.className="badge bg-"+(state.is_default?"secondary":"success")+" ms-2";}
  var spBadge=document.getElementById("bc-sp-cal-status-badge");
  if(spBadge){spBadge.textContent=state.is_default?"Default":"Custom";spBadge.className="badge bg-"+(state.is_default?"secondary":"success")+" ms-2";}
  document.getElementById("bc-steps-per-ml").textContent=state.steps_per_ml!==undefined?state.steps_per_ml.toFixed(1):"--";
  document.getElementById("bc-nominal-vol").textContent=state.nominal_vol!==undefined?state.nominal_vol.toFixed(2):"--";
  document.getElementById("bc-speed-coeff").textContent=state.speed_coeff!==undefined?state.speed_coeff.toFixed(5):"--";
  var rangeEl=document.getElementById("bc-freq-range");rangeEl.textContent=(state.min_freq!=null&&state.max_freq!=null)?state.min_freq+" - "+state.max_freq:"--";
  var freqInput=document.getElementById("bc-vol-freq");if(freqInput&&state.max_freq!=null){freqInput.placeholder=String(Math.round(state.max_freq/2));if(!freqInput.value)freqInput.value=Math.round(state.max_freq/2);}
  document.getElementById("bc-vol-reset-btn").disabled=!!state.is_default;
  document.getElementById("bc-sp-reset-btn").disabled=!!state.is_default;
  if(state.max_freq!=null){var pcts=CONFIG.BC.FREQ_PERCENTAGES;document.querySelectorAll("#bc-speed-body tr").forEach(function(tr,i){var fi=tr.querySelector(".bc-sp-freq");if(fi&&!fi.value)fi.value=Math.round(state.max_freq*(pcts[i]||0.5));});}
  var dateEl=document.getElementById("bc-cal-date");dateEl.textContent=(state.calibration_date>0)?new Date(state.calibration_date*1000).toLocaleString():"--";
  var spDateEl=document.getElementById("bc-sp-cal-date");spDateEl.textContent=(state.calibration_date>0)?new Date(state.calibration_date*1000).toLocaleString():"--";
}

function waitForBuretteIdle(){return new Promise(function(resolve){var poll=setInterval(function(){fetch("/api/status").then(function(r){return r.json();}).then(function(data){if(data&&data.state==="idle"){clearInterval(poll);resolve(true);}}).catch(function(){});},500);});}

function runVolumeCalibration(){
  var freq=parseInt(document.getElementById("bc-vol-freq")?document.getElementById("bc-vol-freq").value:0);if(!freq||freq<=0){alert("Enter frequency");return;}
  var btn=document.getElementById("bc-vol-run-btn");var spinner=document.getElementById("bc-vol-run-spinner");var btnText=document.getElementById("bc-vol-run-text");
  if(btn)btn.disabled=true;if(spinner)spinner.classList.remove("d-none");if(btnText)btnText.textContent=" Filling...";
  sendCommand("burette.cal.run",{mode:"dose",freq_hz:freq}).then(function(result){
    if(result&&result.status==="ok"){
      waitForBuretteIdle().then(function(){sendCommand("burette.cal.getResult",{}).then(function(calResult){
        document.getElementById("bc-vol-run-result").style.display="block";
        document.getElementById("bc-vol-steps").textContent=(calResult&&calResult.data)?(calResult.data.steps_taken??"--"):"--";
        document.getElementById("bc-vol-calc-btn").disabled=false;
        if(spinner)spinner.classList.add("d-none");if(btnText)btnText.textContent="Run";if(btn)btn.disabled=false;
      });});
    }else{if(spinner)spinner.classList.add("d-none");if(btnText)btnText.textContent="Run";if(btn)btn.disabled=false;}
  });
}

function calcVolumeCalibration(){
  var mass=parseFloat(document.getElementById("bc-vol-mass")?document.getElementById("bc-vol-mass").value:"0");
  var temp=parseFloat(document.getElementById("bc-vol-temp")?document.getElementById("bc-vol-temp").value:"25");
  var pressure=parseFloat(document.getElementById("bc-vol-pressure")?document.getElementById("bc-vol-pressure").value:"101.3");
  if(mass<=0){alert("Enter mass");return;}
  sendCommand("burette.cal.calcVolume",{mass_g:mass,temp_c:temp,pressure_kpa:pressure}).then(function(result){
    if(result&&result.status==="ok"&&result.data){var d=result.data;
      document.getElementById("bc-vol-z-factor").textContent=d.z_factor!==undefined?d.z_factor.toFixed(6):"--";
      document.getElementById("bc-vol-actual").textContent=d.actual_volume_ml!==undefined?d.actual_volume_ml.toFixed(4):"--";
      document.getElementById("bc-vol-new-sps").textContent=d.new_steps_per_ml!==undefined?d.new_steps_per_ml.toFixed(1):"--";
      document.getElementById("bc-vol-error").textContent=d.relative_error_pct!==undefined?d.relative_error_pct.toFixed(2)+"%":"--";
      document.getElementById("bc-vol-calc-result").style.display="block";
      document.getElementById("bc-vol-save-btn").disabled=false;
    }
  });
}

function saveBuretteCal(){
  sendCommand("burette.cal.save",{}).then(function(result){
    if(result&&result.status==="ok"){alert("Calibration saved");
      document.getElementById("bc-vol-run-result").style.display="none";
      document.getElementById("bc-vol-calc-result").style.display="none";
      document.getElementById("bc-vol-calc-btn").disabled=true;
      document.getElementById("bc-vol-save-btn").disabled=true;
      document.getElementById("bc-sp-calc-result").style.display="none";
      document.getElementById("bc-sp-save-btn").disabled=true;
      loadBuretteCalStatus();
    }
  });
}

function resetBuretteCal(){if(!confirm("Reset burette calibration?"))return;sendCommand("burette.cal.reset",{}).then(function(){loadBuretteCalStatus();});}

function initSpeedTable(){
  var tbody=document.getElementById("bc-speed-body");if(!tbody)return;
  APP_STATE.calibration.speedRowCount=0;tbody.innerHTML="";
  for(var i=0;i<3;i++)addSpeedRow();
}

function addSpeedRow(){
  if(APP_STATE.calibration.speedRowCount>=CONFIG.BC.MAX_SPEED_ROWS)return;
  var tbody=document.getElementById("bc-speed-body");if(!tbody)return;
  var idx=APP_STATE.calibration.speedRowCount;APP_STATE.calibration.speedRowCount++;
  var tr=document.createElement("tr");tr.id="bc-sp-row-"+idx;
  tr.innerHTML='<td class="text-center">'+(idx+1)+'</td><td><input type="number" class="form-control form-control-sm text-center bc-sp-freq" data-idx="'+idx+'" value="" min="1" step="1"></td><td><input type="number" class="form-control form-control-sm text-center bc-sp-speed" data-idx="'+idx+'" value="" min="0" step="0.1" readonly></td><td><input type="number" class="form-control form-control-sm text-center bc-sp-k" data-idx="'+idx+'" readonly></td><td><input type="number" class="form-control form-control-sm text-center bc-sp-time" data-idx="'+idx+'" readonly></td>';
  tbody.appendChild(tr);
}

function runSpeedSequence(){
  var rows=document.querySelectorAll("#bc-speed-body tr");if(rows.length===0)return;
  var runBtn=document.getElementById("bc-sp-run-btn");var spinner=document.getElementById("bc-sp-run-spinner");var runText=document.getElementById("bc-sp-run-text");var calcBtn=document.getElementById("bc-sp-calc-btn");var resetBtn=document.getElementById("bc-sp-reset-btn");
  runBtn.disabled=true;calcBtn.disabled=true;resetBtn.disabled=true;spinner.classList.remove("d-none");runText.textContent=" Running...";
  var freqs=Array.from(rows).map(function(tr){var inp=tr.querySelector(".bc-sp-freq");return inp?parseInt(inp.value):0;});
  if(freqs.some(function(f){return !f||f<=0;})){alert("Invalid frequencies");spinner.classList.add("d-none");runText.textContent="Run";runBtn.disabled=false;calcBtn.disabled=false;resetBtn.disabled=false;return;}
  sendCommand("burette.cal.runSpeedSeq",{freqs:freqs,speed_ml_min:20}).then(function(result){
    if(result&&result.status==="ok"){
      waitForBuretteIdle().then(function(){
        Promise.all([sendCommand("burette.cal.getResult",{}),sendCommand("burette.cal.get",{})]).then(function(results){
          var calResult=results[0];var calStatus=results[1];
          var nominalVol=calStatus&&calStatus.data?calStatus.data.nominal_vol:null;
          if(calResult&&calResult.data&&calResult.data.speeds){
            rows.forEach(function(tr,i){
              var spd=tr.querySelector(".bc-sp-speed");if(spd&&calResult.data.speeds[i]!=null)spd.value=calResult.data.speeds[i].toFixed(2);
              var freq=parseFloat(tr.querySelector(".bc-sp-freq")?tr.querySelector(".bc-sp-freq").value:0);
              var speed=calResult.data.speeds[i];var kEl=tr.querySelector(".bc-sp-k");if(kEl&&freq>0&&speed!=null)kEl.value=(speed/freq).toFixed(6);
              var timeEl=tr.querySelector(".bc-sp-time");if(timeEl&&nominalVol>0&&speed>0)timeEl.value=Math.round(nominalVol/speed*60);
            });
            if(calStatus&&calStatus.status==="ok"&&calStatus.data)updateBuretteCalUI(calStatus.data);
          }
          spinner.classList.add("d-none");runText.textContent="Run";runBtn.disabled=false;calcBtn.disabled=false;resetBtn.disabled=false;
        });
      });
    }else{
      spinner.classList.add("d-none");runText.textContent="Run";runBtn.disabled=false;calcBtn.disabled=false;resetBtn.disabled=false;
      sendCommand("burette.cal.get",{}).then(function(sr){if(sr&&sr.status==="ok"&&sr.data)updateBuretteCalUI(sr.data);});
    }
  });
}

function calcSpeedCalibration(){
  var rows=document.querySelectorAll("#bc-speed-body tr");
  var measurements=Array.from(rows).reduce(function(acc,tr){var freq=parseFloat(tr.querySelector(".bc-sp-freq")?tr.querySelector(".bc-sp-freq").value:"0");var speed=parseFloat(tr.querySelector(".bc-sp-speed")?tr.querySelector(".bc-sp-speed").value:"0");return freq>0&&speed>0?acc.concat([{freq_hz:freq,speed_ml_min:speed}]):acc;},[]);
  if(measurements.length<2){alert("Need at least 2 measurements");return;}
  sendCommand("burette.cal.calcSpeed",{measurements:measurements}).then(function(result){
    if(result&&result.status==="ok"&&result.data){var d=result.data;
      document.getElementById("bc-sp-k").textContent=d.k!==undefined?d.k.toFixed(6):"--";
      document.getElementById("bc-sp-r2").textContent=d.r_squared!==undefined?d.r_squared.toFixed(4):"--";
      document.getElementById("bc-sp-calc-result").style.display="block";
      document.getElementById("bc-sp-save-btn").disabled=false;
    }
  });
}

window.calibratePoint=calibratePoint;window.computeCalibration=computeCalibration;window.resetCalibration=resetCalibration;window.loadCalibrationStatus=loadCalibrationStatus;
window.loadBuretteCalStatus=loadBuretteCalStatus;window.runVolumeCalibration=runVolumeCalibration;window.calcVolumeCalibration=calcVolumeCalibration;
window.saveBuretteCal=saveBuretteCal;window.resetBuretteCal=resetBuretteCal;window.initSpeedTable=initSpeedTable;window.addSpeedRow=addSpeedRow;
window.runSpeedSequence=runSpeedSequence;window.calcSpeedCalibration=calcSpeedCalibration;
)js"sv;

static constexpr std::string_view INIT_JS = R"js(
var _cmdId=0;

function initTheme(){
  var saved=localStorage.getItem('ecotiter-theme')||'light';
  document.documentElement.setAttribute('data-bs-theme',saved);
  var icon=document.getElementById('theme-icon');if(icon)icon.textContent=saved==='dark'?'&#9728;':'&#9790;';
}

function toggleTheme(){
  var html=document.documentElement;var cur=html.getAttribute('data-bs-theme');var next=cur==='dark'?'light':'dark';
  html.setAttribute('data-bs-theme',next);localStorage.setItem('ecotiter-theme',next);
  var icon=document.getElementById('theme-icon');if(icon)icon.textContent=next==='dark'?'&#9728;':'&#9790;';
}

var sendCommand=async function(cmd,params){
  try{var body={id:++_cmdId,cmd:cmd};if(params)for(var k in params)body[k]=params[k];
    var r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    if(!r.ok)return null;return await r.json();
  }catch(e){return null;}
};

var toggleValve=async function(){
  var newPos=APP_STATE.valve.position==='input'?'output':'input';
  var btn=document.getElementById('hw-valve-toggle-btn');if(btn){btn.disabled=true;btn.textContent='...';}
  try{
    var r=await fetch('/api/valve',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({position:newPos})});
    if(r.ok){var j=await r.json();if(j.valve){APP_STATE.valve.position=j.valve;setText('hw-valve',j.valve);}}
  }catch(e){}finally{if(btn){btn.disabled=false;btn.textContent='Toggle';}}
};

var sendCmd=async function(){
  var input=document.getElementById('cmd-input');var resp=document.getElementById('cmd-response');
  if(!input||!resp)return;var cmd=input.value.trim();if(!cmd)return;
  try{var r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:cmd});resp.value=await r.text();}catch(e){resp.value='Error: '+e.message;}
};

var sendCmdRaw=async function(cmdBody){
  try{var r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cmdBody)});return r.ok?await r.json():null;}catch(e){return null;}
};

var loadInitialLogs=function(){
  fetch('/api/logs?limit=20').then(function(r){if(!r.ok)return null;return r.json();}).then(function(data){
    if(!data||!data.entries)return;
    data.entries.forEach(function(e){APP_STATE.logs.messages.unshift('['+new Date().toLocaleTimeString()+'] ['+(e.level||'INFO')+'] '+(e.msg||''));});
    if(APP_STATE.logs.messages.length>CONFIG.LOG_MAX_ENTRIES)APP_STATE.logs.messages.length=CONFIG.LOG_MAX_ENTRIES;
    renderLogTextarea();
  }).catch(function(e){});
};

var initApp=function(){
  initTheme();
  updateDynamicInput();
  var ac=document.getElementById('ws-autoupdate-check');if(ac)ac.checked=APP_STATE.logs.wsAutoupdate;
  var rc=document.getElementById('ws-raw-json');if(rc)rc.checked=APP_STATE.logs.wsRawJson;
  connectWs();
  initStepperControls();
  loadCalibrationStatus();
  loadBuretteCalStatus();
  initSpeedTable();
  loadInitialLogs();
};

document.addEventListener('DOMContentLoaded',initApp);
window.sendCommand=sendCommand;window.sendCmdRaw=sendCmdRaw;window.toggleTheme=toggleTheme;window.toggleValve=toggleValve;window.sendCmd=sendCmd;
)js"sv;

struct FileEntry {
    const char* path;
    std::string_view content;
};

static constexpr std::array<FileEntry, 10> FILES = {{
    {"/",            INDEX_HTML},
    {"/style.css",   MINIMAL_CSS},
    {"/wifi",        CAPTIVE_HTML},
    {"/js/state.js", STATE_JS},
    {"/js/ws.js",    WS_JS},
    {"/js/ui-update.js", UI_UPDATE_JS},
    {"/js/logs.js",  LOGS_JS},
    {"/js/stepper.js", STEPPER_JS},
    {"/js/calibration.js", CALIBRATION_JS},
    {"/js/init.js",  INIT_JS},
}};

} // namespace detail

[[nodiscard]] inline std::string_view getFile(std::string_view path) {
    for (auto& entry : detail::FILES) {
        if (path == entry.path) {
            return entry.content;
        }
    }
    return {};
}

} // namespace ecotiter::interface::webui