// ============================================================
// Component Test Sketch - Breadboard Practice Run
// ============================================================
// Tests each component individually so you can verify wiring
// before running the full Lovebox firmware.
//
// Build & upload:  pio run -e test_components --target upload
// Serial monitor:  pio device monitor
//
// The test cycles through each component automatically:
//   1. OLED display - shows text
//   2. Servo motor - sweeps back and forth
//   3. Light sensor - prints analog readings
//
// Watch the serial monitor (115200 baud) for status messages.

#include <Arduino.h>
#include <ESP32Servo.h>
#include "SSD1306Wire.h"

#include "config.h"

SSD1306Wire oled(OLED_ADDR, PIN_SDA, PIN_SCL);
Servo testServo;

// ============================================================
// Test 1: OLED Display
// ============================================================
void testDisplay() {
  Serial.println("\n========== TEST 1: OLED DISPLAY ==========");
  Serial.println("You should see text appear on the OLED screen.");
  Serial.println("If the screen stays blank, check:");
  Serial.println("  - SDA wire goes to GPIO 21");
  Serial.println("  - SCL wire goes to GPIO 22");
  Serial.println("  - VCC goes to 3.3V (NOT 5V)");
  Serial.println("  - GND goes to GND");
  Serial.println("  - I2C address is 0x3C (most common)");

  oled.init();
  oled.flipScreenVertically();
  oled.setColor(WHITE);

  // Screen 1: Simple text
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(64, 10, "OLED works!");
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 35, "Lovebox test");
  oled.display();
  delay(3000);

  // Screen 2: Draw some shapes to test all pixels
  oled.clear();
  oled.drawRect(0, 0, 128, 64);        // Border
  oled.drawLine(0, 0, 127, 63);        // Diagonal
  oled.drawLine(127, 0, 0, 63);        // Other diagonal
  oled.drawCircle(64, 32, 20);         // Center circle
  oled.display();

  Serial.println("[PASS] Display commands sent. Check the screen!");
  delay(3000);

  // Screen 3: Show ready for next test
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 20, "Display OK");
  oled.drawString(64, 35, "Next: Servo test");
  oled.display();
  delay(2000);
}

// ============================================================
// Test 2: Servo Motor
// ============================================================
void testServo_run() {
  Serial.println("\n========== TEST 2: SERVO MOTOR ==========");
  Serial.println("The servo should sweep back and forth.");
  Serial.println("If it doesn't move, check:");
  Serial.println("  - Signal (orange/yellow) wire goes to GPIO 13");
  Serial.println("  - Power (red) wire goes to 5V (VIN pin)");
  Serial.println("  - Ground (brown/black) wire goes to GND");
  Serial.println("  - Make sure ESP32 GND and servo GND are shared");

  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 10, "SERVO TEST");
  oled.drawString(64, 30, "Watch the servo");
  oled.drawString(64, 45, "sweep back & forth");
  oled.display();

  testServo.attach(PIN_SERVO);
  testServo.write(90);  // Start at center
  delay(500);

  // Sweep from center to min to max and back, 3 times
  for (int cycle = 0; cycle < 3; cycle++) {
    Serial.printf("  Sweep cycle %d/3\n", cycle + 1);

    // Sweep to minimum
    for (int pos = 90; pos >= SERVO_MIN_ANGLE; pos--) {
      testServo.write(pos);
      delay(20);
    }
    // Sweep to maximum
    for (int pos = SERVO_MIN_ANGLE; pos <= SERVO_MAX_ANGLE; pos++) {
      testServo.write(pos);
      delay(20);
    }
    // Sweep back to center
    for (int pos = SERVO_MAX_ANGLE; pos >= 90; pos--) {
      testServo.write(pos);
      delay(20);
    }
  }

  testServo.write(90);  // Park at center
  delay(200);
  testServo.detach();

  Serial.println("[PASS] Servo sweep complete!");
  delay(1000);

  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.drawString(64, 20, "Servo OK");
  oled.drawString(64, 35, "Next: Light sensor");
  oled.display();
  delay(2000);
}

// ============================================================
// Test 3: Light Sensor (LDR)
// ============================================================
void testLightSensor() {
  Serial.println("\n========== TEST 3: LIGHT SENSOR ==========");
  Serial.println("Reading analog values from GPIO 34 for 15 seconds.");
  Serial.println("Try covering and uncovering the sensor!");
  Serial.println("If values don't change, check:");
  Serial.println("  - LDR leg 1 goes to 3.3V");
  Serial.println("  - LDR leg 2 goes to GPIO 34 AND one leg of 10k resistor");
  Serial.println("  - 10k resistor other leg goes to GND");
  Serial.printf("  - Current threshold is %d (configurable in config.h)\n", LIGHT_THRESHOLD);

  unsigned long start = millis();
  int minVal = 4095, maxVal = 0;

  while (millis() - start < 15000) {
    int reading = analogRead(PIN_LIGHT);
    if (reading < minVal) minVal = reading;
    if (reading > maxVal) maxVal = reading;

    // Show on OLED
    oled.clear();
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.setFont(ArialMT_Plain_10);
    oled.drawString(64, 0, "LIGHT SENSOR TEST");

    oled.setFont(ArialMT_Plain_24);
    oled.drawString(64, 15, String(reading));

    oled.setFont(ArialMT_Plain_10);
    String status = reading > LIGHT_THRESHOLD ? "BRIGHT (box open)" : "DARK (box closed)";
    oled.drawString(64, 42, status);

    // Draw a bar graph
    int barWidth = map(reading, 0, 4095, 0, 120);
    oled.fillRect(4, 56, barWidth, 6);
    oled.drawRect(4, 56, 120, 6);
    oled.display();

    Serial.printf("  Light: %4d  %s\n", reading, status.c_str());
    delay(250);
  }

  Serial.printf("[DONE] Light sensor range: %d - %d\n", minVal, maxVal);
  if (maxVal - minVal < 100) {
    Serial.println("[WARN] Very little variation detected. Check wiring or try");
    Serial.println("       covering the sensor with your hand to block light.");
  } else {
    Serial.println("[PASS] Good variation detected!");
  }
}

// ============================================================
// Setup & Loop
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);  // Give serial monitor time to connect

  Serial.println("\n=============================================");
  Serial.println("   LOVEBOX COMPONENT TEST - BREADBOARD RUN");
  Serial.println("=============================================");
  Serial.println("This will test each component one by one.");
  Serial.println("Watch this serial output and the hardware.\n");

  // Run all tests in sequence
  testDisplay();
  testServo_run();
  testLightSensor();

  // Final summary
  Serial.println("\n=============================================");
  Serial.println("   ALL TESTS COMPLETE");
  Serial.println("=============================================");
  Serial.println("If everything worked, you're ready to flash");
  Serial.println("the real firmware: pio run -e esp32 --target upload");
  Serial.println("\nBut first, set up your Telegram bot token in");
  Serial.println("include/config.h (see README for instructions).");

  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(64, 5, "All tests");
  oled.drawString(64, 25, "passed!");
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 50, "Ready for firmware");
  oled.display();
}

void loop() {
  // Nothing to do - tests run once in setup()
  // The ESP32 just idles here. Reset the board to re-run tests.
  delay(1000);
}
