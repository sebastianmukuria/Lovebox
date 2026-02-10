#pragma once

// ============================================================
// Lovebox Configuration
// ============================================================

// --- Telegram Bot ---
// 1. Open Telegram, search for @BotFather
// 2. Send /newbot, follow prompts to name your bot
// 3. BotFather gives you a token like "123456789:ABCdefGHI..."
// 4. Paste that token below
#define TELEGRAM_BOT_TOKEN "YOUR_BOT_TOKEN_HERE"

// --- Hardware Pins (ESP32 WEMOS D1 Mini32) ---
// I2C for OLED display
#define PIN_SDA 21       // ESP32 default I2C SDA
#define PIN_SCL 22       // ESP32 default I2C SCL

// Servo motor
#define PIN_SERVO 13     // Any PWM-capable GPIO

// Light sensor (analog)
#define PIN_LIGHT 34     // ESP32 ADC1 channel (safe to use with WiFi)

// --- Display ---
#define OLED_ADDR 0x3C   // I2C address of SSD1306
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- Behavior ---
#define LIGHT_THRESHOLD 1500     // ADC reading to detect box opened (ESP32 ADC is 12-bit: 0-4095)
#define POLL_INTERVAL_MS 10000   // Check Telegram every 10 seconds
#define SERVO_MIN_ANGLE 75       // Servo sweep range
#define SERVO_MAX_ANGLE 105
#define SERVO_STEP_DELAY_MS 50   // Delay between servo steps

// --- WiFiManager ---
#define AP_NAME "Lovebox-Setup"  // Name of the WiFi network the box creates on first boot
