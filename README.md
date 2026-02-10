❤️ Lovebox ❤️
===

DIY project to lasercut a lovebox inspired from https://en.lovebox.love/. Send messages or pictures from your phone via Telegram -- when a new message arrives, the heart spins on the front of the box. It stops when the box is opened (detected by a built-in light sensor). Have fun building your own!

This is a modernized fork of [julisa99/Lovebox](https://github.com/julisa99/Lovebox), updated for current hardware and tooling (ESP32, PlatformIO, Telegram Bot, WiFiManager).

![](https://github.com/julisa99/Lovebox/blob/master/docs/images/demo_lovebox.gif)

## What changed from the original

| Area | Original (2018) | This fork |
|---|---|---|
| Microcontroller | ESP8266 (WeMos D1 Mini) | **ESP32** (WEMOS D1 Mini32 or any ESP32 devkit) |
| Build system | Arduino IDE (manual library install) | **PlatformIO** (pinned deps, one-command build) |
| Message backend | GitHub Gist (manual editing, 60s polling) | **Telegram Bot** (send from your phone, 10s polling) |
| WiFi setup | Hardcoded in `credentials.h` | **WiFiManager** captive portal (phone-based setup) |
| TLS | SHA-1 fingerprint pinning (broken/expired) | **CA root certificate** (valid until 2038) |
| Storage | EEPROM (no wear leveling) | **NVS Preferences** (wear-leveled flash) |

## Materials

1. [3mm](https://www.amazon.de/dp/B0002FQKX4) or [4mm wood](https://www.amazon.de/dp/B06Y48L765) (to cut the case + heart)
2. **ESP32 dev board** -- any ESP32 DevKit or WEMOS D1 Mini32 (~$3-5)
3. [Servo motor](https://www.amazon.de/dp/B07236KYVC)
4. [Light sensor](https://www.amazon.de/dp/B00SWO73DS)
5. [10kΩ resistor](https://www.amazon.de/dp/B072BHDBDG)
6. [OLED display](https://www.amazon.de/dp/B074NJMPYJ) (SSD1306 128x64, I2C)
7. [Perfboard](https://www.amazon.de/dp/B00VL1KHJQ)
8. [Breakaway PCB Connector](https://www.amazon.de/dp/B01M69EA9O)
9. [1mm acryl](https://www.amazon.de/dp/B07T4JSXYR) (to cut the display)
10. [Darkening foil/dazzle strip](https://www.amazon.de/dp/B001CC6VYO)

## Wiring (ESP32)

| Component | ESP32 Pin |
|---|---|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Servo signal | GPIO 13 |
| Light sensor | GPIO 34 (ADC1) |

> **Note:** On ESP32, ADC2 pins (GPIOs 0, 2, 4, 12-15, 25-27) cannot be used while WiFi is active. The light sensor must be on an ADC1 pin (GPIO 32-39). GPIO 34 is used here.

All pin assignments can be changed in `include/config.h`.

## Assembly

Below you can see where the OLED display, the light sensor with resistor and the servo motor are connected to the microcontroller. The second photo is the soldered assembly. The micro-USB connector of the microcontroller board is at the ground. On top of the microcontroller a perfboard is soldered. The servo motor is added and also soldered to the perfboard. Notice the resistor and the light sensor that are also soldered to the board, with the light sensor pointing up. The display is soldered to another perfboard which provides a flat surface to hold the display in place. It is connected to the other perfboard by using a Breakaway PCB Connector as a spacer above the motor.

![](https://github.com/julisa99/Lovebox/blob/master/docs/images/composition.JPG)

After finishing the electronics, cut the case, cap and hearts out of the wood and the display out of the 1mm acryl with a lasercutter. [Four SVGs are provided](https://github.com/julisa99/Lovebox/tree/master/lasercut_files) containing the cutting plan. Glue the little heart on the larger heart and paint it red. Cut the size of the display out of the darkening foil and put it on the acryl display. Stick the white servo horn into the back of the heart. Assemble the case and cap, put the hardware inside, and you're ready to program.

![](https://github.com/julisa99/Lovebox/blob/master/docs/images/heart.jpg)

## Setup

### 1. Create a Telegram Bot

1. Open Telegram on your phone
2. Search for **@BotFather** and start a conversation
3. Send `/newbot`
4. Follow the prompts -- choose a name and username for your bot
5. BotFather will give you a **token** like `123456789:ABCdefGHIjklMNO...`
6. Copy this token -- you'll need it in step 3

### 2. Install PlatformIO

Install [PlatformIO CLI](https://platformio.org/install/cli) or the [VS Code extension](https://platformio.org/install/ide?install=vscode).

With pip:
```
pip install platformio
```

### 3. Configure and flash

1. Clone this repo:
   ```
   git clone https://github.com/sebastianmukuria/Lovebox.git
   cd Lovebox
   ```

2. Open `include/config.h` and paste your Telegram bot token:
   ```c
   #define TELEGRAM_BOT_TOKEN "123456789:ABCdefGHIjklMNO..."
   ```

3. Adjust pin numbers in `config.h` if your wiring differs from the table above.

4. Connect your ESP32 via USB and flash:
   ```
   pio run --target upload
   ```

5. Open the serial monitor to see status messages:
   ```
   pio device monitor
   ```

### 4. Connect to WiFi

On first boot, the Lovebox creates a WiFi network called **"Lovebox-Setup"**. Connect to it with your phone -- a captive portal will open where you select your home WiFi and enter the password. The credentials are saved and the box will auto-connect on future boots.

To reset WiFi settings, erase the ESP32's flash:
```
pio run --target erase
```

### 5. Send a message

Open Telegram and send a text message to your bot. Within 10 seconds, the Lovebox will display it and start spinning the heart. Open the box (letting light hit the sensor) to mark it as read.

## How it works

The ESP32 polls the [Telegram Bot API](https://core.telegram.org/bots/api) every 10 seconds using `getUpdates`. When a new message is found:

1. The message text is displayed on the OLED
2. The message and its ID are saved to NVS (non-volatile storage) so they survive reboots
3. The servo starts spinning the heart
4. The light sensor watches for the box to be opened (analog reading > threshold)
5. When opened, the heart stops and the box returns to polling mode

## Project structure

```
Lovebox/
├── platformio.ini       # Build config, board, library dependencies
├── include/
│   └── config.h         # All tunable settings (pins, token, thresholds)
├── src/
│   └── main.cpp         # Firmware source
├── Lovebox.ino          # Original ESP8266 firmware (reference only)
├── credentials.h        # Original credentials file (reference only)
├── lasercut_files/      # SVGs for laser cutting the case
├── message_examples/    # Example images for the display
└── docs/                # Photos and documentation
```

## Acknowledgements

This fork builds on the original [Lovebox](https://github.com/julisa99/Lovebox) by [@julisa99](https://github.com/julisa99).

Thanks to [@blblblu](https://github.com/blblblu/) for the idea of hosting the message content for free on GitHub Gist and [Deus](https://github.com/Daniel-Amadeus/) for helping with the first lasercut attempts.

[Marcel](https://github.com/marcelstoer/) (co-founder of ThingPulse, which provides the open-source display driver used in the project) was one of the first who [wrote about the project and added more photos of the soldering](https://frightanic.com/iot/the-lovebox-a-gift-from-a-maker/).

Thanks to [@Nounnours](https://github.com/Nounnours/) who found and fixed the display error with binary images.

###### tags: `Lovebox` `ESP32` `Telegram` `PlatformIO`
