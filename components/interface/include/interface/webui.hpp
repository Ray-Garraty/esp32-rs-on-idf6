#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <array>

namespace ecotiter::interface::webui {

namespace detail {

static constexpr auto INDEX_HTML = R"htmlraw(<!doctype html>
<html lang="en" data-bs-theme="light">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EcoTiter Dashboard</title>
<link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
<link href="https://cdn.jsdelivr.net/npm/bootstrap-icons@1.11.3/font/bootstrap-icons.min.css" rel="stylesheet">
<link href="style.css" rel="stylesheet">
</head>
<body>
<div class="container py-4">
<h3 class="mb-4 text-center">EcoTiter Burette <span id="connection-status" class="badge bg-success"><i class="bi bi-wifi"></i></span></h3>

<div class="accordion accordion-flush" id="main-accordion">

<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button" data-bs-toggle="collapse" data-bs-target="#c-status">Status</button></h2>
<div id="c-status" class="accordion-collapse collapse show">
<div class="card-body">
<table class="table table-sm table-bordered mb-0">
<tbody>
<tr><th>Temperature</th><td id="hw-temperature">--</td></tr>
<tr><th>ADC (mV)</th><td id="hw-adc">--</td></tr>
<tr><th>Valve</th><td id="hw-valve">-- <button class="btn btn-sm btn-primary ms-2" onclick="toggleValve()">Toggle</button></td></tr>
<tr><th>Burette</th><td id="hw-burette">--</td></tr>
</tbody></table>
</div></div></div>

<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button collapsed" data-bs-toggle="collapse" data-bs-target="#c-command">Command Console</button></h2>
<div id="c-command" class="accordion-collapse collapse">
<div class="card-body">
<div class="input-group mb-2">
<input type="text" id="cmd-input" class="form-control font-monospace" placeholder="Enter JSON command">
<button class="btn btn-primary" onclick="sendCmd()">Send</button>
</div>
<textarea id="cmd-response" class="form-control font-monospace" rows="4" readonly></textarea>
</div></div></div>

<div class="accordion-item mb-3">
<h2 class="accordion-header"><button class="accordion-button" data-bs-toggle="collapse" data-bs-target="#c-logs">Log</button></h2>
<div id="c-logs" class="accordion-collapse collapse show">
<div class="card-body">
<div class="d-flex gap-2 mb-2">
<button class="btn btn-sm btn-danger" onclick="clearLogs()">Clear</button>
</div>
<textarea id="log-messages" class="form-control font-monospace" rows="6" readonly></textarea>
</div></div></div>

</div></div>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js"></script>
<script src="js/state.js"></script>
<script src="js/ws.js"></script>
<script src="js/ui-update.js"></script>
<script src="js/logs.js"></script>
<script src="js/init.js"></script>
</body>
</html>)htmlraw";

static constexpr auto CAPTIVE_HTML = R"htmlraw(<!DOCTYPE html>
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
input{width:100%;padding:12px 16px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px}
input:focus{outline:none;border-color:#667eea}
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
<div class="form-group"><label for="password">Password</label><input type="password" id="password" placeholder="Enter password"></div>
<button type="submit">Connect</button>
</form>
<div id="status" class="status"></div>
</div>
<script>
document.getElementById('wifi-form').addEventListener('submit', async function(e){
e.preventDefault();
var ssid=document.getElementById('ssid').value;
var pass=document.getElementById('password').value;
var status=document.getElementById('status');
var btn=this.querySelector('button');
btn.disabled=true;btn.textContent='Connecting...';
status.className='status info';status.textContent='Connecting...';
try{
var r=await fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pass})});
var j=await r.json();
if(j.success){
status.className='status success';status.innerHTML='Connected!<br>Device restarting...';
btn.textContent='Done!';
} else {
status.className='status error';status.textContent='Error: '+(j.message||'Failed');
btn.disabled=false;btn.textContent='Connect';
}}catch(e){
status.className='status error';status.textContent='Connection lost. Device may be restarting.';
btn.disabled=false;btn.textContent='Connect';
}});
</script>
</body>
</html>)htmlraw";

static constexpr auto STYLE_CSS = R"css(
.accordion-button{font-weight:600}
.accordion-button:focus{box-shadow:none;border-color:transparent}
textarea#log-messages{font-size:0.75rem;resize:none}
.table td,.table th{vertical-align:middle}
#cmd-response{font-size:0.75rem}
)css";

static constexpr auto STATE_JS = R"js(
var CONFIG={LOG_MAX_ENTRIES:100};
var APP_STATE={
  logs:{messages:[]},
  valve:{position:'input'},
  connectionAlive:false
};
window.APP_STATE=APP_STATE;
window.CONFIG=CONFIG;
)js";

static constexpr auto WS_JS = R"js(
var ws=null;
var lastMsgTime=Date.now();
var wsReconnectTimer=null;

function connectWs(){
  if(ws&&ws.readyState===WebSocket.OPEN)return;
  var proto=window.location.protocol==='https:'?'wss:':'ws:';
  var url=proto+'//'+window.location.host+'/ws/stream';
  ws=new WebSocket(url);
  ws.onopen=function(){APP_STATE.connectionAlive=true;updateConnStatus();lastMsgTime=Date.now();};
  ws.onmessage=function(e){
    lastMsgTime=Date.now();
    if(!APP_STATE.connectionAlive){APP_STATE.connectionAlive=true;updateConnStatus();}
    try{var data=JSON.parse(e.data);updateUI(data);}catch(err){}
  };
  ws.onclose=function(){
    APP_STATE.connectionAlive=false;updateConnStatus();
    if(wsReconnectTimer)clearTimeout(wsReconnectTimer);
    wsReconnectTimer=setTimeout(connectWs,3000);
  };
  ws.onerror=function(){ws.close();};
}

