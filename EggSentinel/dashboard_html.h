// ================================================================
//  EGG SENTINEL — Web Dashboard (single-file HTML/CSS/JS)
//  Served from PROGMEM, no external dependencies, works offline.
//  This page (unlike the setup page) DOES use JavaScript/fetch —
//  that's fine here because by the time you're looking at this
//  dashboard, you're on your normal home Wi-Fi in a regular full
//  browser, not inside a captive-portal popup.
// ================================================================
#pragma once

const char DASHBOARD_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Egg Sentinel</title>
<style>
  :root {
    --bg: #0b0d12;
    --panel: #12151c;
    --border: #232834;
    --text: #e6e9ef;
    --muted: #8b93a7;
    --ok: #3ddc84;
    --warn: #f5a623;
    --alert: #ff4d4f;
    --accent: #5b8cff;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px; background: var(--bg); color: var(--text);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  }
  .wrap { max-width: 720px; margin: 0 auto; }
  h1 { font-size: 22px; display: flex; align-items: center; gap: 10px; margin-bottom: 4px;}
  .egg { font-size: 26px; }
  .sub { color: var(--muted); font-size: 13px; margin-bottom: 24px; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 20px; }
  .card {
    background: var(--panel); border: 1px solid var(--border); border-radius: 12px;
    padding: 16px;
  }
  .card .label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .05em; margin-bottom: 6px;}
  .card .value { font-size: 22px; font-weight: 600; }
  .status-banner {
    border-radius: 12px; padding: 16px; margin-bottom: 20px; font-weight: 600;
    display: flex; justify-content: space-between; align-items: center; gap: 10px;
  }
  .status-ok { background: rgba(61,220,132,0.12); border: 1px solid var(--ok); color: var(--ok); }
  .status-warning { background: rgba(245,166,35,0.12); border: 1px solid var(--warn); color: var(--warn); }
  .status-alert { background: rgba(255,77,79,0.12); border: 1px solid var(--alert); color: var(--alert); }
  button {
    background: var(--accent); color: white; border: none; border-radius: 8px;
    padding: 8px 14px; font-size: 13px; cursor: pointer; font-weight: 600;
  }
  button.secondary { background: transparent; border: 1px solid var(--border); color: var(--text); }
  button.danger { background: transparent; border: 1px solid var(--alert); color: var(--alert); }
  button:hover { opacity: 0.88; }
  button:disabled { opacity: 0.5; cursor: default; }
  section { margin-bottom: 28px; }
  section h2 { font-size: 15px; color: var(--muted); margin-bottom: 10px; font-weight: 600; text-transform: uppercase; letter-spacing: .04em;}
  .log-item {
    border-bottom: 1px solid var(--border); padding: 10px 0; font-size: 13px;
    display: flex; gap: 10px; align-items: flex-start;
  }
  .log-item:last-child { border-bottom: none; }
  .log-time { color: var(--muted); white-space: nowrap; font-size: 12px; min-width: 70px;}
  .tag { font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: 700; white-space: nowrap;}
  .tag-ALERT { background: var(--alert); color: white; }
  .tag-INFO { background: var(--border); color: var(--muted); }
  .tag-BOOT { background: var(--accent); color: white; }
  .tag-SCAN { background: #2d3548; color: var(--accent); }
  .tag-BLE { background: #3a2d54; color: #b89cff; }
  .device-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid var(--border); font-size: 13px;}
  .device-row:last-child { border-bottom: none; }
  .settings-row { display: flex; flex-direction: column; gap: 6px; margin-bottom: 14px; }
  .settings-row label { font-size: 12px; color: var(--muted); }
  .settings-row input {
    background: var(--bg); border: 1px solid var(--border); color: var(--text);
    padding: 9px 12px; border-radius: 8px; font-size: 13px;
  }
  .settings-actions { display: flex; gap: 10px; flex-wrap: wrap; }
  .pill { font-size: 11px; padding: 3px 8px; border-radius: 20px; background: var(--border); color: var(--muted);}
  .pill.on { background: rgba(61,220,132,0.15); color: var(--ok); }
  .scan-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
  .scan-summary { font-size: 13px; color: var(--muted); margin-top: 8px; }
  .danger-zone { border: 1px solid var(--alert); border-radius: 12px; padding: 16px; background: rgba(255,77,79,0.05); }
  .danger-zone p { font-size: 12px; color: var(--muted); margin: 0 0 12px; line-height: 1.5; }
  .toggle-row { display: flex; justify-content: space-between; align-items: center; gap: 12px; }
  .toggle-row .toggle-label { font-size: 13px; }
  .toggle-row .toggle-label .desc { display: block; font-size: 11px; color: var(--muted); margin-top: 2px; }
  .switch { position: relative; display: inline-block; width: 46px; height: 26px; flex-shrink: 0; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; cursor: pointer; inset: 0; background-color: var(--border);
    transition: .2s; border-radius: 26px;
  }
  .slider:before {
    position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 3px;
    background-color: white; transition: .2s; border-radius: 50%;
  }
  input:checked + .slider { background-color: var(--accent); }
  input:checked + .slider:before { transform: translateX(20px); }
  footer { color: var(--muted); font-size: 11px; text-align: center; margin-top: 30px;}
  #toast {
    position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%);
    background: var(--panel); border: 1px solid var(--border); padding: 10px 18px;
    border-radius: 8px; font-size: 13px; display: none; max-width: 90%; text-align: center;
  }
