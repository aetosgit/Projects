/*
  School Noise Monitoring Dashboard — Central ESP32
  Four NodeMCU ESP8266 stations:
    noise01 = Classroom 1
    noise02 = Classroom 2
    noise03 = Library
    noise04 = Admin / Reception

  IMPORTANT:
  The displayed dB-eq value is an EDUCATIONAL SIMULATION derived from
  microphone AC-RMS ADC counts. It is NOT calibrated dB(A), and must not
  be used to claim regulatory compliance. Use a reference sound-level meter
  to calibrate the microphone system before reporting actual dB(A).
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>

// ===== SCHOOL WI-FI SETTINGS =====
const char* WIFI_SSID = "TII-School-Guest";
const char* WIFI_PASSWORD = "Guest@2026";
// =================================

WebServer server(80);

const char* NODE_IDS[4] = {"noise01", "noise02", "noise03", "noise04"};
const char* PLACE_NAMES[4] = {
  "Classroom 1",
  "Classroom 2",
  "Library",
  "Admin / Reception"
};

const unsigned long OFFLINE_AFTER_MS = 10000;

struct Station {
  String id;
  String place;
  float rms;
  int peak;
  unsigned long lastSeen;
  bool received;
};

Station stations[4];

// Educational simulation only.
// Arbitrary reference: RMS 0.30 ADC counts maps to 40 dB-eq.
// This is NOT a physical sound-pressure calibration.
float simulatedDbEq(float rms) {
  const float referenceRms = 0.30f;
  const float referenceDb = 40.0f;

  if (rms <= 0.0f) return 30.0f;

  float value = referenceDb + 20.0f * log10f(rms / referenceRms);
  if (value < 30.0f) value = 30.0f;
  if (value > 90.0f) value = 90.0f;
  return value;
}

int stationIndex(const String& id) {
  for (int i = 0; i < 4; i++) {
    if (id == stations[i].id) return i;
  }
  return -1;
}

String jsonEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') out += '\\';
    if (c >= 32) out += c;
  }
  return out;
}

void handleReading() {
  String id = server.arg("node_id");
  int idx = stationIndex(id);

  if (idx < 0) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"unknown node_id\"}");
    return;
  }

  stations[idx].rms = server.arg("rms").toFloat();
  stations[idx].peak = server.arg("peak").toInt();
  stations[idx].lastSeen = millis();
  stations[idx].received = true;

  // CSV includes raw sensor readings plus the simulated display value.
  File f = LittleFS.open("/noise_log_db_eq.csv", FILE_APPEND);
  if (f) {
    float dbEq = simulatedDbEq(stations[idx].rms);
    f.printf("%lu,%s,%s,%.2f,%.1f,%d\n",
             (unsigned long)(millis() / 1000),
             stations[idx].id.c_str(),
             stations[idx].place.c_str(),
             stations[idx].rms,
             dbEq,
             stations[idx].peak);
    f.close();
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus() {
  String out = "{\"stations\":[";

  for (int i = 0; i < 4; i++) {
    if (i) out += ",";

    bool online = stations[i].received &&
                  (millis() - stations[i].lastSeen <= OFFLINE_AFTER_MS);
    float dbEq = simulatedDbEq(stations[i].rms);

    out += "{\"node_id\":\"" + jsonEscape(stations[i].id) +
           "\",\"place\":\"" + jsonEscape(stations[i].place) +
           "\",\"rms\":" + String(stations[i].rms, 2) +
           ",\"db_eq\":" + String(dbEq, 1) +
           ",\"peak\":" + String(stations[i].peak) +
           ",\"online\":" + String(online ? "true" : "false") +
           ",\"age_ms\":" +
           String(stations[i].received ? (millis() - stations[i].lastSeen) : 0) +
           "}";
  }

  out += "],\"uptime_s\":" + String(millis() / 1000) + "}";
  server.send(200, "application/json", out);
}

const char DASHBOARD_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#08111f">
<title>School Noise Monitoring Dashboard</title>
<style>
:root{color-scheme:dark;--bg:#08111f;--panel:#111e30;--line:#24364e;--muted:#a8b7cc;--white:#f5f8ff;--green:#22c55e;--orange:#f59e0b;--red:#ef4444;--blue:#60a5fa}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(ellipse at top,#152b46,var(--bg) 65%);font-family:Arial,Helvetica,sans-serif;color:var(--white);min-height:100vh;padding:22px}
.wrap{max-width:1400px;margin:auto}
.head{display:flex;justify-content:space-between;gap:16px;align-items:center;flex-wrap:wrap;margin-bottom:18px}
.eyebrow{color:#93c5fd;font-weight:700;letter-spacing:2px;font-size:12px}
.title{font-size:clamp(24px,3vw,38px);font-weight:800;margin:7px 0}
.sub{color:var(--muted);font-size:14px}
.legend{display:flex;gap:12px;flex-wrap:wrap;background:#0e1a2b;border:1px solid var(--line);border-radius:14px;padding:12px 15px}
.leg{display:flex;gap:7px;align-items:center;font-size:13px;font-weight:700}
.dot{width:11px;height:11px;border-radius:50%;display:inline-block}
.summary{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:18px 0}
.tile,.tablebox,.note{background:rgba(17,30,48,.94);border:1px solid var(--line);border-radius:16px}
.tile{padding:16px}.tile .label{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:1px}
.tile .num{font-size:clamp(25px,3vw,36px);font-weight:800;margin-top:9px}
.tile .hint{font-size:12px;color:var(--muted);margin-top:5px}
.tablebox{overflow:hidden}
.tablehead{display:flex;justify-content:space-between;align-items:center;padding:17px 19px;border-bottom:1px solid var(--line);gap:10px;flex-wrap:wrap}
.tablehead h2{font-size:17px;margin:0}
.live{font-size:12px;color:#86efac;display:flex;align-items:center;gap:7px}
.live .dot{box-shadow:0 0 12px #22c55e}
.tablewrap{overflow-x:auto}
table{width:100%;border-collapse:collapse;min-width:720px}
th{text-align:left;color:#9fb2ca;font-size:11px;letter-spacing:1px;text-transform:uppercase;background:#0d1929;padding:14px 18px}
td{padding:17px 18px;border-top:1px solid #203047;font-size:14px}
tbody tr:hover{background:#16263b}
.place{font-weight:700}.node{font-size:11px;color:var(--muted);margin-top:5px}
.reading{font-size:25px;font-weight:800;font-variant-numeric:tabular-nums}
.unit{font-size:11px;color:var(--muted);font-weight:500}
.pill{display:inline-flex;align-items:center;gap:8px;padding:7px 10px;border-radius:99px;font-weight:800;font-size:11px;letter-spacing:.5px}
.pill.green{background:#123a2b;color:#86efac}.pill.orange{background:#442f12;color:#fcd34d}
.pill.red{background:#451b23;color:#fca5a5}.pill.gray{background:#263449;color:#cbd5e1}
.bar{height:5px;background:#26364b;border-radius:9px;margin-top:9px;overflow:hidden;max-width:150px}
.fill{height:100%;border-radius:9px}
.footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;padding:15px 3px;color:var(--muted);font-size:12px}
.note{margin-top:12px;padding:14px 16px;color:#c8d6e8;font-size:12px;line-height:1.5}
.warn{color:#fcd34d;font-weight:700}
a{color:#93c5fd}
@media(max-width:760px){body{padding:12px}.summary{grid-template-columns:repeat(2,minmax(0,1fr))}.tile{padding:13px}.legend{gap:9px}.tablehead{padding:14px}}
</style>
</head>
<body>
<div class="wrap">
  <header class="head">
    <div>
      <div class="eyebrow">SCHOOL ENVIRONMENTAL INTELLIGENCE</div>
      <div class="title">Noise Monitoring Dashboard</div>
      <div class="sub">Four monitoring stations · Live updates every 2 seconds</div>
    </div>
    <div class="legend">
      <span class="leg"><i class="dot" style="background:var(--green)"></i> NORMAL</span>
      <span class="leg"><i class="dot" style="background:var(--orange)"></i> ELEVATED</span>
      <span class="leg"><i class="dot" style="background:var(--red)"></i> HIGH</span>
    </div>
  </header>

  <section class="summary">
    <div class="tile"><div class="label">Stations Online</div><div class="num" id="online">0 / 4</div><div class="hint">Latest heartbeat status</div></div>
    <div class="tile"><div class="label">Normal</div><div class="num" id="normal">0</div><div class="hint">Below 50 simulated dB-eq</div></div>
    <div class="tile"><div class="label">Elevated</div><div class="num" id="elevated">0</div><div class="hint">50 to below 60 simulated dB-eq</div></div>
    <div class="tile"><div class="label">High</div><div class="num" id="high">0</div><div class="hint">60 simulated dB-eq or higher</div></div>
  </section>

  <section class="tablebox">
    <div class="tablehead">
      <h2>Live readings by place</h2>
      <div class="live"><i class="dot"></i><span id="connection">Connecting to ESP32…</span></div>
    </div>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Place</th><th>Noise level</th><th>Signal peak</th><th>Station status</th><th>Last update</th></tr></thead>
        <tbody id="rows"><tr><td colspan="5">Waiting for station data…</td></tr></tbody>
      </table>
    </div>
  </section>

  <div class="note">
    <span class="warn">Measurement note:</span>
    “dB-eq (simulated)” is an educational indicator calculated from microphone AC-RMS ADC counts using an arbitrary reference. It is NOT calibrated dB(A), and it must not be used to claim regulatory compliance. Actual dB(A) requires calibration and validation against a reference sound-level meter. Green/orange/red categories are provisional display categories, not official noise-limit determinations.
  </div>

  <div class="footer">
    <span id="updated">Last dashboard refresh: —</span>
    <span><a href="/download">Download CSV log</a> · <a href="/api/status">View JSON data</a></span>
  </div>
</div>
<script>
const $ = id => document.getElementById(id);

function fmtAge(ms) {
  if (ms < 1000) return 'just now';
  return Math.round(ms / 1000) + 's ago';
}

function classify(dbEq) {
  if (dbEq < 50) return ['green', 'NORMAL'];
  if (dbEq < 60) return ['orange', 'ELEVATED'];
  return ['red', 'HIGH'];
}

async function refresh() {
  try {
    const res = await fetch('/api/status', {cache: 'no-store'});
    if (!res.ok) throw Error('HTTP ' + res.status);

    const data = await res.json();
    let online = 0, normal = 0, elevated = 0, high = 0;

    const html = data.stations.map(s => {
      if (!s.online) {
        return `<tr>
          <td><div class="place">${s.place}</div><div class="node">${s.node_id}</div></td>
          <td>—</td><td>—</td>
          <td><span class="pill gray">● OFFLINE</span></td>
          <td>no recent data</td>
        </tr>`;
      }

      online++;
      const dbEq = Number(s.db_eq);
      const [cls, label] = classify(dbEq);

      if (cls === 'green') normal++;
      else if (cls === 'orange') elevated++;
      else high++;

      const width = Math.max(0, Math.min(100, (dbEq / 90) * 100));

      return `<tr>
        <td><div class="place">${s.place}</div><div class="node">${s.node_id}</div></td>
        <td>
          <div class="reading">${dbEq.toFixed(1)} <span class="unit">dB-eq*</span></div>
          <div class="bar"><div class="fill" style="width:${width}%;background:var(--${cls})"></div></div>
          <div class="unit">Simulated indicator</div>
        </td>
        <td>${s.peak} <span class="unit">ADC counts</span></td>
        <td><span class="pill ${cls}">● ${label}</span></td>
        <td>${fmtAge(s.age_ms)}</td>
      </tr>`;
    }).join('');

    $('rows').innerHTML = html;
    $('online').textContent = online + ' / 4';
    $('normal').textContent = normal;
    $('elevated').textContent = elevated;
    $('high').textContent = high;
    $('connection').textContent = 'Connected to ESP32';
    $('updated').textContent = 'Last dashboard refresh: ' + new Date().toLocaleTimeString();
  } catch (e) {
    $('connection').textContent = 'ESP32 connection lost';
    $('rows').innerHTML = '<tr><td colspan="5">Cannot reach the ESP32. Check Wi-Fi and server power.</td></tr>';
  }
}

refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)HTML";

void handleDownload() {
  if (!LittleFS.exists("/noise_log_db_eq.csv")) {
    File createFile = LittleFS.open("/noise_log_db_eq.csv", FILE_WRITE);
    if (createFile) {
      createFile.println("uptime_s,node_id,place,rms_adc_counts,simulated_db_eq,peak_adc_counts");
      createFile.close();
    }
  }

  File f = LittleFS.open("/noise_log_db_eq.csv", FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Could not open log file");
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=noise_log_db_eq.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < 4; i++) {
    stations[i].id = NODE_IDS[i];
    stations[i].place = PLACE_NAMES[i];
    stations[i].rms = 0.0f;
    stations[i].peak = 0;
    stations[i].lastSeen = 0;
    stations[i].received = false;
  }

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed; CSV logging unavailable");
  }

  if (!LittleFS.exists("/noise_log_db_eq.csv")) {
    File f = LittleFS.open("/noise_log_db_eq.csv", FILE_WRITE);
    if (f) {
      f.println("uptime_s,node_id,place,rms_adc_counts,simulated_db_eq,peak_adc_counts");
      f.close();
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to school Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("ESP32 IP address: http://");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", DASHBOARD_PAGE);
  });
  server.on("/dashboard", HTTP_GET, []() {
    server.send_P(200, "text/html", DASHBOARD_PAGE);
  });
  server.on("/api/reading", HTTP_POST, handleReading);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/download", HTTP_GET, handleDownload);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Noise dashboard server started");
  Serial.println("Open the IP address above in a browser on the same Wi-Fi.");
}

void loop() {
  server.handleClient();
}
