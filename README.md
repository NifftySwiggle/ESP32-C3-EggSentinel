# ESP32-C3 Wi-Fi and Bluetooth Security Monitor
Buy the ESP32-C3 EGG used or contact me to get one.
[Buy from AliExpress](https://s.click.aliexpress.com/e/_c4oyN1mt)

## 📑 Table of Contents
- [Flashing Device](#flashing-device)
- [Device Setup](#device-setup)
- [What This Detects](#what-this-detects)
- [Source Code](#source-code)
- [Build Instructions](#build-instructions)
- [Libraries Needed](#libraries-needed)
- [ESP32C3 Egg Schematic](#esp32c3-egg-schematic)
- [Contributing](#contributing)

  
## Flashing Device

### Option 1 — Flash Using the Web Flasher (Recommended)
This is the easiest method. No Arduino IDE needed.
👉 [Web Flasher](https://nifftyswiggle.com/eggsentinel)

### Option 2 — Flash Manually
Download the merged firmware BIN:
👉 [EggSentinel.bin](EggSentinel.bin)

Use any flashing tool you prefer.
My recommended tool is [ESP‑Connect](https://thelastoutpostworkshop.github.io/ESPConnect/)

1. Plug in your ESP32‑C3
2. Click Connect
3. Select your COM/USB port
4. Choose Flash Firmware
5. Select the merged .bin file
6. Click Flash
7. Wait for the flashing process to complete

## Device Setup

1. Power on with no saved Wi-Fi (first boot, or after "Redo Wi-Fi
   setup" from the dashboard)
2. It broadcasts "EggSentinel_Setup" as its own hotspot
3. Connect to it — a setup page should pop up automatically on most
   phones/laptops; if not, visit
   `http://192.168.4.1` directly
4. Tap a nearby network from the suggestions, or type one manually
5. Enter the Wi-Fi password, optionally Telegram bot token + chat ID
6. Tap "Connect & finish setup"
7. It saves, confirms, and restarts into your home network
8. Dashboard available at `http://eggsentinel.local`

To redo setup later, use "Redo Wi-Fi setup" on the dashboard.
To turn Bluetooth detection on/off, use the toggle under "Bluetooth
detection" on the dashboard — works instantly, no reboot.

## What this Detects

| Detection                         | Always on?                     |
|-----------------------------------|--------------------------------|
| Deauth/disassoc flood             | Yes                            |
| Rogue/evil-twin AP                | Yes                            |
| ARP spoofing/MITM                 | Yes                            |
| New device on LAN                 | Yes                            |
| Self port-scan                    | Yes                            |
| BLE advertisement flood           | Only if Bluetooth toggle is ON |
| New/unknown BLE device nearby     | Only if Bluetooth toggle is ON |



# Source Code 
For Developers

## Build Instructions

1. Download the EggSentiel Folder
2. Open or download Ardunino IDF
3. Install ESP32 board manager
4. Set board to ESP32C3 Dev Module
5. Select Partition Scheme to Minimal SPIFFS (1.9MB APP with OTA)
6. Install the Libarries needed (see below) 

## Libraries Needed 
Arduino Library Manager

- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson (v6.x)
- **NimBLE-Arduino** (by h2zero) — confirmed working

## ESP32C3 Egg Schematic
<img width="751" height="1104" alt="esp32c3 oled schematic" src="https://github.com/user-attachments/assets/0a7ac87a-31dc-4c35-b0c4-85acf7f091e5" />

## Contributing

Contributions are welcome — whether it’s fixing a bug, improving detection logic, adding new features, or enhancing the dashboard UI.

### How to contribute

1. Fork the repository
2. Create a new branch for your feature or fix
3. Make your changes
4. Submit a Pull Request with a short description of what you changed
I’ll review it and merge if everything looks good

### Ways you can help

- Improve Wi‑Fi or BLE detection logic
- Add new security checks
- Optimize performance on the ESP32‑C3
- Improve the dashboard HTML/CSS
- Add documentation or examples
- Report bugs using the Issues tab

### Before submitting a PR

- Make sure the code compiles for ESP32‑C3 Dev Module
- Use the Minimal SPIFFS (1.9MB APP with OTA) partition scheme
- Keep formatting consistent with the existing .ino
- Test your changes if possible
