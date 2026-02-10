// ============================================================
// Lovebox Firmware - ESP32 + Telegram Bot
// ============================================================
// Modernized from the original ESP8266 + GitHub Gist version
// by julisa99. See README.md for hardware and setup instructions.
//
// Changes from original:
//   - ESP32 instead of ESP8266 (better WiFi, hardware crypto, BLE)
//   - Telegram Bot API instead of GitHub Gist (instant messaging, images, easy UX)
//   - WiFiManager instead of hardcoded credentials (captive portal on first boot)
//   - NVS (Preferences) instead of EEPROM (wear-leveled flash storage)
//   - Proper TLS with CA certificate instead of fingerprint pinning

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>       // NVS key-value storage (replaces EEPROM)
#include <ESP32Servo.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "SSD1306Wire.h"

#include "config.h"

// --- Telegram API root CA certificate (DigiCert Global Root G2) ---
// This is the root CA that signs api.telegram.org's TLS certificate.
// Root CAs last ~20 years (expires 2038), unlike leaf certs which rotate
// every few months. This is why fingerprint pinning broke the original.
static const char telegram_root_ca[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wzt
CO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQv
IOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQMDAPgNV
HRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV5uNu
5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj+zHhTRUcYIrB
0SMng0eJ0EQu9PQjkqHaBqFG0OH37bC64An+H/YCDRGo6PdRgNqhg7iNqFEe6pJF
xMcPQJkNFNb3k3it4FTzpEGtCAP0YFaKSWZMAKdqoGsXPPe8pPP0GOtpp1b5oTS3
3FWLQWyaFLOBhIYLA9UDhEJjJBMPEDkqEPaVdUae+aCaqFME7BbCM0FHPVPDQ7kw
ak4CRx0kZFJhJQ/OgjFDGHGF4m+7MjM/BfpC+JMPQINbbydhAAAA
-----END CERTIFICATE-----
)EOF";

// --- Global objects ---
SSD1306Wire oled(OLED_ADDR, PIN_SDA, PIN_SCL);
Servo heartServo;
Preferences prefs;           // NVS storage (wear-leveled, unlike EEPROM)
WiFiManager wifiManager;

// --- State ---
int servoPos = 90;
int servoDir = -1;
long lastUpdateId = 0;       // Telegram update ID - tracks which messages we've processed
bool hasUnreadMessage = false;
String currentMessage = "";
String currentMessageType = ""; // "text" or "photo"

// ============================================================
// Display
// ============================================================

void displayText(const String& text) {
  oled.clear();
  oled.drawStringMaxWidth(0, 0, SCREEN_WIDTH, text);
  oled.display();
}

void displayBitmap(const String& bits) {
  // Same pixel format as the original: a string of '0' and '1' chars,
  // 128 pixels wide, row by row. '1' = white pixel.
  oled.clear();
  for (unsigned int i = 0; i < bits.length(); i++) {
    if (bits[i] == '1') {
      int x = i % (SCREEN_WIDTH + 1);  // +1 accounts for newline chars in original format
      int y = i / (SCREEN_WIDTH + 1);
      if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
        oled.setPixel(x, y);
      }
    }
  }
  oled.display();
}

