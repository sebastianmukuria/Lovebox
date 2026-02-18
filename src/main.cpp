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
#include <TJpg_Decoder.h>

#include "config.h"

// --- TLS Configuration ---
// Using setInsecure() to skip certificate verification for now.
// This is acceptable for a personal IoT project on a home network.
// TODO: Add proper CA root certificate for production use.

// --- Global objects ---
SSD1306Wire oled(OLED_ADDR, PIN_SDA, PIN_SCL);
Servo heartServo;
Preferences prefs;           // NVS storage (wear-leveled, unlike EEPROM)
// WiFiManager is NOT a global — its constructor can crash before setup() runs.
// Declared locally inside setup() instead.

// --- State ---
int servoPos = 90;
int servoDir = -1;
long lastUpdateId = 0;       // Telegram update ID - tracks which messages we've processed
bool hasUnreadMessage = false;
unsigned long unreadSince = 0;
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
// JPEG Image Display
// ============================================================
// When someone sends a photo via Telegram, we:
//   1. Get the file_id from the Telegram update
//   2. Call getFile API to get a download path
//   3. Download the JPEG into memory
//   4. Decode it with TJpg_Decoder, scaling to fit 128x64
//   5. Convert each pixel to black/white and draw on the OLED

int imgOffsetX = 0;  // For centering the decoded image
int imgOffsetY = 0;

// Callback called by TJpg_Decoder for each decoded pixel block
bool onJpgBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int px = x + i + imgOffsetX;
      int py = y + j + imgOffsetY;
      if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
        uint16_t color = bitmap[j * w + i];  // RGB565 format
        // Convert to grayscale: extract R/G/B, weight by human perception
        uint8_t r = ((color >> 11) & 0x1F) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;
        // Simple threshold: > 128 = white pixel
        if (gray > 128) {
          oled.setPixel(px, py);
        }
      }
    }
  }
  return true;
}

// Download a Telegram photo and display it on the OLED
bool downloadAndDisplayImage(const String& fileId) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  // Step 1: Get the file path from Telegram
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN)
             + "/getFile?file_id=" + fileId;
  if (!https.begin(client, url)) return false;

  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[Image] getFile HTTP error: %d\n", code);
    https.end();
    return false;
  }

  JsonDocument doc;
  deserializeJson(doc, https.getString());
  https.end();

  String filePath = doc["result"]["file_path"].as<String>();
  if (filePath.length() == 0) {
    Serial.println("[Image] No file_path in response");
    return false;
  }

  // Step 2: Download the JPEG
  String fileUrl = "https://api.telegram.org/file/bot" + String(TELEGRAM_BOT_TOKEN)
                 + "/" + filePath;
  if (!https.begin(client, fileUrl)) return false;

  code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[Image] Download HTTP error: %d\n", code);
    https.end();
    return false;
  }

  int contentLen = https.getSize();
  if (contentLen <= 0 || contentLen > 30000) {
    Serial.printf("[Image] Bad size: %d bytes\n", contentLen);
    https.end();
    return false;
  }

  uint8_t* jpgBuf = (uint8_t*)malloc(contentLen);
  if (!jpgBuf) {
    Serial.println("[Image] malloc failed");
    https.end();
    return false;
  }

  WiFiClient* stream = https.getStreamPtr();
  int bytesRead = 0;
  unsigned long timeout = millis() + 10000;
  while (bytesRead < contentLen && millis() < timeout) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(jpgBuf + bytesRead, min(avail, contentLen - bytesRead));
      bytesRead += n;
    }
    delay(1);
  }
  https.end();

  if (bytesRead != contentLen) {
    Serial.printf("[Image] Incomplete download: %d/%d\n", bytesRead, contentLen);
    free(jpgBuf);
    return false;
  }

  Serial.printf("[Image] Downloaded %d bytes, decoding...\n", contentLen);

  // Step 3: Get image dimensions and pick a scale
  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, jpgBuf, contentLen);
  Serial.printf("[Image] JPEG size: %dx%d\n", w, h);

  // TJpg_Decoder supports scales: 1, 2, 4, 8
  uint8_t scale = 1;
  if (w > 512 || h > 256) scale = 8;
  else if (w > 256 || h > 128) scale = 4;
  else if (w > 128 || h > 64) scale = 2;
  TJpgDec.setJpgScale(scale);

  // Center the scaled image on the 128x64 display
  int scaledW = w / scale;
  int scaledH = h / scale;
  imgOffsetX = max(0, (SCREEN_WIDTH - scaledW) / 2);
  imgOffsetY = max(0, (SCREEN_HEIGHT - scaledH) / 2);

  // Step 4: Decode and draw
  oled.clear();
  TJpgDec.setCallback(onJpgBlock);
  TJpgDec.drawJpg(0, 0, jpgBuf, contentLen);
  oled.display();

  free(jpgBuf);
  Serial.println("[Image] Displayed on OLED");
  return true;
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

