
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =====================================================
// CENTRAL ELECTION DASHBOARD
// Educational polling simulator
// =====================================================

// -------- Wi-Fi configuration --------
const char* WIFI_SSID = "Mr Jasper_5GHz";
const char* WIFI_PASSWORD = "Danush21!";

// -------- EVM IP addresses --------
// Replace with actual IPs from the EVM Serial Monitors.
const char* DHARAPURAM_URL =
  "http://192.168.1.12/api/votes";

const char* MADURANTAKAM_URL =
  "http://192.168.1.13/api/votes";

// -------- Central server configuration --------
WebServer server(80);

const unsigned long POLL_INTERVAL_MS = 2000;
const unsigned long ONLINE_WINDOW_MS = 10000;
const unsigned long HTTP_TIMEOUT_MS = 700;

unsigned long lastPoll = 0;
unsigned long lastWiFiAttempt = 0;

// =====================================================
// DATA STRUCTURES
// =====================================================
struct EVMData {
  const char* name;
  const char* code;
  const char* url;

  int dmk = 0;
  int aidmk = 0;
  int tvk = 0;

  bool hasData = false;
  unsigned long lastSuccess = 0;
  unsigned long lastAttempt = 0;
  int lastHttpCode = 0;
};

EVMData dharap = {
  "Dharapuram",
  "Dharapuram-EVM",
  DHARAPURAM_URL
};

EVMData madura = {
  "Madurantakam",
  "Madurantakam-EVM",
  MADURANTAKAM_URL
};

// =====================================================
// SAFE NON-NEGATIVE VOTE COUNT PARSER
// Supports original votesA/votesB/votesC API fields.
// =====================================================
bool readVoteValue(JsonVariantConst value, int &result) {
  if (value.isNull()) return false;

  if (value.is<int>()) {
    int v = value.as<int>();
    if (v < 0) return false;
    result = v;
    return true;
  }

  if (value.is<long>()) {
    long v = value.as<long>();
    if (v < 0 || v > 2147483647L) return false;
    result = (int)v;
    return true;
  }

  return false;
}

// =====================================================
// FETCH ONE EVM
// Retains the last valid readings if a request fails.
// =====================================================
void pollEVM(EVMData &evm) {
  evm.lastAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    evm.lastHttpCode = 0;
    return;
  }

  WiFiClient client;
  HTTPClient http;

  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, evm.url)) {
    evm.lastHttpCode = 0;
    return;
  }

  int code = http.GET();
  evm.lastHttpCode = code;

  if (code == HTTP_CODE_OK) {
    String body = http.getString();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (!err) {
      // Reject a response from the wrong EVM.
      const char* machine = doc["machine"] | "";

      bool correctMachine =
        strcmp(machine, evm.code) == 0;

      int a, b, c;

      bool validCounts =
        readVoteValue(doc["votesA"], a) &&
        readVoteValue(doc["votesB"], b) &&
        readVoteValue(doc["votesC"], c);

      if (correctMachine && validCounts) {
        evm.dmk = a;
        evm.aidmk = b;
        evm.tvk = c;
        evm.hasData = true;
        evm.lastSuccess = millis();
      }
    }
  }

  http.end();
}

// =====================================================
// STATUS HELPERS
// =====================================================
bool isOnline(const EVMData &evm) {
  return evm.hasData &&
         (millis() - evm.lastSuccess <= ONLINE_WINDOW_MS);
}

unsigned long secondsSinceUpdate(const EVMData &evm) {
  if (!evm.hasData) return 0;
  return (millis() - evm.lastSuccess) / 1000;
}