void displayStatus(const String& line1, const String& line2 = "") {
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.drawString(SCREEN_WIDTH / 2, 20, line1);
  if (line2.length() > 0) {
    oled.drawString(SCREEN_WIDTH / 2, 36, line2);
  }
  oled.display();
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

// ============================================================
// Servo (heart spinner)
// ============================================================

void spinServo() {
  // Oscillate the servo between SERVO_MIN_ANGLE and SERVO_MAX_ANGLE.
  // This wiggles the heart on the front of the box to signal a new message.
  heartServo.write(servoPos);
  delay(SERVO_STEP_DELAY_MS);

  if (servoPos <= SERVO_MIN_ANGLE || servoPos >= SERVO_MAX_ANGLE) {
    servoDir *= -1;
  }
  servoPos += servoDir;
}

void parkServo() {
  // Return to center and detach so the servo doesn't buzz/draw power
  heartServo.write(90);
  delay(200);
  heartServo.detach();
}

// ============================================================
// Telegram Bot API
// ============================================================
// The Telegram Bot API works like this:
//   1. You call getUpdates with an offset (lastUpdateId + 1)
//   2. Telegram returns any new messages sent to your bot since that offset
//   3. You process them and bump the offset so you don't see them again
//
// This is "long polling" -- simple, no webhooks, no server needed.
// The ESP32 just asks "any new messages?" every POLL_INTERVAL_MS.

bool checkTelegram() {
  WiFiClientSecure client;
  client.setCACert(telegram_root_ca);

  HTTPClient https;
  String apiUrl = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN)
                + "/getUpdates?offset=" + String(lastUpdateId + 1)
                + "&limit=1&timeout=5";

  if (!https.begin(client, apiUrl)) {
    Serial.println("[Telegram] Connection failed");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Telegram] HTTP error: %d\n", httpCode);
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  // Parse the JSON response
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Telegram] JSON parse error: %s\n", err.c_str());
    return false;
  }

  if (!doc["ok"].as<bool>()) return false;

  JsonArray results = doc["result"].as<JsonArray>();
  if (results.size() == 0) return false;  // No new messages

  // Process the first (newest) message
  JsonObject update = results[0];
  lastUpdateId = update["update_id"].as<long>();

  // Save the update ID to NVS so we don't re-show old messages after reboot
  prefs.putLong("lastUpdateId", lastUpdateId);

  JsonObject message = update["message"];
  if (message.isNull()) return false;

  // Check what kind of message it is
  if (message.containsKey("text")) {
    currentMessage = message["text"].as<String>();
    currentMessageType = "text";
    Serial.printf("[Telegram] Text message: %s\n", currentMessage.c_str());
  }
  else if (message.containsKey("photo")) {
    // Telegram sends multiple sizes; grab the largest (last in array)
    JsonArray photos = message["photo"].as<JsonArray>();
    String fileId = photos[photos.size() - 1]["file_id"].as<String>();

    // We need a second API call to get the actual image file path
    // For now, show a caption or placeholder
    if (message.containsKey("caption")) {
      currentMessage = message["caption"].as<String>();
    } else {
      currentMessage = "[Photo received]";
    }
    currentMessageType = "text"; // Display caption as text for now
    Serial.printf("[Telegram] Photo with caption: %s\n", currentMessage.c_str());
  }
  else {
    // Sticker, video, etc. -- show a generic notice
    currentMessage = "New message!";
    currentMessageType = "text";
  }

  return true;
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n[Lovebox] Starting...");

  // --- Display init ---
  oled.init();
  oled.flipScreenVertically();
  oled.setColor(WHITE);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.setFont(ArialMT_Plain_10);
  displayStatus("<3 LOVEBOX <3", "Connecting...");

  // --- WiFiManager ---
  // On first boot (or if saved network is unavailable), the ESP32 creates
  // a WiFi access point called "Lovebox-Setup". Connect to it with your
  // phone, and a captive portal opens where you pick your home WiFi.
  // After that, credentials are saved and it auto-connects on future boots.
  wifiManager.setConfigPortalTimeout(180); // 3 min timeout, then retry
  if (!wifiManager.autoConnect(AP_NAME)) {
    Serial.println("[WiFi] Failed to connect - restarting");
    displayStatus("WiFi failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }
  Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
  displayStatus("WiFi connected!", WiFi.localIP().toString());
  delay(1000);

  // --- Servo ---
  heartServo.attach(PIN_SERVO);
  heartServo.write(90);

  // --- NVS (replaces EEPROM) ---
  // NVS is a key-value store built into ESP32's flash with automatic
  // wear leveling. Unlike EEPROM.write(142, val) which always hits the
  // same flash sector, NVS spreads writes across multiple sectors.
  prefs.begin("lovebox", false);
  lastUpdateId = prefs.getLong("lastUpdateId", 0);
  hasUnreadMessage = prefs.getBool("unread", false);

  if (hasUnreadMessage) {
    currentMessage = prefs.getString("message", "");
    currentMessageType = prefs.getString("msgType", "text");
    if (currentMessage.length() > 0) {
      if (currentMessageType == "text") {
        displayText(currentMessage);
      } else {
        displayBitmap(currentMessage);
      }
    }
  } else {
    displayStatus("<3 LOVEBOX <3");
  }

  Serial.println("[Lovebox] Ready!");
}

// ============================================================
// Main Loop
// ============================================================
// The loop has two modes:
//   1. UNREAD message: spin the heart, watch the light sensor.
//      When the box is opened (light > threshold), mark as read.
//   2. READ (idle): poll Telegram for new messages every POLL_INTERVAL_MS.

void loop() {
  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.reconnect();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnect failed - restarting");
      ESP.restart();
    }
  }

  if (hasUnreadMessage) {
    // --- Unread mode: spin heart, wait for box to be opened ---
    if (!heartServo.attached()) {
      heartServo.attach(PIN_SERVO);
    }
    spinServo();

    int light = analogRead(PIN_LIGHT);
    if (light > LIGHT_THRESHOLD) {
      // Box was opened! Mark message as read.
      hasUnreadMessage = false;
      prefs.putBool("unread", false);
      parkServo();
      Serial.println("[Lovebox] Message read (box opened)");
    }
  }
  else {
    // --- Idle mode: check Telegram for new messages ---
    bool gotMessage = checkTelegram();

    if (gotMessage) {
      hasUnreadMessage = true;
      prefs.putBool("unread", true);
      prefs.putString("message", currentMessage);
      prefs.putString("msgType", currentMessageType);

      if (currentMessageType == "text") {
        displayText(currentMessage);
      } else {
        displayBitmap(currentMessage);
      }

      heartServo.attach(PIN_SERVO);
      Serial.println("[Lovebox] New message! Spinning heart.");
    }

    delay(POLL_INTERVAL_MS);
  }
}
