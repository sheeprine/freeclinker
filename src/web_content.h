#pragma once

// Served at GET / by WebConfigServer.  Communicates with the device via:
//   GET  /api/config  — read current settings (JSON)
//   POST /api/config  — write settings patch (JSON), returns updated settings
//   POST /api/cli     — send one CLI command (text/plain), returns response text

static const char WEB_INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>FreeCLinker — Config</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #141414; color: #d4d4d4;
      font: 14px/1.5 'Courier New', monospace;
      height: 100dvh; display: flex; flex-direction: column; overflow: hidden;
    }
    header {
      display: flex; align-items: center; gap: 12px;
      padding: 10px 16px; background: #1e1e1e;
      border-bottom: 1px solid #2d2d2d; flex-shrink: 0;
    }
    .brand { font-size: 13px; font-weight: bold; color: #60a5fa; margin-right: auto; }
    .dot { width: 8px; height: 8px; border-radius: 50%; background: #4ade80;
           box-shadow: 0 0 6px #4ade8088; flex-shrink: 0; }
    .hdr-status { font-size: 12px; color: #4ade80; }
    select {
      background: #2a2a2a; color: #d4d4d4; border: 1px solid #3a3a3a;
      border-radius: 4px; padding: 3px 6px; font: inherit; font-size: 12px;
    }
    button {
      font: inherit; font-size: 12px; padding: 4px 12px; border-radius: 4px;
      border: 1px solid #3a3a3a; background: #2a2a2a; color: #d4d4d4;
      cursor: pointer; transition: background 0.1s;
    }
    button:hover:not(:disabled) { background: #383838; }
    button:disabled { opacity: 0.35; cursor: default; }
    button.primary { background: #1d4ed8; border-color: #2563eb; color: #fff; }
    button.primary:hover:not(:disabled) { background: #2563eb; }
    .tab-bar {
      display: flex; background: #1a1a1a; border-bottom: 1px solid #2d2d2d;
      padding: 0 12px; flex-shrink: 0;
    }
    .tab {
      padding: 8px 16px; border: none; border-bottom: 2px solid transparent;
      border-radius: 0; background: transparent; color: #6b7280;
      cursor: pointer; font-size: 13px; margin-bottom: -1px; transition: color 0.1s;
    }
    .tab:hover { background: transparent; color: #9ca3af; }
    .tab.active { color: #60a5fa; border-bottom-color: #60a5fa; }
    .panels { flex: 1; position: relative; overflow: hidden; }
    .panel { position: absolute; inset: 0; display: none; flex-direction: column; overflow: hidden; }
    .panel.active { display: flex; }
    #panel-config { overflow-y: auto; align-items: center; padding: 28px 16px; }
    .config-card {
      width: 100%; max-width: 460px; background: #1e1e1e;
      border: 1px solid #2d2d2d; border-radius: 8px; overflow: hidden;
    }
    .card-header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 14px 18px; border-bottom: 1px solid #2d2d2d;
    }
    .card-header h2 { font-size: 13px; color: #9ca3af; font-weight: normal;
                      letter-spacing: 0.05em; text-transform: uppercase; }
    .cfg-field {
      display: flex; align-items: center; gap: 16px;
      padding: 16px 18px; border-bottom: 1px solid #252525;
    }
    .cfg-field:last-of-type { border-bottom: none; }
    .cfg-field-text { flex: 1; min-width: 0; }
    .cfg-field-name { font-size: 14px; color: #e2e8f0; margin-bottom: 2px; }
    .cfg-field-desc { font-size: 11px; color: #6b7280; line-height: 1.4; }
    .cfg-field-desc strong { color: #f87171; }
    .switch { position: relative; display: inline-flex; width: 42px; height: 24px; flex-shrink: 0; cursor: pointer; }
    .switch input { opacity: 0; width: 0; height: 0; position: absolute; }
    .track { width: 42px; height: 24px; background: #374151; border-radius: 12px;
             transition: background 0.2s; position: relative; }
    .thumb { position: absolute; top: 3px; left: 3px; width: 18px; height: 18px;
             background: #9ca3af; border-radius: 50%; transition: transform 0.2s, background 0.2s; }
    .switch input:checked ~ .track { background: #1d4ed8; }
    .switch input:checked ~ .track .thumb { transform: translateX(18px); background: #fff; }
    .num-wrap { display: flex; align-items: center; gap: 6px; margin-top: 8px; }
    .num-wrap input[type=number] {
      width: 90px; background: #2a2a2a; border: 1px solid #3a3a3a; border-radius: 4px;
      color: #d4d4d4; font: inherit; font-size: 14px; padding: 4px 8px; text-align: right;
    }
    .num-wrap input[type=number]:focus { outline: none; border-color: #2563eb; }
    .num-unit { color: #6b7280; font-size: 12px; }
    .num-equiv { color: #4b5563; font-size: 11px; margin-left: 4px; }
    .card-footer {
      display: flex; align-items: center; justify-content: flex-end;
      gap: 10px; padding: 12px 18px; border-top: 1px solid #2d2d2d;
    }
    .saved-msg { font-size: 12px; color: #4ade80; opacity: 0; transition: opacity 0.4s; margin-right: auto; }
    #terminal { flex: 1; overflow-y: auto; padding: 12px 16px; }
    .line { white-space: pre-wrap; word-break: break-all; line-height: 1.55; }
    .line.sys { color: #6b7280; }
    .line.cmd { color: #60a5fa; }
    .line.err { color: #f87171; }
    .input-bar {
      display: flex; align-items: center; gap: 8px;
      padding: 8px 16px; border-top: 1px solid #2d2d2d;
      background: #1a1a1a; flex-shrink: 0;
    }
    .prompt { color: #4b5563; user-select: none; }
    #cmdInput {
      flex: 1; background: transparent; border: none; outline: none;
      color: #60a5fa; font: inherit; caret-color: #60a5fa;
    }
    #cmdInput::placeholder { color: #374151; }
  </style>
</head>
<body>

<header>
  <span class="brand">FreeCLinker</span>
  <div class="dot"></div>
  <span class="hdr-status">WiFi AP</span>
</header>

<div class="tab-bar">
  <button class="tab active" data-tab="config">Easy Config</button>
  <button class="tab"        data-tab="cli">CLI</button>
</div>

<div class="panels">

  <!-- Easy Config -->
  <div id="panel-config" class="panel active">

    <!-- Camera & Recording card -->
    <div class="config-card">
      <div class="card-header">
        <h2>Camera &amp; Recording</h2>
        <button id="readBtn">Refresh</button>
      </div>

      <!-- Camera type -->
      <div class="cfg-field">
        <div class="cfg-field-text">
          <div class="cfg-field-name">Camera type</div>
          <div class="cfg-field-desc">Select the camera protocol. <strong>Reboot required to apply.</strong></div>
        </div>
        <select id="cameraType">
          <option value="0">DJI Action</option>
          <option value="1">GoPro</option>
        </select>
      </div>

      <!-- Stop on disarm -->
      <div class="cfg-field">
        <div class="cfg-field-text">
          <div class="cfg-field-name">Stop recording on disarm</div>
          <div class="cfg-field-desc">When off, the camera keeps recording regardless of FC arm state.</div>
        </div>
        <label class="switch" title="Stop on disarm">
          <input type="checkbox" id="stopOnDisarm" checked>
          <span class="track"><span class="thumb"></span></span>
        </label>
      </div>

      <!-- Disarm delay -->
      <div class="cfg-field" style="flex-direction:column;align-items:flex-start;">
        <div class="cfg-field-name">Disarm delay</div>
        <div class="cfg-field-desc">Delay between FC disarm and stopping the recording. 0 = stop immediately.</div>
        <div class="num-wrap">
          <input type="number" id="disarmDelay" min="0" max="60000" step="500" value="0">
          <span class="num-unit">ms</span>
          <span class="num-equiv" id="delayEquiv"></span>
        </div>
      </div>

      <div class="card-footer">
        <span class="saved-msg" id="savedMsg"></span>
        <button id="applyBtn" class="primary">Apply</button>
      </div>
    </div>

    <!-- Camera Mode Switch card -->
    <div class="config-card" style="margin-top:16px;">
      <div class="card-header">
        <h2>Camera Mode Switch</h2>
      </div>

      <!-- AUX Channel -->
      <div class="cfg-field">
        <div class="cfg-field-text">
          <div class="cfg-field-name">AUX Channel</div>
          <div class="cfg-field-desc">RC channel monitored for camera mode switching. Set to Disabled if not used.</div>
        </div>
        <select id="auxChannel">
          <option value="0">Disabled</option>
          <option value="1">AUX 1</option>
          <option value="2">AUX 2</option>
          <option value="3">AUX 3</option>
          <option value="4">AUX 4</option>
          <option value="5">AUX 5</option>
          <option value="6">AUX 6</option>
          <option value="7">AUX 7</option>
          <option value="8">AUX 8</option>
          <option value="9">AUX 9</option>
          <option value="10">AUX 10</option>
          <option value="11">AUX 11</option>
          <option value="12">AUX 12</option>
        </select>
      </div>

      <!-- AUX Mode -->
      <div class="cfg-field">
        <div class="cfg-field-text">
          <div class="cfg-field-name">Mode when high (&gt; 1500 µs)</div>
          <div class="cfg-field-desc">Camera mode to activate when the AUX channel goes high. Returns to Video when low.</div>
        </div>
        <select id="auxMode">
          <option value="0">Slow Motion</option>
          <option value="1">Video</option>
          <option value="2">Timelapse</option>
          <option value="5">Photo</option>
          <option value="10">Hyperlapse</option>
        </select>
      </div>

      <div class="card-footer">
        <span class="saved-msg" id="auxSavedMsg"></span>
        <button id="auxApplyBtn" class="primary">Apply</button>
      </div>
    </div>

  </div><!-- /panel-config -->

  <!-- CLI -->
  <div id="panel-cli" class="panel">
    <div id="terminal"></div>
    <div class="input-bar">
      <span class="prompt">&gt;</span>
      <input id="cmdInput" type="text" placeholder="type a command and press Enter…" autocomplete="off" spellcheck="false">
      <button id="sendBtn">Send</button>
    </div>
  </div>

</div><!-- /panels -->

<script>
'use strict';

const terminal     = document.getElementById('terminal');
const cmdInput     = document.getElementById('cmdInput');
const sendBtn      = document.getElementById('sendBtn');
const readBtn      = document.getElementById('readBtn');
const applyBtn     = document.getElementById('applyBtn');
const auxApplyBtn  = document.getElementById('auxApplyBtn');
const stopToggle   = document.getElementById('stopOnDisarm');
const delayInput   = document.getElementById('disarmDelay');
const delayEquiv   = document.getElementById('delayEquiv');
const savedMsg     = document.getElementById('savedMsg');
const auxChannelSel = document.getElementById('auxChannel');
const auxModeSel   = document.getElementById('auxMode');
const auxSavedMsg  = document.getElementById('auxSavedMsg');
const cameraTypeSel = document.getElementById('cameraType');

// ── Tabs ──────────────────────────────────────────────────────────────────────
let activeTab = 'config';
document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    const target = tab.dataset.tab;
    if (target === activeTab) return;
    activeTab = target;
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t === tab));
    document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + target));
    if (target === 'config') loadConfig();
    if (target === 'cli') setTimeout(() => cmdInput.focus(), 0);
  });
});

// ── Terminal log ──────────────────────────────────────────────────────────────
function log(text, cls = '') {
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    if (i < lines.length - 1 || lines[i] !== '') {
      const div = document.createElement('div');
      div.className = 'line' + (cls ? ' ' + cls : '');
      div.textContent = lines[i];
      terminal.appendChild(div);
    }
  }
  terminal.scrollTop = terminal.scrollHeight;
}

// ── Config API ────────────────────────────────────────────────────────────────
async function loadConfig() {
  try {
    const r = await fetch('/api/config');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    cameraTypeSel.value  = j.camera_type   ?? 0;
    stopToggle.checked   = j.stop_on_disarm ?? true;
    delayInput.value     = j.disarm_delay  ?? 0;
    auxChannelSel.value  = j.aux_channel   ?? 0;
    auxModeSel.value     = j.aux_mode      ?? 0;
    updateDelayEquiv();
    updateAuxModeState();
  } catch (e) {
    console.error('loadConfig:', e);
  }
}

async function saveConfig(data) {
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data)
  });
  if (!r.ok) throw new Error('Save failed: HTTP ' + r.status);
}

// ── CLI API ───────────────────────────────────────────────────────────────────
const history = [];
let histIdx = -1, histDraft = '';

async function sendCommand(cmd) {
  log('> ' + cmd, 'cmd');
  try {
    const r = await fetch('/api/cli', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: cmd
    });
    const text = await r.text();
    if (text.trim()) log(text.trimEnd());
  } catch (e) {
    log('Error: ' + e.message, 'err');
  }
}

async function sendCurrent() {
  const cmd = cmdInput.value.trim();
  if (!cmd) return;
  history.unshift(cmd);
  if (history.length > 100) history.pop();
  histIdx = -1; histDraft = '';
  cmdInput.value = '';
  await sendCommand(cmd);
}

cmdInput.addEventListener('keydown', e => {
  if (e.key === 'Enter') {
    sendCurrent();
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    if (histIdx === -1) histDraft = cmdInput.value;
    if (histIdx < history.length - 1) histIdx++;
    cmdInput.value = history[histIdx] ?? histDraft;
    cmdInput.setSelectionRange(cmdInput.value.length, cmdInput.value.length);
  } else if (e.key === 'ArrowDown') {
    e.preventDefault();
    if (histIdx > 0) histIdx--;
    else histIdx = -1;
    cmdInput.value = histIdx === -1 ? histDraft : history[histIdx];
    cmdInput.setSelectionRange(cmdInput.value.length, cmdInput.value.length);
  }
});

// ── Easy Config actions ───────────────────────────────────────────────────────
readBtn.addEventListener('click', loadConfig);

applyBtn.addEventListener('click', async () => {
  try {
    await saveConfig({
      camera_type:    parseInt(cameraTypeSel.value),
      stop_on_disarm: stopToggle.checked,
      disarm_delay:   parseInt(delayInput.value) || 0
    });
    flashSaved(savedMsg);
  } catch (e) { console.error(e); }
});

auxApplyBtn.addEventListener('click', async () => {
  try {
    await saveConfig({
      aux_channel: parseInt(auxChannelSel.value),
      aux_mode:    parseInt(auxModeSel.value)
    });
    flashSaved(auxSavedMsg);
  } catch (e) { console.error(e); }
});

auxChannelSel.addEventListener('change', updateAuxModeState);
sendBtn.addEventListener('click', sendCurrent);

function updateAuxModeState() {
  auxModeSel.disabled = parseInt(auxChannelSel.value) === 0;
}

function flashSaved(el) {
  el.textContent = 'Applied';
  el.style.opacity = '1';
  setTimeout(() => { el.style.opacity = '0'; }, 2500);
}

delayInput.addEventListener('input', updateDelayEquiv);
function updateDelayEquiv() {
  const ms = parseInt(delayInput.value) || 0;
  delayEquiv.textContent = ms === 0 ? '(immediate)' : `= ${(ms/1000).toFixed(ms%1000===0?0:1)} s`;
}

// ── Init ──────────────────────────────────────────────────────────────────────
updateDelayEquiv();
loadConfig();
</script>
</body>
</html>
)HTML";