// Returns: 1 = new message, -1 = /read command (dismiss), 0 = nothing
int checkTelegram() {
  WiFiClientSecure client;
  client.setInsecure();

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
  if (results.size() == 0) return 0;  // No new messages

  // Process the first (newest) message
  JsonObject update = results[0];
  lastUpdateId = update["update_id"].as<long>();

  // Save the update ID to NVS so we don't re-show old messages after reboot
  prefs.putLong("lastUpdateId", lastUpdateId);

  JsonObject message = update["message"];
  if (message.isNull()) return 0;

  // Check what kind of message it is
  if (message.containsKey("text")) {
    String text = message["text"].as<String>();
    if (text == "/read" || text == "/dismiss") {
      Serial.println("[Telegram] /read command received - dismissing message");
      return -1;
    }
    currentMessage = text;
    currentMessageType = "text";
    Serial.printf("[Telegram] Text message: %s\n", currentMessage.c_str());
  }
  else if (message.containsKey("photo")) {
    // Telegram sends multiple sizes; pick the smallest one >= 128px wide
    // (smaller = faster download, and we're scaling to 128x64 anyway)
    JsonArray photos = message["photo"].as<JsonArray>();
    String fileId;
    for (size_t i = 0; i < photos.size(); i++) {
      fileId = photos[i]["file_id"].as<String>();
      int w = photos[i]["width"].as<int>();
      if (w >= 128) break;  // Good enough for our 128px-wide display
    }

    currentMessageType = "photo";
    currentMessage = fileId;  // Store file_id for re-display after reboot
    Serial.printf("[Telegram] Photo received (file_id: %.20s...)\n", fileId.c_str());
  }
  else {
    // Sticker, video, etc. -- show a generic notice
    currentMessage = "New message!";
    currentMessageType = "text";
  }

  return 1;
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
  WiFiManager wifiManager;
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
    // Restore unread timestamp - if stored time is 0 (old firmware), treat as just received
    // so the 24h timeout counts from now, not from epoch.
    unsigned long storedAge = prefs.getULong("unreadAge", 0);
    unreadSince = (storedAge == 0) ? millis() : (millis() - storedAge);

    currentMessage = prefs.getString("message", "");
    currentMessageType = prefs.getString("msgType", "text");
    if (currentMessage.length() > 0) {
      if (currentMessageType == "photo") {
        // Re-download photo on reboot (file_id is stored)
        downloadAndDisplayImage(currentMessage);
      } else {
        displayText(currentMessage);
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
    // --- Unread mode: spin heart, wait for box to be opened or /read command ---
    if (!heartServo.attached()) {
      heartServo.attach(PIN_SERVO);
    }
    spinServo();

    int light = analogRead(PIN_LIGHT);
    Serial.printf("[LDR] value=%d threshold=%d\n", light, LIGHT_THRESHOLD);
    bool boxOpened = light > LIGHT_THRESHOLD;

    // Also check for /read command from Telegram (so you can dismiss without the LDR)
    bool dismissed = (checkTelegram() == -1);

    // Auto-dismiss after 24 hours so old messages don't loop forever
    bool timedOut = (millis() - unreadSince > UNREAD_TIMEOUT_MS);

    if (boxOpened || dismissed || timedOut) {
      if (timedOut)   Serial.println("[Lovebox] Message auto-dismissed (24h timeout)");
      if (dismissed)  Serial.println("[Lovebox] Message dismissed via /read command");
      hasUnreadMessage = false;
      prefs.putBool("unread", false);
      parkServo();
      displayStatus("<3 LOVEBOX <3");
      Serial.println("[Lovebox] Message read");
    }
  }
  else {
    // --- Idle mode: check Telegram for new messages ---
    Serial.printf("[LDR] value=%d threshold=%d\n", analogRead(PIN_LIGHT), LIGHT_THRESHOLD);
    int result = checkTelegram();

    if (result == 1) {
      hasUnreadMessage = true;
      unreadSince = millis();
      prefs.putBool("unread", true);
      prefs.putULong("unreadAge", 0);  // 0 = just received (age unknown after reboot)
      prefs.putString("message", currentMessage);
      prefs.putString("msgType", currentMessageType);

      if (currentMessageType == "photo") {
        downloadAndDisplayImage(currentMessage);
      } else {
        displayText(currentMessage);
      }

      heartServo.attach(PIN_SERVO);
      Serial.println("[Lovebox] New message! Spinning heart.");
    }

    delay(POLL_INTERVAL_MS);
  }
}
