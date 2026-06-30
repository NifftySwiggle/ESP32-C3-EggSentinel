/*
  ================================================================
   ================================================================
   EGG SENTINEL — ESP32-C3 Wi-Fi Security Monitor
   Created by NifftySwiggle
  ================================================================
  Hardware : ESP32-C3 "egg" board, built-in 0.42" SSD1306 OLED
             (128x64 driver buffer, 72x40 visible glass at offset
              x+28, y+24 — your confirmed working settings)

  ALERTS: OLED (flashes on alert, animated mascot) + web dashboard
  (http://eggsentinel.local) + optional Telegram.

  LIBRARIES NEEDED (Library Manager):
    - Adafruit GFX Library
    - Adafruit SSD1306
    - ArduinoJson (v6.x)
    - NimBleDevice

  PARTITION SCHEME
  "Minimal SPIFFS (1.9MB APP with OTA)" — this is the best fit: 
   gives the app partition roughly 1.9MB (up from 1.2MB), 
   more than enough headroom for the current sketch plus NimBLE, 
   while keeping a tiny SPIFFS partition (in case anything ever needs it) 
   and keeping OTA capability intact.
   
   If that exact label isn't in your menu, 
   the next-best alternatives in order of preference:
   "No OTA (2MB APP/2MB SPIFFS)" — also works, 
   slightly more total room but loses OTA capability 
   (not something this sketch uses anyway, so no real loss).
   "Huge APP (3MB No OTA)" — way more than needed,
    but works too if the above options aren't present.
  ================================================================

  WHAT THIS DOES (read before relying on it):
  This is a DEFENSIVE Wi-Fi monitor. An ESP32 has one radio and
  cannot decrypt other devices' encrypted traffic. This code does
  not attempt offensive actions (no deauthing others, no attacks).
  What it DOES do:
    1. DEAUTH / DISASSOC FLOOD DETECTION (Wi-Fi mgmt-frame sniffing)
    2. ROGUE / EVIL-TWIN AP DETECTION (SSID seen from wrong BSSID)
    3. ARP SPOOFING / GATEWAY MAC-CHANGE DETECTION
    4. NEW DEVICE / LAN CENSUS (periodic ARP scan)
    5. SELF PORT-SCAN DETECTION
  ================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <NimBLEDevice.h>
#include "dashboard_html.h"
#include "setup_html.h"
extern "C" {
  #include "lwip/etharp.h"
}

// ================================================================
//  CONFIG
// ================================================================
#define DEVICE_NAME        "EggSentinel"
#define WEB_PORT           80
#define SETUP_AP_SSID      "EggSentinel_Setup"
#define WIFI_CONNECT_TIMEOUT_MS   20000

#define DEAUTH_THRESHOLD       6
#define DEAUTH_WINDOW_MS       10000
#define ARP_SCAN_INTERVAL_MS   60000
#define AP_SCAN_INTERVAL_MS    45000
#define PORTSCAN_THRESHOLD      8
#define PORTSCAN_WINDOW_MS     5000
#define ALERT_COOLDOWN_MS      30000
#define BLE_SCAN_INTERVAL_MS    30000  // how often a BLE burst starts
#define BLE_SCAN_DURATION_SEC   6      // length of each burst
#define BLE_SPAM_THRESHOLD      40     // adverts/burst that triggers an alert
#define BLE_MAX_TRACKED_DEVICES 40     // ring buffer cap, keeps RAM bounded

// ================================================================
//  Display geometry — your confirmed working settings
// ================================================================
#define OLED_DRIVER_W  128
#define OLED_DRIVER_H  64
#define OLED_I2C_ADDR  0x3C
#define SCREEN_OFFSET_X  28
#define SCREEN_OFFSET_Y  24
#define VISIBLE_W  72
#define VISIBLE_H  40

Adafruit_SSD1306 display(OLED_DRIVER_W, OLED_DRIVER_H, &Wire, -1);

// ================================================================
//  Globals
// ================================================================
Preferences prefs;
WebServer server(WEB_PORT);
DNSServer dnsServer;

enum DeviceMode { MODE_SETUP, MODE_MONITOR };
DeviceMode g_mode = MODE_SETUP;

String  g_wifiSsid = "";
String  g_wifiPass = "";
String  g_telegramToken;
String  g_telegramChatId;
bool    g_telegramEnabled = false;

volatile uint32_t g_deauthCount = 0;
volatile unsigned long g_deauthWindowStart = 0;
volatile uint32_t g_deauthTotalLifetime = 0;

String  g_gatewayMac = "";
String  g_lastKnownGatewayMac = "";
bool    g_gatewayMacInit = false;

uint32_t g_lastAlertTime[10] = {0};
enum AlertType {
  ALERT_DEAUTH = 0,
  ALERT_ROGUE_AP,
  ALERT_ARP_SPOOF,
  ALERT_NEW_DEVICE,
  ALERT_PORTSCAN,
  ALERT_WIFI_DOWN,
  ALERT_BOOT,
  ALERT_BLE_SPAM,
  ALERT_BLE_NEW_DEVICE,
  ALERT_TYPE_COUNT
};

struct KnownDevice {
  String mac;
  String ip;
  unsigned long lastSeen;
};
std::vector<KnownDevice> g_knownDevices;
bool g_lanBaselineEstablished = false;

struct LogEntry {
  unsigned long timestamp;
  String type;
  String message;
};
std::vector<LogEntry> g_eventLog;
#define MAX_LOG_ENTRIES 40

unsigned long g_bootTime = 0;
unsigned long g_lastArpScan = 0;
unsigned long g_lastApScan = 0;
unsigned long g_lastDailySummary = 0;
int g_rogueApCount = 0;
int g_newDeviceCount = 0;
int g_lanDeviceCount = 0;

std::vector<unsigned long> g_recentConnTimes;

enum SystemStatus { STATUS_OK, STATUS_WARNING, STATUS_ALERT };
SystemStatus g_currentStatus = STATUS_OK;
String g_statusMessage = "All clear";
unsigned long g_statusMessageUntil = 0;

int g_displayPage = 0;
unsigned long g_lastPageFlip = 0;
#define PAGE_FLIP_MS 4000
#define NUM_PAGES 4

String g_lastManualScanSummary = "";

// ================================================================
//  BLUETOOTH (toggleable from the dashboard — off by default so a
//  fresh setup behaves exactly like the Wi-Fi-only version unless
//  you turn it on)
// ================================================================
bool g_bleEnabled = false; // persisted; flip from the dashboard
struct BleDevice {
  String addr;
  String name;
  int rssi;
  unsigned long lastSeen;
};
std::vector<BleDevice> g_knownBleDevices;
bool g_bleBaselineEstablished = false;
int g_bleAdvertCountThisBurst = 0;
int g_bleSpamEventCount = 0;
bool g_bleScanInProgress = false;
unsigned long g_lastBleScan = 0;
NimBLEScan* g_bleScan = nullptr;
bool g_bleInitialized = false;

class EggBleCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice);
};
EggBleCallbacks* g_bleCallbacks = nullptr;

// ================================================================
//  Forward declarations
// ================================================================
void loadSettings();
void saveSettings();
bool connectWiFiSTA();
void enterSetupMode();
void enterMonitorMode();
void setupPromiscuousSniffer();
void IRAM_ATTR wifiSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type);
void checkDeauthFlood();
void scanForRogueAP();
void checkGatewayMac();
void scanLanDevices();
void runFullScanNow();
void setupBLE();
void teardownBLE();
void startBleScanBurst();
void processBleAdvertisement(const NimBLEAdvertisedDevice* device);
void handleApiBleToggle();
void sendTelegramAlert(String message);
void raiseAlert(AlertType type, String message, SystemStatus severity);
void addLogEntry(String type, String message);
void setupSetupModeServer();
void setupMonitorModeServer();
void handleSetupRoot();
void handleSetupSave();
void handleCaptiveRedirect();
void handleDashboardRoot();
void handleApiStatus();
void handleApiLog();
void handleApiSettings();
void handleApiTestTelegram();
void handleApiAck();
void handleApiScanNow();
void handleApiResetWifi();
void drawDisplay();
void drawBootScreen();
void drawSetupScreen();
void drawHomePage();
void drawStatsPage();
void drawNetworkPage();
void drawMascotPage();
void drawDogIdle(int cx, int cy);
void drawDogAlert(int cx, int cy);
void drawDogHappy(int cx, int cy);
String formatUptime(unsigned long ms);

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== EGG SENTINEL booting ===");

  Wire.begin(5, 6); // SDA = 5, SCL = 6 — your confirmed working pins
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("OLED not found at 0x3C — check wiring/address.");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawBootScreen();

  loadSettings();
  g_bootTime = millis();

  if (g_wifiSsid.length() > 0 && connectWiFiSTA()) {
    enterMonitorMode();
  } else {
    enterSetupMode();
  }
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (g_mode == MODE_SETUP) {
    drawDisplay();
    return;
  }

  // ---- MODE_MONITOR ----
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      raiseAlert(ALERT_WIFI_DOWN, "⚠️ Lost Wi-Fi connection. Reconnecting...", STATUS_WARNING);
      if (!connectWiFiSTA()) {
        Serial.println("Reconnect failed — falling back to setup mode.");
        enterSetupMode();
        return;
      }
    }
  }

  checkDeauthFlood();

  if (millis() - g_lastApScan > AP_SCAN_INTERVAL_MS) {
    g_lastApScan = millis();
    scanForRogueAP();
    checkGatewayMac();
  }

  if (millis() - g_lastArpScan > ARP_SCAN_INTERVAL_MS) {
    g_lastArpScan = millis();
    scanLanDevices();
  }

  if (g_bleEnabled && g_bleInitialized && !g_bleScanInProgress && millis() - g_lastBleScan > BLE_SCAN_INTERVAL_MS) {
    g_lastBleScan = millis();
    startBleScanBurst();
  }

  if (millis() - g_lastDailySummary > 86400000UL) {
    g_lastDailySummary = millis();
    String summary = "📋 Daily Egg Sentinel summary:\n";
    summary += "Uptime: " + formatUptime(millis() - g_bootTime) + "\n";
    summary += "Deauth frames seen: " + String(g_deauthTotalLifetime) + "\n";
    summary += "Devices on LAN: " + String(g_lanDeviceCount) + "\n";
    summary += "Rogue APs flagged: " + String(g_rogueApCount);
    sendTelegramAlert(summary);
  }

  if (g_currentStatus != STATUS_OK && millis() > g_statusMessageUntil) {
    g_currentStatus = STATUS_OK;
    g_statusMessage = "All clear";
  }

  drawDisplay();
}

// ================================================================
//  SETTINGS (persisted in NVS flash via Preferences)
// ================================================================
void loadSettings() {
  prefs.begin("eggsentinel", false);
  g_wifiSsid        = prefs.getString("wifi_ssid", "");
  g_wifiPass        = prefs.getString("wifi_pass", "");
  g_telegramToken   = prefs.getString("tg_token", "");
  g_telegramChatId  = prefs.getString("tg_chat", "");
  g_bleEnabled      = prefs.getBool("ble_on", false);
  prefs.end();
  g_telegramEnabled = (g_telegramToken.length() > 10 && g_telegramChatId.length() > 0);
}

void saveSettings() {
  prefs.begin("eggsentinel", false);
  prefs.putString("wifi_ssid", g_wifiSsid);
  prefs.putString("wifi_pass", g_wifiPass);
  prefs.putString("tg_token", g_telegramToken);
  prefs.putString("tg_chat", g_telegramChatId);
  prefs.putBool("ble_on", g_bleEnabled);
  prefs.end();
  g_telegramEnabled = (g_telegramToken.length() > 10 && g_telegramChatId.length() > 0);
}

// ================================================================
//  WI-FI CONNECT
// ================================================================
bool connectWiFiSTA() {
  WiFi.mode(WIFI_STA);
  Serial.println("Connecting to Wi-Fi: " + g_wifiSsid);
  WiFi.begin(g_wifiSsid.c_str(), g_wifiPass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected. IP: " + WiFi.localIP().toString());
    WiFi.setSleep(false);
    return true;
  }
  Serial.println("Wi-Fi connect failed.");
  return false;
}

// ================================================================
//  MODE SWITCHING
// ================================================================
void enterSetupMode() {
  g_mode = MODE_SETUP;
  server.stop();
  dnsServer.stop();
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_AP_SSID);
  delay(200);
  IPAddress apIp = WiFi.softAPIP(); // normally 192.168.4.1
  dnsServer.start(53, "*", apIp);

  setupSetupModeServer();

  Serial.println("Setup mode active.");
  Serial.println("Join Wi-Fi: " + String(SETUP_AP_SSID));
  Serial.println("Setup page: http://" + apIp.toString());
}

void enterMonitorMode() {
  g_mode = MODE_MONITOR;
  dnsServer.stop();
  server.stop();

  setupPromiscuousSniffer();
  setupMonitorModeServer();

  if (g_bleEnabled) {
    setupBLE();
  }

  if (MDNS.begin(DEVICE_NAME)) {
    MDNS.addService("http", "tcp", WEB_PORT);
    Serial.println("mDNS started: http://" + String(DEVICE_NAME) + ".local");
  }

  addLogEntry("BOOT", "Egg Sentinel started, IP " + WiFi.localIP().toString());
  raiseAlert(ALERT_BOOT, "🥚 Egg Sentinel is online.\nIP: " + WiFi.localIP().toString(), STATUS_OK);

  scanLanDevices();
  g_lanBaselineEstablished = true;
  checkGatewayMac();
  g_gatewayMacInit = true;

  g_lastArpScan = millis();
  g_lastApScan = millis();
  g_lastDailySummary = millis();
}

// ================================================================
//  802.11 PROMISCUOUS SNIFFER — deauth / disassoc detection
// ================================================================
void IRAM_ATTR wifiSnifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  uint16_t frameControl = (payload[1] << 8) | payload[0];
  uint8_t frameSubType = (frameControl >> 4) & 0x0F;
  uint8_t frameType = (frameControl >> 2) & 0x03;

  if (frameType == 0 && (frameSubType == 12 || frameSubType == 10)) {
    g_deauthCount++;
    g_deauthTotalLifetime++;
  }
}

void setupPromiscuousSniffer() {
  esp_wifi_set_promiscuous(true);
  wifi_promiscuous_filter_t filter;
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&wifiSnifferCallback);
  g_deauthWindowStart = millis();
}

void checkDeauthFlood() {
  unsigned long now = millis();
  if (now - g_deauthWindowStart > DEAUTH_WINDOW_MS) {
    if (g_deauthCount >= DEAUTH_THRESHOLD) {
      String msg = "🚨 Deauth/disassoc flood detected!\n";
      msg += String(g_deauthCount) + " frames in " + String(DEAUTH_WINDOW_MS / 1000) + "s.\n";
      msg += "Possible Wi-Fi jamming/deauth attack nearby.";
      raiseAlert(ALERT_DEAUTH, msg, STATUS_ALERT);
    }
    g_deauthCount = 0;
    g_deauthWindowStart = now;
  }
}

// ================================================================
//  ROGUE / EVIL-TWIN AP DETECTION
// ================================================================
void scanForRogueAP() {
  String myBssid = WiFi.BSSIDstr();
  int n = WiFi.scanNetworks(false, true);
  bool sawImpersonator = false;
  String impersonatorMac = "";

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == g_wifiSsid) {
      String foundBssid = WiFi.BSSIDstr(i);
      if (foundBssid != myBssid) {
        sawImpersonator = true;
        impersonatorMac = foundBssid;
      }
    }
  }
  WiFi.scanDelete();

  if (sawImpersonator) {
    g_rogueApCount++;
    String msg = "🚨 Possible evil-twin AP detected!\n";
    msg += "SSID \"" + g_wifiSsid + "\" seen from unknown AP:\n";
    msg += impersonatorMac + "\n";
    msg += "Your real AP: " + myBssid;
    raiseAlert(ALERT_ROGUE_AP, msg, STATUS_ALERT);
  }
}

// ================================================================
//  ARP SPOOFING / MITM DETECTION
// ================================================================
void checkGatewayMac() {
  IPAddress gwIp = WiFi.gatewayIP();
  if (gwIp == IPAddress(0,0,0,0)) return;
  if (WiFi.status() != WL_CONNECTED) return;

  {
    WiFiClient probe;
    probe.connect(gwIp, 80, 100);
    probe.stop();
  }

  ip4_addr_t ipaddr;
  ipaddr.addr = static_cast<uint32_t>(gwIp);
  struct eth_addr* ethret = NULL;
  const ip4_addr_t* ipret = NULL;

  int8_t idx = etharp_find_addr(NULL, &ipaddr, &ethret, &ipret);
  if (idx < 0 || ethret == NULL) return;

  char macBuf[18];
  snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
           ethret->addr[0], ethret->addr[1], ethret->addr[2],
           ethret->addr[3], ethret->addr[4], ethret->addr[5]);
  g_gatewayMac = String(macBuf);

  if (!g_gatewayMacInit) {
    g_lastKnownGatewayMac = g_gatewayMac;
    return;
  }

  if (g_lastKnownGatewayMac.length() > 0 && g_gatewayMac != g_lastKnownGatewayMac) {
    String msg = "🚨 ARP SPOOFING WARNING!\n";
    msg += "Gateway MAC changed unexpectedly.\n";
    msg += "Was: " + g_lastKnownGatewayMac + "\n";
    msg += "Now: " + g_gatewayMac + "\n";
    msg += "Possible man-in-the-middle attack.";
    raiseAlert(ALERT_ARP_SPOOF, msg, STATUS_ALERT);
    g_lastKnownGatewayMac = g_gatewayMac;
  }
}

// ================================================================
//  LAN DEVICE CENSUS — new/unrecognized device detection
// ================================================================
void scanLanDevices() {
  IPAddress localIp = WiFi.localIP();
  IPAddress subnet  = WiFi.subnetMask();
  if (localIp == IPAddress(0,0,0,0)) return;

  uint32_t ipInt    = (uint32_t)localIp;
  uint32_t maskInt  = (uint32_t)subnet;
  uint32_t network  = ipInt & maskInt;
  uint32_t hostBits = ~maskInt;
  uint32_t maxHosts = hostBits;
  if (maxHosts > 254) maxHosts = 254;

  WiFiClient probe;
  for (uint32_t h = 1; h <= maxHosts; h++) {
    uint32_t candidate = network | h;
    IPAddress target(candidate);
    if (target == localIp) continue;
    probe.connect(target, 80, 15);
    probe.stop();
    yield();
  }
  delay(150);

  std::vector<KnownDevice> currentScan;
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t* ipaddr;
    struct eth_addr* ethaddr;
    struct netif* netif;
    if (etharp_get_entry(i, &ipaddr, &netif, &ethaddr)) {
      if (ipaddr == NULL || ethaddr == NULL) continue;
      char macBuf[18];
      snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
               ethaddr->addr[0], ethaddr->addr[1], ethaddr->addr[2],
               ethaddr->addr[3], ethaddr->addr[4], ethaddr->addr[5]);
      KnownDevice d;
      d.mac = String(macBuf);
      d.ip = IPAddress(ipaddr->addr).toString();
      d.lastSeen = millis();
      currentScan.push_back(d);
    }
  }

  g_lanDeviceCount = currentScan.size();

  if (g_lanBaselineEstablished) {
    for (auto &dev : currentScan) {
      bool known = false;
      for (auto &k : g_knownDevices) {
        if (k.mac == dev.mac) { known = true; k.lastSeen = dev.lastSeen; break; }
      }
      if (!known) {
        g_newDeviceCount++;
        String msg = "📡 New device joined the network:\n";
        msg += "MAC: " + dev.mac + "\n";
        msg += "IP: " + dev.ip;
        raiseAlert(ALERT_NEW_DEVICE, msg, STATUS_WARNING);
        g_knownDevices.push_back(dev);
      }
    }
  } else {
    g_knownDevices = currentScan;
  }
}

// ================================================================
//  MANUAL "SCAN NOW"
// ================================================================
void runFullScanNow() {
  int devicesBefore = g_lanDeviceCount;
  int rogueBefore = g_rogueApCount;

  scanLanDevices();
  scanForRogueAP();
  checkGatewayMac();

  int newDevices = g_lanDeviceCount > devicesBefore ? (g_lanDeviceCount - devicesBefore) : 0;
  bool rogueFound = g_rogueApCount > rogueBefore;

  String summary = "Scan complete: " + String(g_lanDeviceCount) + " devices on LAN";
  if (newDevices > 0) summary += ", " + String(newDevices) + " new";
  if (rogueFound) summary += ", ⚠️ rogue AP detected";
  g_lastManualScanSummary = summary;
  addLogEntry("SCAN", summary);
}

// ================================================================
//  BLUETOOTH LE THREAT DETECTION (only runs when g_bleEnabled is on)
// ================================================================
// Runs as short bursts (BLE_SCAN_DURATION_SEC, default 6s) every
// BLE_SCAN_INTERVAL_MS (default 30s) rather than continuously, since
// the ESP32-C3 has one radio shared between Wi-Fi and Bluetooth —
// continuous BLE scanning would compete with deauth detection for
// radio time. A short burst every 30s gives solid BLE coverage
// while keeping Wi-Fi detection fully reliable in between.
//
// Detects:
//  1. BLE advertisement flood ("BLE spam") — an abnormal number of
//     BLE adverts in one short burst, the same signal produced by
//     fake-pairing-popup spam attacks (regardless of which gadget
//     sends them — there's no way to fingerprint a specific device).
//  2. New/unknown BLE device nearby. NOTE: many phones/earbuds use
//     BLE address randomization for privacy, so you'll see some
//     natural false positives from your OWN devices — that's normal
//     BLE behavior, not a bug.
void processBleAdvertisement(const NimBLEAdvertisedDevice* device) {
  g_bleAdvertCountThisBurst++;

  String addr = device->getAddress().toString().c_str();
  String name = device->haveName() ? String(device->getName().c_str()) : "";
  int rssi = device->getRSSI(); // NimBLE 2.x: RSSI is always available, no haveRSSI() check needed

  for (auto &b : g_knownBleDevices) {
    if (b.addr == addr) {
      b.lastSeen = millis();
      b.rssi = rssi;
      if (name.length() > 0) b.name = name;
      return;
    }
  }

  BleDevice nd;
  nd.addr = addr;
  nd.name = name;
  nd.rssi = rssi;
  nd.lastSeen = millis();

  if (g_bleBaselineEstablished) {
    if (g_knownBleDevices.size() >= BLE_MAX_TRACKED_DEVICES) {
      g_knownBleDevices.erase(g_knownBleDevices.begin());
    }
    g_knownBleDevices.push_back(nd);

    String msg = "📶 New Bluetooth device nearby:\n";
    msg += (name.length() > 0 ? name : "(unnamed)") + "\n";
    msg += addr + " · " + String(rssi) + "dBm";
    raiseAlert(ALERT_BLE_NEW_DEVICE, msg, STATUS_WARNING);
  } else {
    if (g_knownBleDevices.size() < BLE_MAX_TRACKED_DEVICES) {
      g_knownBleDevices.push_back(nd);
    }
  }
}

void EggBleCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
  processBleAdvertisement(advertisedDevice);
}

void setupBLE() {
  if (g_bleInitialized) return;
  NimBLEDevice::init("");
  g_bleScan = NimBLEDevice::getScan();
  g_bleCallbacks = new EggBleCallbacks();
  g_bleScan->setScanCallbacks(g_bleCallbacks, true); // true = report                                                   
  g_bleScan->setActiveScan(true);
  g_bleScan->setInterval(100);
  g_bleScan->setWindow(99);
  g_bleInitialized = true;
  g_lastBleScan = millis(); // first burst fires after one full interval
  Serial.println("BLE scanner initialized (NimBLE).");
}

void teardownBLE() {
  if (!g_bleInitialized) return;
  if (g_bleScan) g_bleScan->stop();
  NimBLEDevice::deinit(true);
  g_bleScan = nullptr;
  g_bleInitialized = false;
  g_bleBaselineEstablished = false;
  g_knownBleDevices.clear();
  Serial.println("BLE scanner stopped, radio freed for Wi-Fi.");
}

void startBleScanBurst() {
  if (g_bleScan == nullptr) return;
  g_bleScanInProgress = true;
  g_bleAdvertCountThisBurst = 0;
  g_bleScan->getResults(BLE_SCAN_DURATION_SEC * 1000, false);
  g_bleScan->stop();

  if (g_bleAdvertCountThisBurst >= BLE_SPAM_THRESHOLD) {
    g_bleSpamEventCount++;
    String msg = "🚨 Bluetooth advertisement flood detected!\n";
    msg += String(g_bleAdvertCountThisBurst) + " BLE adverts in " + String(BLE_SCAN_DURATION_SEC) + "s.\n";
    msg += "Possible BLE spam attack (fake pairing popups) nearby.";
    raiseAlert(ALERT_BLE_SPAM, msg, STATUS_ALERT);
  }

  g_bleBaselineEstablished = true;
  g_bleScan->clearResults();
  g_bleScanInProgress = false;
}

// ================================================================
//  ALERT ENGINE
// ================================================================
void raiseAlert(AlertType type, String message, SystemStatus severity) {
  unsigned long now = millis();
  if (now - g_lastAlertTime[type] < ALERT_COOLDOWN_MS && type != ALERT_BOOT) {
    return;
  }
  g_lastAlertTime[type] = now;

  Serial.println("[ALERT] " + message);
  addLogEntry(severity == STATUS_ALERT ? "ALERT" : (type == ALERT_BLE_NEW_DEVICE || type == ALERT_BLE_SPAM ? "BLE" : "INFO"), message);

  g_currentStatus = severity;
  int nl = message.indexOf('\n');
  g_statusMessage = (nl > 0) ? message.substring(0, nl) : message;
  g_statusMessageUntil = now + 15000;

  sendTelegramAlert(message);
}

void addLogEntry(String type, String message) {
  LogEntry e;
  e.timestamp = millis();
  e.type = type;
  e.message = message;
  g_eventLog.insert(g_eventLog.begin(), e);
  if (g_eventLog.size() > MAX_LOG_ENTRIES) {
    g_eventLog.pop_back();
  }
}

// ================================================================
//  TELEGRAM
// ================================================================
void sendTelegramAlert(String message) {
  if (!g_telegramEnabled) return;
  if (g_mode != MODE_MONITOR || WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connect failed");
    return;
  }

  String encoded = "";
  for (size_t i = 0; i < message.length(); i++) {
    char c = message.charAt(i);
    if (c == '\n') encoded += "%0A";
    else if (c == ' ') encoded += "%20";
    else if (c == '#') encoded += "%23";
    else if (c == '&') encoded += "%26";
    else if (c == '?') encoded += "%3F";
    else encoded += c;
  }

  String path = "/bot" + g_telegramToken + "/sendMessage?chat_id=" + g_telegramChatId + "&text=" + encoded;
  client.println("GET " + path + " HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Connection: close");
  client.println();

  unsigned long start = millis();
  while (client.connected() && millis() - start < 5000) {
    while (client.available()) client.read();
  }
  client.stop();
}

// ================================================================
//  SETUP-MODE WEB SERVER (captive portal — plain HTML form, no JS)
// ================================================================
void setupSetupModeServer() {
  server.on("/", HTTP_GET, handleSetupRoot);
  server.on("/save", HTTP_POST, handleSetupSave);
  server.on("/hotspot-detect.html", HTTP_GET, handleSetupRoot);
  server.on("/library/test/success.html", HTTP_GET, handleSetupRoot); // older iOS/macOS variant
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);
  server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/fwlink", HTTP_GET, handleCaptiveRedirect);

  server.onNotFound(handleCaptiveRedirect);

  server.begin();
}

void handleSetupRoot() {
  int n = WiFi.scanNetworks(false, true); // include hidden (won't have names)

  // De-dupe identical SSIDs (same network, multiple APs/bands) and
  // skip our own setup hotspot if it somehow shows up in the scan.
  String options = "";
  std::vector<String> seen;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;          // hidden network, nothing to show
    if (ssid == String(SETUP_AP_SSID)) continue;
    bool dupe = false;
    for (auto &s : seen) { if (s == ssid) { dupe = true; break; } }
    if (dupe) continue;
    seen.push_back(ssid);

    // Minimal HTML-escaping for option values — SSIDs can contain
    // characters like & or " that would otherwise break the markup.
    String escaped = ssid;
    escaped.replace("&", "&amp;");
    escaped.replace("\"", "&quot;");
    escaped.replace("<", "&lt;");

    options += "<option value=\"" + escaped + "\">";
  }
  WiFi.scanDelete();

  if (options.length() == 0) {
  }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(SETUP_HTML_TOP);
  server.sendContent(options);
  server.sendContent(SETUP_HTML_MID);
  server.sendContent("");
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleSetupSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String tgToken = server.arg("tgToken");
  String tgChat = server.arg("tgChat");

  if (ssid.length() == 0) {
    server.send(200, "text/html", SETUP_ERROR_HTML);
    return;
  }

  g_wifiSsid = ssid;
  g_wifiPass = pass;
  if (tgToken.length() > 0) {
    g_telegramToken = tgToken;
    g_telegramChatId = tgChat;
  }
  saveSettings();

  server.send(200, "text/html", SETUP_SAVED_HTML);
  server.client().flush();
  delay(1500);
  ESP.restart();
}

// ================================================================
//  MONITOR-MODE WEB SERVER (full dashboard)
// ================================================================
void checkSelfPortscan() {
  unsigned long now = millis();
  g_recentConnTimes.push_back(now);
  while (!g_recentConnTimes.empty() && now - g_recentConnTimes.front() > PORTSCAN_WINDOW_MS) {
    g_recentConnTimes.erase(g_recentConnTimes.begin());
  }
  if (g_recentConnTimes.size() >= PORTSCAN_THRESHOLD) {
    String msg = "🚨 Rapid connection attempts to Egg Sentinel's web server.\n";
    msg += String(g_recentConnTimes.size()) + " hits in " + String(PORTSCAN_WINDOW_MS/1000) + "s.\n";
    msg += "Possible scan of the device itself.";
    raiseAlert(ALERT_PORTSCAN, msg, STATUS_WARNING);
    g_recentConnTimes.clear();
  }
}

void setupMonitorModeServer() {
  server.on("/", HTTP_GET, handleDashboardRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/log", HTTP_GET, handleApiLog);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  server.on("/api/test-telegram", HTTP_POST, handleApiTestTelegram);
  server.on("/api/ack", HTTP_POST, handleApiAck);
  server.on("/api/scan-now", HTTP_POST, handleApiScanNow);
  server.on("/api/reset-wifi", HTTP_POST, handleApiResetWifi);
  server.on("/api/ble-toggle", HTTP_POST, handleApiBleToggle);
  server.begin();
}

void handleDashboardRoot() {
  checkSelfPortscan();
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleApiStatus() {
  checkSelfPortscan();
  DynamicJsonDocument doc(2048);
  doc["device"] = DEVICE_NAME;
  doc["uptime"] = formatUptime(millis() - g_bootTime);
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["status"] = (g_currentStatus == STATUS_OK) ? "ok" : (g_currentStatus == STATUS_WARNING ? "warning" : "alert");
  doc["statusMessage"] = g_statusMessage;
  doc["deauthLifetime"] = g_deauthTotalLifetime;
  doc["lanDevices"] = g_lanDeviceCount;
  doc["newDevicesSeen"] = g_newDeviceCount;
  doc["rogueApCount"] = g_rogueApCount;
  doc["gatewayMac"] = g_gatewayMac;
  doc["telegramEnabled"] = g_telegramEnabled;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["lastScanSummary"] = g_lastManualScanSummary;
  doc["bleEnabled"] = g_bleEnabled;
  doc["bleDevices"] = (int)g_knownBleDevices.size();
  doc["bleSpamCount"] = g_bleSpamEventCount;

  JsonArray devices = doc.createNestedArray("devices");
  for (auto &d : g_knownDevices) {
    JsonObject o = devices.createNestedObject();
    o["mac"] = d.mac;
    o["ip"] = d.ip;
  }

  JsonArray bleList = doc.createNestedArray("bleList");
  for (auto &b : g_knownBleDevices) {
    JsonObject o = bleList.createNestedObject();
    o["addr"] = b.addr;
    o["name"] = b.name;
    o["rssi"] = b.rssi;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiLog() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (auto &e : g_eventLog) {
    JsonObject o = arr.createNestedObject();
    o["t"] = e.timestamp;
    o["type"] = e.type;
    o["message"] = e.message;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiSettings() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err) {
      if (doc.containsKey("token")) g_telegramToken = doc["token"].as<String>();
      if (doc.containsKey("chatId")) g_telegramChatId = doc["chatId"].as<String>();
      saveSettings();
      server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  server.send(400, "application/json", "{\"ok\":false}");
}

void handleApiTestTelegram() {
  if (!g_telegramEnabled) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Telegram not configured\"}");
    return;
  }
  sendTelegramAlert("✅ Test alert from Egg Sentinel — your bot is working!");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiAck() {
  g_currentStatus = STATUS_OK;
  g_statusMessage = "All clear";
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiScanNow() {
  runFullScanNow();
  DynamicJsonDocument doc(256);
  doc["ok"] = true;
  doc["summary"] = g_lastManualScanSummary;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiResetWifi() {
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting into setup mode...\"}");
  g_wifiSsid = "";
  g_wifiPass = "";
  saveSettings();
  delay(1000);
  ESP.restart();
}

void handleApiBleToggle() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing body\"}");
    return;
  }
  DynamicJsonDocument doc(128);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc.containsKey("enabled")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }

  bool wantEnabled = doc["enabled"].as<bool>();
  if (wantEnabled == g_bleEnabled) {
    server.send(200, "application/json", "{\"ok\":true,\"unchanged\":true}");
    return;
  }

  g_bleEnabled = wantEnabled;
  saveSettings();

  if (g_bleEnabled) {
    setupBLE();
    addLogEntry("INFO", "Bluetooth detection turned ON from dashboard.");
  } else {
    teardownBLE();
    addLogEntry("INFO", "Bluetooth detection turned OFF from dashboard — Wi-Fi-only mode.");
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// ================================================================
//  OLED DISPLAY
// ================================================================
void drawBootScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(SCREEN_OFFSET_X + 6, SCREEN_OFFSET_Y + 8);
  display.print("EGG");
  display.setCursor(SCREEN_OFFSET_X + 2, SCREEN_OFFSET_Y + 20);
  display.print("SENTINEL");
  display.setCursor(SCREEN_OFFSET_X + 8, SCREEN_OFFSET_Y + 31);
  display.print("booting");
  display.display();
}

void drawSetupScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
  display.print("SETUP MODE");
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 12);
  display.print("Join WiFi:");
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 22);
  display.print(SETUP_AP_SSID);
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 33);
  display.print(WiFi.softAPIP());
  display.display();
}

void drawStatusIcon(int x, int y) {
  if (g_currentStatus == STATUS_OK) {
    display.drawCircle(x + 4, y + 4, 3, SSD1306_WHITE);
  } else if (g_currentStatus == STATUS_WARNING) {
    display.drawTriangle(x + 4, y, x, y + 8, x + 8, y + 8, SSD1306_WHITE);
  } else {
    display.fillRect(x, y, 8, 8, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(x + 2, y);
    display.print("!");
    display.setTextColor(SSD1306_WHITE);
  }
}

void drawHomePage() {
  display.setTextSize(1);
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi DOWN");
  drawStatusIcon(SCREEN_OFFSET_X + VISIBLE_W - 10, SCREEN_OFFSET_Y);

  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 12);
  if (g_currentStatus == STATUS_OK) {
    display.print("All clear");
  } else {
    String m = g_statusMessage;
    if (m.length() > 14) {
      display.print(m.substring(0, 14));
      display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 22);
      display.print(m.substring(14, 28));
    } else {
      display.print(m);
    }
  }

  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 32);
  display.print(formatUptime(millis() - g_bootTime));
}

void drawStatsPage() {
  display.setTextSize(1);
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
  display.print("Deauth:" + String(g_deauthTotalLifetime));
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 11);
  if (g_bleEnabled) {
    display.print("LAN:" + String(g_lanDeviceCount) + " BLE:" + String(g_knownBleDevices.size()));
  } else {
    display.print("LAN dev:" + String(g_lanDeviceCount));
  }
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 22);
  if (g_bleEnabled) {
    display.print("Rogue:" + String(g_rogueApCount) + " Spam:" + String(g_bleSpamEventCount));
  } else {
    display.print("Rogue AP:" + String(g_rogueApCount));
  }
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 33);
  display.print("RSSI:" + String(WiFi.RSSI()));
}

void drawNetworkPage() {
  display.setTextSize(1);

  // Line 1: IP address
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
  display.print(WiFi.localIP().toString());

  // Line 2: Status label
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 12);
  display.print("Status:");

  // Line 3: Status message
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 22);
  display.print(g_statusMessage);

  // Line 4: Device name
  display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y + 33);
  display.print(DEVICE_NAME);
  display.print(".local");
}

// ----------------------------------------------------------------
//  MASCOT — "Sentinel", a scrappy pitbull guard dog. Pure vector
//  draw calls, costs ~0 extra RAM/flash beyond the code itself.
// ----------------------------------------------------------------
void drawDogHead(int cx, int cy, bool earsBack, bool mouthOpen, bool angryBrow) {
  if (earsBack) {
    display.fillTriangle(cx - 12, cy - 6, cx - 6, cy - 10, cx - 4, cy - 2, SSD1306_WHITE);
    display.fillTriangle(cx + 12, cy - 6, cx + 6, cy - 10, cx + 4, cy - 2, SSD1306_WHITE);
  } else {
    display.fillTriangle(cx - 11, cy - 14, cx - 5, cy - 7, cx - 9, cy - 2, SSD1306_WHITE);
    display.fillTriangle(cx + 11, cy - 14, cx + 5, cy - 7, cx + 9, cy - 2, SSD1306_WHITE);
  }

  display.fillRoundRect(cx - 11, cy - 8, 22, 16, 5, SSD1306_WHITE);
  display.fillRoundRect(cx - 6, cy, 12, 7, 3, SSD1306_WHITE);

  if (angryBrow) {
    display.drawLine(cx - 7, cy - 4, cx - 3, cy - 2, SSD1306_BLACK);
    display.drawLine(cx + 7, cy - 4, cx + 3, cy - 2, SSD1306_BLACK);
  } else {
    display.fillCircle(cx - 5, cy - 2, 1, SSD1306_BLACK);
    display.fillCircle(cx + 5, cy - 2, 1, SSD1306_BLACK);
  }

  display.fillRoundRect(cx - 2, cy + 1, 4, 3, 1, SSD1306_BLACK);

  if (mouthOpen) {
    display.fillTriangle(cx - 4, cy + 5, cx + 4, cy + 5, cx, cy + 9, SSD1306_BLACK);
    display.drawLine(cx - 4, cy + 5, cx - 2, cy + 4, SSD1306_BLACK);
    display.drawLine(cx + 4, cy + 5, cx + 2, cy + 4, SSD1306_BLACK);
  } else {
    display.drawLine(cx - 3, cy + 5, cx + 3, cy + 5, SSD1306_BLACK);
  }
}

void drawDogBody(int cx, int cy) {
  display.fillRoundRect(cx - 9, cy, 18, 10, 4, SSD1306_WHITE);
  display.fillRect(cx - 8, cy + 8, 4, 5, SSD1306_WHITE);
  display.fillRect(cx + 4, cy + 8, 4, 5, SSD1306_WHITE);
}

void drawDogIdle(int cx, int cy) {
  drawDogBody(cx, cy + 6);
  drawDogHead(cx, cy - 4, false, false, false);
  display.fillCircle(cx + 11, cy + 10, 2, SSD1306_WHITE);
}

void drawDogAlert(int cx, int cy) {
  drawDogBody(cx, cy + 6);
  drawDogHead(cx, cy - 4, true, true, true);
  display.drawLine(cx - 4, cy + 1, cx - 4, cy - 2, SSD1306_WHITE);
  display.drawLine(cx, cy + 1, cx, cy - 2, SSD1306_WHITE);
  display.drawLine(cx + 4, cy + 1, cx + 4, cy - 2, SSD1306_WHITE);
}

void drawDogHappy(int cx, int cy) {
  drawDogBody(cx, cy + 6);
  drawDogHead(cx, cy - 4, false, false, false);
  display.fillTriangle(cx - 2, cy + 3, cx + 2, cy + 3, cx, cy + 7, SSD1306_WHITE);
  display.fillCircle(cx + 11, cy + 7, 2, SSD1306_WHITE);
}

void drawMascotPage() {
  int cx = SCREEN_OFFSET_X + VISIBLE_W / 2;
  int cy = SCREEN_OFFSET_Y + VISIBLE_H / 2 + 2;

  bool justAcked = (g_currentStatus == STATUS_OK && millis() - g_statusMessageUntil < 4000 && g_statusMessageUntil > 0);

  if (g_currentStatus != STATUS_OK) {
    drawDogAlert(cx, cy);
    display.setTextSize(1);
    display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
    display.print("ON GUARD");
  } else if (justAcked) {
    drawDogHappy(cx, cy);
    display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
    display.print("Good egg!");
  } else {
    drawDogIdle(cx, cy);
    display.setCursor(SCREEN_OFFSET_X, SCREEN_OFFSET_Y);
    display.print("Sentinel");
  }
}

// ----------------------------------------------------------------
//  Main display dispatcher — flashes (inverts) during active alerts
// ----------------------------------------------------------------
void drawDisplay() {
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 200) return;
  lastDraw = millis();

  if (g_mode == MODE_SETUP) {
    drawSetupScreen();
    return;
  }

  bool activeAlert = (g_currentStatus != STATUS_OK);

  if (!activeAlert && millis() - g_lastPageFlip > PAGE_FLIP_MS) {
    g_lastPageFlip = millis();
    g_displayPage = (g_displayPage + 1) % NUM_PAGES;
  }

  display.clearDisplay();
  if (activeAlert) {
    static unsigned long lastAlertPageSwap = 0;
    static bool showMascotSide = false;
    if (millis() - lastAlertPageSwap > 1500) {
      lastAlertPageSwap = millis();
      showMascotSide = !showMascotSide;
    }
    if (showMascotSide) drawMascotPage(); else drawHomePage();
  } else {
    switch (g_displayPage) {
      case 0: drawHomePage(); break;
      case 1: drawStatsPage(); break;
      case 2: drawNetworkPage(); break;
      case 3: drawMascotPage(); break;
    }
  }

  // Flash (invert) the whole screen while alerting — a single
  // hardware command, no extra RAM, very hard to miss.
  if (activeAlert) {
    bool flashOn = (millis() / 500) % 2 == 0;
    display.invertDisplay(flashOn);
  } else {
    display.invertDisplay(false);
  }

  display.display();
}

// ================================================================
//  SMALL HELPERS
// ================================================================
String formatUptime(unsigned long ms) {
  unsigned long s = ms / 1000;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;    s %= 60;
  String out = "";
  if (d > 0) out += String(d) + "d ";
  out += String(h) + "h" + String(m) + "m";
  return out;
}
