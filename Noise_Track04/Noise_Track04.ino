
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <math.h>

// ===== SCHOOL WI-FI =====
const char* WIFI_SSID = "TII-School-Guest";
const char* WIFI_PASSWORD = "Guest@2026";

// Central ESP32 IP
const char* ESP32_SERVER_IP = "192.168.19.38";

// ===== STATION IDENTITY =====
const char* NODE_ID = "noise04";
const char* PLACE_NAME = "Admin / Reception";

const int MIC_PIN = A0;
const unsigned long SEND_INTERVAL_MS = 2000;
const int SAMPLE_COUNT = 800;

unsigned long lastSend = 0;

struct NoiseSample {
  float rms;
  int peak;
  int minimum;
  int maximum;
  float mean;
};

NoiseSample sampleMicrophone() {
  double sum = 0.0;
  double sumSquares = 0.0;
  int minimum = 1023;
  int maximum = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    int value = analogRead(MIC_PIN);

    sum += value;
    sumSquares += (double)value * value;

    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;

    delayMicroseconds(250);
  }

  double mean = sum / SAMPLE_COUNT;
  double variance =
      (sumSquares / SAMPLE_COUNT) - (mean * mean);

  if (variance < 0.0) variance = 0.0;

  NoiseSample result;
  result.mean = mean;
  result.minimum = minimum;
  result.maximum = maximum;
  result.rms = sqrt(variance);

  int highPeak = abs(maximum - (int)round(mean));
  int lowPeak = abs(minimum - (int)round(mean));
  result.peak = max(highPeak, lowPeak);

  return result;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("Wi-Fi disconnected; reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long started = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  }

  Serial.print("Wi-Fi connection failed. Status: ");
  Serial.println(WiFi.status());
  return false;
}

void sendReading(const NoiseSample& s) {
  if (!connectWiFi()) return;

  WiFiClient client;
  HTTPClient http;

  String url = String("http://") +
               ESP32_SERVER_IP + "/api/reading";

  http.setReuse(false);
  http.setTimeout(5000);

  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return;
  }

  http.addHeader(
      "Content-Type",
      "application/x-www-form-urlencoded"
  );

  String body =
      String("node_id=") + NODE_ID +
      "&place=Classroom%201" +
      "&rms=" + String(s.rms, 2) +
      "&peak=" + String(s.peak);

  int code = http.POST(body);

  Serial.printf(
      "%s | %s | RMS %.2f ADC counts | peak %d"
      " | HTTP %d",
      NODE_ID, PLACE_NAME, s.rms, s.peak, code
  );

  if (code < 0) {
    Serial.print(" | Error: ");
    Serial.print(HTTPClient::errorToString(code));
  } else {
    Serial.print(" | Wi-Fi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.print(" dBm");
  }

  Serial.println();
  http.end();
  client.stop();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Classroom 1 Noise Station");
  Serial.println("Measurement: relative ADC RMS");
  Serial.println("Not a calibrated dB(A) meter");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  connectWiFi();
}

void loop() {
  if (millis() - lastSend >= SEND_INTERVAL_MS ||
      lastSend == 0) {
    lastSend = millis();

    NoiseSample reading = sampleMicrophone();

    Serial.printf(
        "Signal | min %d | max %d | mean %.2f"
        " | RMS %.2f | peak %d\n",
        reading.minimum,
        reading.maximum,
        reading.mean,
        reading.rms,
        reading.peak
    );

    sendReading(reading);
  }

  delay(5);
}