// =====================================================
// JSON API: GET /api/status
// =====================================================
void handleStatus() {
  JsonDocument doc;

  doc["system"] = "Central Election Dashboard";
  doc["wifiConnected"] =
    (WiFi.status() == WL_CONNECTED);

  JsonObject d = doc["dharapuram"].to<JsonObject>();
  d["name"] = dharap.name;
  d["machine"] = dharap.code;
  d["online"] = isOnline(dharap);
  d["hasData"] = dharap.hasData;
  d["dmk"] = dharap.dmk;
  d["aidmk"] = dharap.aidmk;
  d["tvk"] = dharap.tvk;
  d["total"] = dharap.dmk + dharap.aidmk + dharap.tvk;
  d["secondsSinceUpdate"] = secondsSinceUpdate(dharap);

  JsonObject m = doc["madurantakam"].to<JsonObject>();
  m["name"] = madura.name;
  m["machine"] = madura.code;
  m["online"] = isOnline(madura);
  m["hasData"] = madura.hasData;
  m["dmk"] = madura.dmk;
  m["aidmk"] = madura.aidmk;
  m["tvk"] = madura.tvk;
  m["total"] = madura.dmk + madura.aidmk + madura.tvk;
  m["secondsSinceUpdate"] = secondsSinceUpdate(madura);

  // Totals represent the latest valid data from both EVMs.
  doc["combined"]["dmk"] = dharap.dmk + madura.dmk;
  doc["combined"]["aidmk"] = dharap.aidmk + madura.aidmk;
  doc["combined"]["tvk"] = dharap.tvk + madura.tvk;

  doc["combined"]["total"] =
    dharap.dmk + dharap.aidmk + dharap.tvk +
    madura.dmk + madura.aidmk + madura.tvk;

  String output;
  serializeJson(doc, output);

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", output);
}

