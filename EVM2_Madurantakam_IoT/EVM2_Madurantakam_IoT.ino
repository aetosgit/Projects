
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// =====================================================
// MACHINE CONFIGURATION
// Set these four values separately for each EVM.
// =====================================================
const char* MACHINE_NAME = "Madurantakam";
const char* MACHINE_CODE = "Madurantakam-EVM";
const char* PREF_NAMESPACE = "MA";
const char* DASHBOARD_TITLE = "Madurantakam EVM";

// Wi-Fi: enter your network credentials locally.
// Do not share sketches containing real credentials.
const char* ssid = "SSID";
const char* wifiPassword = "PASSWORD";

// =====================================================
// GPIO CONFIGURATION
// =====================================================
const int buttonA = 16;  // DMK
const int buttonB = 17;  // AIADMK
const int buttonC = 18;  // TVK
const int resetButton = 4;

const int redLED = 2;
const int buzzer = 22;

// =====================================================
// SYSTEM OBJECTS AND VOTE COUNTS
// =====================================================
Preferences preferences;
WebServer server(80);

int votesA = 0;
int votesB = 0;
int votesC = 0;

unsigned long nextVoteAllowed = 0;
unsigned long feedbackUntil = 0;
unsigned long lastWiFiAttempt = 0;

bool feedbackActive = false;

// Button debouncing
struct ButtonState {
  int lastReading = HIGH;
  int stableState = HIGH;
  unsigned long lastChange = 0;
};

ButtonState btnA, btnB, btnC, btnReset;

// Reset sequence:
// Hold 5 seconds to arm.
// Release, then hold 2 seconds to confirm.
unsigned long resetPressStart = 0;
unsigned long resetArmedUntil = 0;
bool resetHandled = false;

// =====================================================
// BUTTON DEBOUNCE
// Returns true once for each debounced press.
// =====================================================
bool pressed(int pin, ButtonState &b) {
  int reading = digitalRead(pin);

  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.lastChange = millis();
  }

  if (millis() - b.lastChange >= 35) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        return true;
      }
    }
  }

  return false;
}

// =====================================================
// VOTE STORAGE
// =====================================================
void saveVotes() {
  preferences.putInt("votesA", votesA);
  preferences.putInt("votesB", votesB);
  preferences.putInt("votesC", votesC);
}

void printTotals() {
  Serial.println("\n===== EVM VOTE TOTALS =====");
  Serial.printf("Machine: %s\n", MACHINE_CODE);
  Serial.printf("DMK: %d\n", votesA);
  Serial.printf("AIADMK: %d\n", votesB);
  Serial.printf("TVK: %d\n", votesC);
  Serial.printf("TOTAL: %d\n", votesA + votesB + votesC);
  Serial.println("===========================\n");
}

// =====================================================
// VOTE HANDLER
// =====================================================
void castVote(int party) {
  if (party == 0) {
    votesA++;
    Serial.println("Vote recorded: DMK");
  } else if (party == 1) {
    votesB++;
    Serial.println("Vote recorded: AIADMK");
  } else {
    votesC++;
    Serial.println("Vote recorded: TVK");
  }

  saveVotes();
  printTotals();

  // Non-blocking LED/buzzer feedback
  digitalWrite(redLED, HIGH);
  digitalWrite(buzzer, HIGH);
  feedbackActive = true;
  feedbackUntil = millis() + 180;

  // Brief lockout after each vote
  nextVoteAllowed = millis() + 2000;
}

// =====================================================
// RESET HANDLER
// Hold reset 5 seconds, release, then hold 2 seconds.
// =====================================================
void handleResetButton() {
  bool isLow = digitalRead(resetButton) == LOW;

  if (!isLow) {
    resetPressStart = 0;
    resetHandled = false;
    return;
  }

  if (resetPressStart == 0) {
    resetPressStart = millis();
  }

  unsigned long held = millis() - resetPressStart;

  if (resetArmedUntil != 0 &&
      millis() > resetArmedUntil) {
    resetArmedUntil = 0;
    Serial.println("Reset confirmation expired.");
  }

  if (resetHandled) return;

  // First hold: arm reset
  if (resetArmedUntil == 0 && held >= 5000) {
    resetArmedUntil = millis() + 10000;
    resetHandled = true;

    Serial.println("\nRESET ARMED.");
    Serial.println(
      "Release the button, then hold it again for 2 seconds."
    );
  }

  // Second hold: confirm reset
  else if (resetArmedUntil != 0 && held >= 2000) {
    votesA = 0;
    votesB = 0;
    votesC = 0;

    saveVotes();
    resetArmedUntil = 0;
    resetHandled = true;

    Serial.println("\nALL VOTE COUNTS RESET.");
    printTotals();
  }
}