</style>
</head>
<body>
<div class="wrap">
  <h1><span class="egg">🥚</span> Egg Sentinel</h1>
  <div class="sub" id="subtitle">Loading device status…</div>

  <div class="status-banner status-ok" id="statusBanner">
    <span id="statusText">Checking status…</span>
    <button class="secondary" onclick="ackAlert()">Acknowledge</button>
  </div>

  <div class="grid">
    <div class="card"><div class="label">Uptime</div><div class="value" id="uptime">—</div></div>
    <div class="card"><div class="label">Wi-Fi Signal</div><div class="value" id="rssi">—</div></div>
    <div class="card"><div class="label">Deauth Frames</div><div class="value" id="deauth">—</div></div>
    <div class="card"><div class="label">LAN Devices</div><div class="value" id="lanCount">—</div></div>
    <div class="card" id="bleCard1" style="display:none"><div class="label">BLE Devices Nearby</div><div class="value" id="bleCount">—</div></div>
    <div class="card" id="bleCard2" style="display:none"><div class="label">BLE Spam Events</div><div class="value" id="bleSpam">—</div></div>
  </div>

  <section>
    <h2>Network scan</h2>
    <div class="card">
      <div class="scan-row">
        <span style="font-size:13px;color:var(--muted)">Run all checks immediately instead of waiting for the next scheduled scan</span>
        <button id="scanBtn" onclick="scanNow()">Scan now</button>
      </div>
      <div class="scan-summary" id="scanSummary"></div>
    </div>
  </section>

  <section>
    <h2>Devices on your network</h2>
    <div class="card" id="deviceList"><div class="device-row"><span>Loading…</span></div></div>
  </section>

  <section>
    <h2>Event log</h2>
    <div class="card" id="logList"><div class="log-item"><span>Loading…</span></div></div>
  </section>

  <section>
    <h2>Telegram alerts <span class="pill" id="tgPill">checking…</span></h2>
    <div class="card">
      <div class="settings-row">
        <label>Bot Token</label>
        <input type="text" id="tgToken" placeholder="123456789:AAH...">
      </div>
      <div class="settings-row">
        <label>Chat ID</label>
        <input type="text" id="tgChatId" placeholder="987654321">
      </div>
      <div class="settings-actions">
        <button onclick="saveSettings()">Save</button>
        <button class="secondary" onclick="testTelegram()">Send test alert</button>
      </div>
    </div>
  </section>

  <section>
    <h2>Bluetooth detection</h2>
    <div class="card">
      <div class="toggle-row">
        <div class="toggle-label">
          Scan for Bluetooth threats
          <span class="desc">Runs short 6s scans every 30s, shares the radio with Wi-Fi detection. Off by default.</span>
        </div>
        <label class="switch">
          <input type="checkbox" id="bleToggle" onchange="toggleBle()">
          <span class="slider"></span>
        </label>
      </div>
    </div>
  </section>

  <section id="bleListSection" style="display:none">
    <h2>Bluetooth devices nearby</h2>
    <div class="card" id="bleList"><div class="device-row"><span>Loading…</span></div></div>
  </section>

  <section>
    <h2>Wi-Fi setup</h2>
    <div class="danger-zone">
      <p>
        Need to connect Egg Sentinel to a different network? This clears the
        saved Wi-Fi and restarts the egg into setup mode — join its
        "EggSentinel_Setup" hotspot again to pick a new network. Your
        Telegram settings are kept.
      </p>
      <button class="danger" onclick="resetWifi()">Redo Wi-Fi setup</button>
    </div>
  </section>

  <footer>Egg Sentinel — defensive Wi-Fi monitor. Refreshes every 5s.</footer>
</div>
<div id="toast"></div>

<script>
function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  setTimeout(() => t.style.display = 'none', 3000);
}