// =====================================================
// DASHBOARD HTML
// Browser polls the local API every 2 seconds.
// =====================================================
const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>One Nation - One Election</title>
<style>
:root {
  --bg:#061322;
  --panel:#0b2034;
  --card:#102b44;
  --line:#174764;
  --text:#f5f8ff;
  --muted:#a8bfd3;
  --cyan:#26d9ff;
  --red:#ff3549;
  --green:#00cf7a;
  --purple:#a342ff;
  --yellow:#ffce38;
}
* { box-sizing:border-box; }
body {
  margin:0;
  background:radial-gradient(ellipse at top,#10304a,var(--bg) 65%);
  color:var(--text);
  font-family:Arial,Helvetica,sans-serif;
}
header {
  display:flex;
  justify-content:space-between;
  align-items:center;
  gap:15px;
  padding:18px 2.5%;
  border-bottom:1px solid var(--line);
}
h1 {
  font-size:clamp(22px,3vw,42px);
  margin:0;
  letter-spacing:1px;
}
.brand { color:var(--yellow); }
.subtitle {
  color:var(--muted);
  font-size:12px;
  margin-top:7px;
  letter-spacing:2px;
}
.system {
  text-align:right;
  color:var(--muted);
  font-size:12px;
}
.status { font-weight:bold; }
.online { color:#00f29a; }
.offline { color:#ff6475; }
main { padding:16px 2.5%; }
.panel {
  background:linear-gradient(145deg,#0c263c,#071827);
  border:1px solid var(--line);
  border-radius:16px;
  padding:16px;
  box-shadow:0 6px 28px #0003;
  min-width:0;
}
h2 { font-size:clamp(17px,1.7vw,25px); margin:0 0 13px; }
h3 { margin:0 0 6px; font-size:15px; }
.machines {
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:16px;
}
.machine-head {
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:8px;
  margin-bottom:15px;
}
.machine-code { color:var(--muted); font-size:11px; }
.party-grid {
  display:grid;
  grid-template-columns:repeat(3,minmax(0,1fr));
  gap:8px;
}
.party {
  border-radius:10px;
  text-align:center;
  padding:13px 5px;
  border:1px solid transparent;
  min-width:0;
}
.party .label { font-size:clamp(12px,1.2vw,17px); font-weight:bold; }
.party .count { font-size:clamp(27px,3.2vw,43px); font-weight:800; margin-top:9px; }
.dmk { background:#70121f; border-color:#ff3549; }
.aidmk { background:#064c31; border-color:#00cf7a; }
.tvk { background:#42116b; border-color:#a342ff; }
.total {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-top:10px;
  padding:12px;
  background:#102b44;
  border-radius:9px;
  font-weight:bold;
}
.total strong { font-size:26px; }
.update { color:var(--muted); font-size:11px; margin-top:10px; }
.results {
  display:grid;
  grid-template-columns:1.4fr 1fr;
  gap:16px;
  margin-top:16px;
}
.combined-heading {
  display:flex;
  justify-content:space-between;
  align-items:center;
  gap:10px;
  flex-wrap:wrap;
}
.grand-total { color:var(--yellow); font-size:18px; font-weight:bold; }
.result-row { margin-top:15px; }
.result-top {
  display:flex;
  justify-content:space-between;
  gap:10px;
  font-weight:bold;
}
.track {
  height:12px;
  background:#1d354a;
  border-radius:20px;
  overflow:hidden;
  margin-top:7px;
}
.fill { height:100%; width:0%; transition:width .4s ease; }
.fill.dmk { background:var(--red); }
.fill.aidmk { background:var(--green); }
.fill.tvk { background:var(--purple); }
.share { color:var(--muted); font-size:12px; }
.bottom {
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:16px;
  margin-top:16px;
}
.machine-status {
  display:flex;
  justify-content:space-between;
  align-items:center;
  gap:8px;
  padding:12px 0;
  border-bottom:1px solid #18354b;
  font-size:13px;
}
.machine-status:last-child { border-bottom:0; }
footer {
  text-align:center;
  padding:18px;
  color:var(--muted);
  font-size:12px;
}
footer strong { color:var(--cyan); }
@media(max-width:760px) {
  header { align-items:flex-start; flex-direction:column; }
  .system { text-align:left; }
  .machines,.results,.bottom { grid-template-columns:1fr; }
  main { padding:12px; }
  .panel { padding:13px; }
}
</style>
</head>
<body>
<header>
  <div>
    <h1> ONE NATION <span class="brand">- ONE ELECTION </span></h1>
    <div class="subtitle">LIVE RESULTS · REAL-TIME MONITORING · EDUCATIONAL DEMONSTRATION</div>
  </div>
  <div class="system">
    <div id="clock">--:--:--</div>
    <div id="network" class="status">Connecting...</div>
  </div>
</header>

<main>
  <section class="machines">
    <article class="panel">
      <div class="machine-head">
        <div>
          <h2>DHARAPURAM</h2>
          <div class="machine-code">Dharapuram-EVM</div>
        </div>
        <span id="dStatus" class="status">Waiting</span>
      </div>
      <div class="party-grid">
        <div class="party dmk"><div class="label">DMK</div><div class="count" id="dDmk">--</div></div>
        <div class="party aidmk"><div class="label">AIADMK</div><div class="count" id="dAidmk">--</div></div>
        <div class="party tvk"><div class="label">TVK</div><div class="count" id="dTvk">--</div></div>
      </div>
      <div class="total"><span>TOTAL VOTES</span><strong id="dTotal">--</strong></div>
      <div class="update" id="dUpdate">Waiting for first reading</div>
    </article>

    <article class="panel">
      <div class="machine-head">
        <div>
          <h2>MADURANTAKAM</h2>
          <div class="machine-code">Madurantakam-EVM</div>
        </div>
        <span id="mStatus" class="status">Waiting</span>
      </div>
      <div class="party-grid">
        <div class="party dmk"><div class="label">DMK</div><div class="count" id="mDmk">--</div></div>
        <div class="party aidmk"><div class="label">AIADMK</div><div class="count" id="mAidmk">--</div></div>
        <div class="party tvk"><div class="label">TVK</div><div class="count" id="mTvk">--</div></div>
      </div>
      <div class="total"><span>TOTAL VOTES</span><strong id="mTotal">--</strong></div>
      <div class="update" id="mUpdate">Waiting for first reading</div>
    </article>
  </section>

  <section class="results">
    <article class="panel">
      <div class="combined-heading">
        <h2>COMBINED RESULTS</h2>
        <div class="grand-total">TOTAL: <span id="cTotal">--</span></div>
      </div>
      <div class="result-row">
        <div class="result-top"><span>DMK</span><span><b id="cDmk">--</b> <span class="share" id="pDmk"></span></span></div>
        <div class="track"><div class="fill dmk" id="bDmk"></div></div>
      </div>
      <div class="result-row">
        <div class="result-top"><span>AIADMK</span><span><b id="cAidmk">--</b> <span class="share" id="pAidmk"></span></span></div>
        <div class="track"><div class="fill aidmk" id="bAidmk"></div></div>
      </div>
      <div class="result-row">
        <div class="result-top"><span>TVK</span><span><b id="cTvk">--</b> <span class="share" id="pTvk"></span></span></div>
        <div class="track"><div class="fill tvk" id="bTvk"></div></div>
      </div>
    </article>

    <article class="panel">
      <h2>SYSTEM STATUS</h2>
      <div class="machine-status"><span>Dharapuram</span><span id="sD" class="status">Waiting</span></div>
      <div class="machine-status"><span>Madurantakam</span><span id="sM" class="status">Waiting</span></div>
      <div class="machine-status"><span>Central Wi-Fi</span><span id="sWifi" class="status">Checking</span></div>
      <div class="update">Dashboard refreshes automatically every 2 seconds.</div>
    </article>
  </section>
</main>

<footer>
  DEMOCRACY · TECHNOLOGY · LEARNING FOR A BETTER TOMORROW<br><br>
  Developed by <strong>STREAM TEAM</strong> · Educational Simulator
</footer>

<script>
const $ = id => document.getElementById(id);
function text(id, value) { $(id).textContent = value; }

function status(id, online, hasData) {
  const el = $(id);
  el.className = 'status ' + (online ? 'online' : 'offline');
  el.textContent = online ? '● ONLINE' : (hasData ? '● STALE / OFFLINE' : '● WAITING');
}

function machine(prefix, data, statusId, updateId) {
  const p = prefix;
  if (data.hasData) {
    text(p+'Dmk', data.dmk);
    text(p+'Aidmk', data.aidmk);
    text(p+'Tvk', data.tvk);
    text(p+'Total', data.total);
    text(updateId, data.online
      ? 'Last successful update: '+data.secondsSinceUpdate+'s ago'
      : 'Last valid reading: '+data.secondsSinceUpdate+'s ago — data may be stale');
  } else {
    text(updateId, 'No valid data received yet');
  }
  status(statusId, data.online, data.hasData);
}

function combined(c) {
  text('cDmk', c.dmk);
  text('cAidmk', c.aidmk);
  text('cTvk', c.tvk);
  text('cTotal', c.total);

  const parties = [
    ['Dmk', c.dmk],
    ['Aidmk', c.aidmk],
    ['Tvk', c.tvk]
  ];

  parties.forEach(([key, value]) => {
    const pct = c.total > 0 ? value * 100 / c.total : 0;
    text('p'+key, pct.toFixed(1)+'%');
    $('b'+key).style.width = pct+'%';
  });
}

async function refresh() {
  try {
    const response = await fetch('/api/status', {cache:'no-store'});
    if (!response.ok) throw new Error('HTTP '+response.status);
    const data = await response.json();

    machine('d', data.dharapuram, 'dStatus', 'dUpdate');
    machine('m', data.madurantakam, 'mStatus', 'mUpdate');
    status('sD', data.dharapuram.online, data.dharapuram.hasData);
    status('sM', data.madurantakam.online, data.madurantakam.hasData);
    status('sWifi', data.wifiConnected, data.wifiConnected);
    combined(data.combined);
    text('network', data.wifiConnected
      ? '● CENTRAL WI-FI CONNECTED'
      : '● CENTRAL WI-FI DISCONNECTED');
  } catch (e) {
    text('network', '● DASHBOARD API ERROR');
  }
}

function clock() {
  text('clock', new Date().toLocaleString());
}
clock();
refresh();
setInterval(clock, 1000);
setInterval(refresh, 2000);
</script>
</body>
</html>
)HTML";

// =====================================================
// ROOT PAGE
// =====================================================
void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

// =====================================================
// WIFI RECONNECTION
// =====================================================
void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiAttempt >= 10000) {
    lastWiFiAttempt = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Retrying Wi-Fi connection...");
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println(" ONE NATION - ONE ELECTION");
  Serial.println(" CENTRAL DASHBOARD SERVER");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wait up to 20 seconds for Wi-Fi.
  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi CONNECTED!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Open dashboard: http://");
    Serial.println(WiFi.localIP());

    lastWiFiAttempt = millis();
  } else {
    Serial.println("Wi-Fi NOT CONNECTED.");
    Serial.println("Will keep retrying in the background.");
  }

  // Register dashboard routes.
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();

  Serial.println("Dashboard server started.");
  Serial.println("================================");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();
  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED &&
      millis() - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = millis();

    pollEVM(dharap);
    pollEVM(madura);
  }
}