// =====================================================
// JSON API: GET /api/votes
// =====================================================
void handleVotes() {
  DynamicJsonDocument doc(384);

  doc["machine"] = MACHINE_CODE;
  doc["name"] = MACHINE_NAME;

  // Retain the field names from the original sketch
  doc["votesA"] = votesA;
  doc["votesB"] = votesB;
  doc["votesC"] = votesC;

  doc["DMK"] = votesA;
  doc["AIADMK"] = votesB;
  doc["TVK"] = votesC;
  doc["total"] = votesA + votesB + votesC;

  String response;
  serializeJson(doc, response);

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", response);
}

// =====================================================
// LOCAL DASHBOARD: GET /
// =====================================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="2">
<title>)rawliteral";

  html += DASHBOARD_TITLE;

  html += R"rawliteral(</title>
<style>
body {
  font-family: Arial, sans-serif;
  background: #101827;
  color: white;
  text-align: center;
  margin: 0;
  padding: 20px;
}
.card {
  background: #24364c;
  margin: 12px auto;
  padding: 18px;
  border-radius: 12px;
  max-width: 500px;
  font-size: 24px;
}
.small { color: #b9c7d9; font-size: 14px; }
</style>
</head>
<body>
<h1>ELECTION DASHBOARD</h1>
<h2>)rawliteral";

  html += MACHINE_NAME;

  html += R"rawliteral(</h2>
<p class="small">Machine: )rawliteral";

  html += MACHINE_CODE;

  html += R"rawliteral(</p>
<div class="card">DMK: )rawliteral";

  html += String(votesA);

  html += R"rawliteral(</div>
<div class="card">AIADMK: )rawliteral";

  html += String(votesB);

  html += R"rawliteral(</div>
<div class="card">TVK: )rawliteral";

  html += String(votesC);

  html += R"rawliteral(</div>
<div class="card">TOTAL VOTES: )rawliteral";

  html += String(votesA + votesB + votesC);

  html += R"rawliteral(</div>
<p class="small">Refreshes every 2 seconds</p>
</body>
</html>)rawliteral";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", html);
}

// =====================================================
// WIFI
// =====================================================
void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiAttempt >= 10000) {
    lastWiFiAttempt = millis();
    Serial.println("Attempting Wi-Fi reconnection...");
    WiFi.disconnect();
    WiFi.begin(ssid, wifiPassword);
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(buttonA, INPUT_PULLUP);
  pinMode(buttonB, INPUT_PULLUP);
  pinMode(buttonC, INPUT_PULLUP);
  pinMode(resetButton, INPUT_PULLUP);

  pinMode(redLED, OUTPUT);
  pinMode(buzzer, OUTPUT);

  digitalWrite(redLED, LOW);
  digitalWrite(buzzer, LOW);

  // Each EVM has its own persistent storage namespace.
  preferences.begin(PREF_NAMESPACE, false);

  votesA = preferences.getInt("votesA", 0);
  votesB = preferences.getInt("votesB", 0);
  votesC = preferences.getInt("votesC", 0);

  Serial.println("\nDSPL EVM CONTROLLER");
  Serial.printf("Machine: %s\n", MACHINE_NAME);
  Serial.printf("Code: %s\n", MACHINE_CODE);
  printTotals();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifiPassword);
  lastWiFiAttempt = millis();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/votes", HTTP_GET, handleVotes);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Web server started.");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();
  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    // Print the IP address once connected
    static bool ipPrinted = false;

    if (!ipPrinted) {
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      ipPrinted = true;
    }
  }

  // Turn feedback outputs off without blocking the server
  if (feedbackActive && millis() >= feedbackUntil) {
    digitalWrite(redLED, LOW);
    digitalWrite(buzzer, LOW);
    feedbackActive = false;
  }

  handleResetButton();

  // While reset is armed, suppress voting.
  if (resetArmedUntil != 0 &&
      millis() <= resetArmedUntil) {
    return;
  }

  if (millis() < nextVoteAllowed) return;

  // One accepted vote per button press
  if (pressed(buttonA, btnA)) {
    castVote(0);
  } else if (pressed(buttonB, btnB)) {
    castVote(1);
  } else if (pressed(buttonC, btnC)) {
    castVote(2);
  }
}