async function refresh() {
  try {
    const res = await fetch('/api/status');
    const d = await res.json();
    document.getElementById('subtitle').textContent = d.device + ' · ' + d.ip + ' · ' + d.ssid;
    document.getElementById('uptime').textContent = d.uptime;
    document.getElementById('rssi').textContent = d.rssi + ' dBm';
    document.getElementById('deauth').textContent = d.deauthLifetime;
    document.getElementById('lanCount').textContent = d.lanDevices;

    const banner = document.getElementById('statusBanner');
    const statusText = document.getElementById('statusText');
    banner.className = 'status-banner status-' + d.status;
    statusText.textContent = d.statusMessage;

    const tgPill = document.getElementById('tgPill');
    tgPill.textContent = d.telegramEnabled ? 'enabled' : 'not configured';
    tgPill.className = 'pill' + (d.telegramEnabled ? ' on' : '');

    document.getElementById('scanSummary').textContent = d.lastScanSummary || '';

    document.getElementById('bleToggle').checked = d.bleEnabled;
    const bleVisible = d.bleEnabled;
    document.getElementById('bleCard1').style.display = bleVisible ? '' : 'none';
    document.getElementById('bleCard2').style.display = bleVisible ? '' : 'none';
    document.getElementById('bleListSection').style.display = bleVisible ? '' : 'none';
    if (bleVisible) {
      document.getElementById('bleCount').textContent = d.bleDevices;
      document.getElementById('bleSpam').textContent = d.bleSpamCount;
      const bleList = document.getElementById('bleList');
      bleList.innerHTML = '';
      if (!d.bleList || d.bleList.length === 0) {
        bleList.innerHTML = '<div class="device-row"><span>No BLE devices seen yet</span></div>';
      } else {
        d.bleList.forEach(b => {
          const row = document.createElement('div');
          row.className = 'device-row';
          row.innerHTML = '<span>' + (b.name || '(unnamed)') + '</span><span style="color:var(--muted)">' + b.addr + ' · ' + b.rssi + 'dBm</span>';
          bleList.appendChild(row);
        });
      }
    }

    const list = document.getElementById('deviceList');
    list.innerHTML = '';
    if (d.devices.length === 0) {
      list.innerHTML = '<div class="device-row"><span>No devices detected yet</span></div>';
    }
    d.devices.forEach(dev => {
      const row = document.createElement('div');
      row.className = 'device-row';
      row.innerHTML = '<span>' + dev.ip + '</span><span style="color:var(--muted)">' + dev.mac + '</span>';
      list.appendChild(row);
    });
  } catch (e) { console.error(e); }

  try {
    const res2 = await fetch('/api/log');
    const logs = await res2.json();
    const logList = document.getElementById('logList');
    logList.innerHTML = '';
    if (logs.length === 0) {
      logList.innerHTML = '<div class="log-item"><span>No events yet</span></div>';
    }
    logs.forEach(l => {
      const row = document.createElement('div');
      row.className = 'log-item';
      const mins = Math.floor(l.t / 60000);
      row.innerHTML = '<span class="log-time">' + mins + 'm ago</span>' +
                       '<span class="tag tag-' + l.type + '">' + l.type + '</span>' +
                       '<span>' + l.message.replace(/\n/g, ' · ') + '</span>';
      logList.appendChild(row);
    });
  } catch (e) { console.error(e); }
}

async function ackAlert() {
  await fetch('/api/ack', { method: 'POST' });
  refresh();
}

async function scanNow() {
  const btn = document.getElementById('scanBtn');
  btn.disabled = true;
  btn.textContent = 'Scanning…';
  try {
    const res = await fetch('/api/scan-now', { method: 'POST' });
    const d = await res.json();
    showToast(d.summary || 'Scan complete');
  } catch (e) {
    showToast('Scan failed');
  }
  btn.disabled = false;
  btn.textContent = 'Scan now';
  refresh();
}

async function saveSettings() {
  const token = document.getElementById('tgToken').value;
  const chatId = document.getElementById('tgChatId').value;
  const res = await fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({token, chatId})
  });
  const d = await res.json();
  showToast(d.ok ? 'Settings saved' : 'Save failed');
  refresh();
}

async function testTelegram() {
  const res = await fetch('/api/test-telegram', { method: 'POST' });
  const d = await res.json();
  showToast(d.ok ? 'Test alert sent — check Telegram' : (d.error || 'Failed'));
}

async function toggleBle() {
  const checkbox = document.getElementById('bleToggle');
  const wantEnabled = checkbox.checked;
  checkbox.disabled = true;
  try {
    const res = await fetch('/api/ble-toggle', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ enabled: wantEnabled })
    });
    const d = await res.json();
    showToast(d.ok ? (wantEnabled ? 'Bluetooth detection on' : 'Bluetooth detection off') : 'Failed to change mode');
    if (!d.ok) checkbox.checked = !wantEnabled; // revert on failure
  } catch (e) {
    showToast('Failed to change mode');
    checkbox.checked = !wantEnabled;
  }
  checkbox.disabled = false;
  refresh();
}

async function resetWifi() {
  if (!confirm('This will disconnect Egg Sentinel from your Wi-Fi and restart it into setup mode. Continue?')) return;
  showToast('Restarting into setup mode…');
  try {
    await fetch('/api/reset-wifi', { method: 'POST' });
  } catch (e) {
    // expected — the device reboots and drops the connection
  }
  document.body.innerHTML = '<div style="padding:40px;text-align:center;color:#8b93a7;font-family:-apple-system,sans-serif;">' +
    '🥚 Egg Sentinel is restarting into setup mode.<br><br>' +
    'Connect to the <strong style="color:#e6e9ef">EggSentinel_Setup</strong> Wi-Fi network to continue.</div>';
}

refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
)HTMLPAGE";
