# ESP32-C3-EggSentinel
## ESP32-C3 Wi-Fi and Bluetooth Security Monitor

Buy the ESP32-C3 EGG used https://s.click.aliexpress.com/e/_c4oyN1mt

<img width="751" height="1104" alt="esp32c3 oled schematic" src="https://github.com/user-attachments/assets/0a7ac87a-31dc-4c35-b0c4-85acf7f091e5" />

## What this detects

| Detection                         | Always on?                     |
|-----------------------------------|--------------------------------|
| Deauth/disassoc flood             | Yes                            |
| Rogue/evil-twin AP                | Yes                            |
| ARP spoofing/MITM                 | Yes                            |
| New device on LAN                 | Yes                            |
| Self port-scan                    | Yes                            |
| BLE advertisement flood           | Only if Bluetooth toggle is ON |
| New/unknown BLE device nearby     | Only if Bluetooth toggle is ON |


## Flashing to ESP32-C3

1. Download the EggSentiel Folder
2. Open or download Ardunino IDF
3. Install ESP32 board manager
4. Set board to ESP32C3 Dev Module
5. Select Partition Scheme to Minimal SPIFFS (1.9MB APP with OTA)
6. Install the Libarries needed (see below)
7. Verify & Upload 

## Libraries needed (Arduino Library Manager)

- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson (v6.x)
- **NimBLE-Arduino** (by h2zero) — confirmed working
  
## Setup flow, step by step

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

## Files in EggSentiel Folder

- `EggSentinel.ino` — main firmware
- `dashboard_html.h` — full dashboard (Wi-Fi stats, BLE toggle + stats
  when enabled, event log, Telegram settings, Wi-Fi reset)
- `setup_html.h` — setup wizard (plain HTML form + zero-JS datalist
  network picker, deliberately no JS dependency for the save path)