function updateConnStatus(){
  var el=document.getElementById('connection-status');
  if(!el)return;
  el.className='badge bg-'+(APP_STATE.connectionAlive?'success':'danger');
  el.innerHTML='<i class="bi bi-wifi'+(APP_STATE.connectionAlive?'':'-off')+'"></i>';
}

function sendWs(data){
  if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify(data));
}
)js";

static constexpr auto UI_UPDATE_JS = R"js(
function updateUI(data){
  if(data.temp!==undefined)setText('hw-temperature',data.temp!==null?data.temp.toFixed(1)+' C':'N/A');
  if(data.mv!==undefined)setText('hw-adc',data.mv!==null?data.mv+' mV':'N/A');
  if(data.vlv!==undefined){
    APP_STATE.valve.position=({'in':'input','out':'output'})[data.vlv]||data.vlv||'unknown';
    setText('hw-valve',APP_STATE.valve.position);
  }
  if(data.brt){
    var s=({'idle':'Idle','homing':'Homing','filling':'Filling','emptying':'Emptying','dosing':'Dosing','rinsing':'Rinsing','stopping':'Stopping','error':'Error'})[data.brt.sts]||data.brt.sts||'--';
    setText('hw-burette',s);
  }
}
function setText(id,v){var el=document.getElementById(id);if(el)el.textContent=v;}
function clearLogs(){APP_STATE.logs.messages=[];var el=document.getElementById('log-messages');if(el)el.value='';}
function addLogEntry(msg){APP_STATE.logs.messages.unshift(msg);if(APP_STATE.logs.messages.length>CONFIG.LOG_MAX_ENTRIES)APP_STATE.logs.messages.length=CONFIG.LOG_MAX_ENTRIES;renderLog();}
function renderLog(){var el=document.getElementById('log-messages');if(!el)return;el.value=APP_STATE.logs.messages.join('\n');el.scrollTop=0;}
window.updateUI=updateUI;window.clearLogs=clearLogs;window.addLogEntry=addLogEntry;
)js";

static constexpr auto LOGS_JS = R"js(
window.setLogLevelFilter=function(l){}
)js";

static constexpr auto STEPPER_JS = R"js(
window.initStepperControls=function(){}
)js";

static constexpr auto CALIBRATION_JS = R"js(
window.loadCalibrationStatus=function(){}
window.loadBuretteCalStatus=function(){}
window.initSpeedTable=function(){}
)js";

static constexpr auto INIT_JS = R"js(
var _cmdId=0;
var sendCommand=async function(cmd,params){
  try{
    var body={id:++_cmdId,cmd:cmd};
    if(params)for(var k in params)body[k]=params[k];
    var r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    if(!r.ok)return null;
    return await r.json();
  }catch(e){return null;}
};

var toggleValve=async function(){
  var newPos=APP_STATE.valve.position==='input'?'output':'input';
  var r=await fetch('/api/valve',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({position:newPos})});
  if(r.ok){var j=await r.json();if(j.valve){APP_STATE.valve.position=j.valve;setText('hw-valve',j.valve);}}
};

var sendCmd=async function(){
  var input=document.getElementById('cmd-input');
  var resp=document.getElementById('cmd-response');
  if(!input||!resp)return;
  var cmd=input.value.trim();if(!cmd)return;
  try{
    var r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:cmd});
    var txt=await r.text();
    resp.value=txt;
  }catch(e){resp.value='Error: '+e.message;}
};

var initApp=function(){
  connectWs();
  loadInitialLogs();
};
var loadInitialLogs=function(){
  fetch('/api/logs?limit=20').then(function(r){return r.json();}).then(function(data){
    if(data&&data.entries)data.entries.forEach(function(e){
      var ts=new Date().toLocaleTimeString();
      APP_STATE.logs.messages.push('['+ts+'] ['+(e.level||'INFO')+'] '+(e.msg||''));
    });
    renderLog();
  }).catch(function(e){});
};
document.addEventListener('DOMContentLoaded',initApp);
window.sendCommand=sendCommand;
window.toggleValve=toggleValve;
window.sendCmd=sendCmd;
)js";

struct FileEntry {
    const char* path;
    std::string_view content;
};

static constexpr std::array<FileEntry, 10> FILES = {{
    {"/",            std::string_view(INDEX_HTML, sizeof(INDEX_HTML) - 1)},
    {"/wifi",        std::string_view(CAPTIVE_HTML, sizeof(CAPTIVE_HTML) - 1)},
    {"/style.css",   std::string_view(STYLE_CSS, sizeof(STYLE_CSS) - 1)},
    {"/js/state.js", std::string_view(STATE_JS, sizeof(STATE_JS) - 1)},
    {"/js/ws.js",    std::string_view(WS_JS, sizeof(WS_JS) - 1)},
    {"/js/ui-update.js", std::string_view(UI_UPDATE_JS, sizeof(UI_UPDATE_JS) - 1)},
    {"/js/logs.js",  std::string_view(LOGS_JS, sizeof(LOGS_JS) - 1)},
    {"/js/stepper.js", std::string_view(STEPPER_JS, sizeof(STEPPER_JS) - 1)},
    {"/js/calibration.js", std::string_view(CALIBRATION_JS, sizeof(CALIBRATION_JS) - 1)},
    {"/js/init.js",  std::string_view(INIT_JS, sizeof(INIT_JS) - 1)},
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
