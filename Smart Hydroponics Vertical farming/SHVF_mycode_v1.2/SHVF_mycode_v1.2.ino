// ===================== ESP32 ==========================
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <math.h>
#include <esp_system.h>  // at the top of the file



#define EEPROM_TOTAL_SIZE 512
#define EEPROM_SIZE EEPROM_TOTAL_SIZE
// #define EEPROM_ADDR_K 0  // where K will be saved
#define EEPROM_ADDR_SSID 4    // Start address for SSID
#define EEPROM_ADDR_PASS 36   // Start address for Password
#define EEPROM_ADDR_AUTOMATION 68
#define EEPROM_ADDR_RELAYS 69
#define EEPROM_ADDR_PLANT 70
//#define EEPROM_ADDR_MODEL  120   // reserve ~48 bytes here 

// --- Model storage (exportable/importable) ---
#define EEPROM_ADDR_MAGIC   112
#define EEPROM_ADDR_MODEL   116   // leave space before this for your other data
#define MODEL_MAGIC 0x5A17BEEF

// Configures
#define DEVICE_ID "SHVF-V1" // unique id of the system 
#define DEVICE_CODE "12345" // Product key
const char* serverUrl = "https://api.smarthydrofarm.com/webhook.php";
String storedSSID = "ComTech"; // wifi name | confidential, DO NOT INCLUDE THIS ON USER MANUAL(the ssid)
String storedPASS = "Fcltyhgny2025"; // wifi password | confidential, DO NOT INCLUDE THIS ON USER MANUAL(the password)

// If your relays are active-LOW, set this to true (only used in the fallback mapping)
static const bool SHVF_RELAY_ACTIVE_LOW = true;

// revision memory (survives reboot)
static Preferences SHVF_PREFS;
static int SHVF_lastAppliedRev = 0;
static int SHVF_lastDoseReq    = 0;

// polling cadence (ms)
static unsigned long SHVF_lastPollMs = 0;
static const unsigned long SHVF_POLL_MS = 5000;


// === PINS ==
#define RXD1 18
#define TXD1 17
#define BUTTON_PIN 10
#define I2C_SDA 8 //21
#define I2C_SCL 9 //17

#define RELAY_NUTRIENT_A 1
#define RELAY_NUTRIENT_B 2
#define RELAY_PH_DOWN    42
#define RELAY_PH_UP      41
#define RELAY_WATER_PUMP 40
#define RELAY_GROWLIGHT  39
#define RELAY_SOLENOID   38
#define RELAY_MIXER      37

LiquidCrystal_I2C lcd(0x27, 20, 4);
HardwareSerial SerialUNO(1);

// Access Point config
const char* ap_ssid = "SmartHydroFarm";
const char* ap_password = "12345678";
IPAddress local_ip(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);

// === WiFi ===
WebServer server(80);
DNSServer dnsServer;

//Control state
bool RELAY_AUTOMATION = true;
//extern float msFromMl(float ml);
extern unsigned long msFromMl(float ml);


// === VARIABLES ==
float phValue = 0.0;
float waterTemp = 0.0;
float tds = 0.0;
float airTemp = 0.0;
float humidity = 0.0;
int hour = 0;
int minute = 0;
int second = 0;

// Drum / water-level values from UNO ultrasonic
float drumDistanceCm   = NAN;  // sensor -> water surface distance (cm)
float drumWaterDepthCm = NAN;  // computed water depth (cm)
float drumLiters       = NAN;  // estimated volume (L)


// --- From UNO calibration ---
String calStatusLine = "";   // last [CAL] message (for LCD)
float calC7 = NAN;           // last C7 value
float calC4 = NAN;           // last C4 value
bool calModeActive = false;  // true habang may calibration sequence


// === Plant Profile ===
// POD version safe for EEPROM (no String fields)
struct PlantConfigRaw {
  char  name[16];            // 15 chars + null
  int16_t ppm_min;
  int16_t ppm_max;
  float ph_target;
  float ph_min;
  float ph_max;
  uint8_t light_on_hour;
  uint8_t light_on_minute;
  uint8_t light_off_hour;
  uint8_t light_off_minute;
};


struct PlantConfig {
  String name;
  int ppm_min;
  int ppm_max;
  float ph_target;
  float ph_min;
  float ph_max;
  int light_on_hour;
  int light_on_minute;
  int light_off_hour;
  int light_off_minute;
};
PlantConfig lettuce = {"lettuce", 560, 840, 6.0, 5.5, 6.5, 6, 0, 20, 0};
PlantConfig petchay = {"Petchay", 560,840, 6.0, 5.5, 6.5, 6, 0, 20, 0 };
PlantConfig test = {"test", 560, 840, 7.0, 6.5, 7.5, 6, 0, 20, 0};
PlantConfig currentPlant = lettuce;


const unsigned long MIX_AFTER_PH_MS   = 3000;  // brief mix after pH dose
const unsigned long MIX_AFTER_TDS_MS  = 5000;  // longer mix after nutrient

const unsigned long SETTLE_MS         = 30000; // wait this long before learning from last dose
const unsigned long COOLDOWN_MS       = 20000; // min time between doses (faster than your 60s)

const unsigned long MIN_DOSE_MS       = 50;   // never “tap” shorter than this
const unsigned long MAX_DOSE_MS       = 8000;  // single shot clamp (prevent overshoot)

const float MAX_PH_STEP_PER_SHOT      = 0.5f;  // cap per-shot target delta
const float TARGET_PPM_FRACTION       = 0.5f;  // go to midpoint = min + fraction*(max-min)

// Small exploration early on to avoid getting stuck if starting guesses are off
const float EXPLORE_FRACTION = 0.15f; // up to ±15% randomization while updates < N
const uint32_t EXPLORE_UNTIL = 10;    // per-channel update count threshold

//======================

// === Timers and Data ===
int lcdPage = 0;
unsigned long lastButtonPress = 0;
unsigned long lastSendTime = 0;
unsigned long sendInterval = 10000;


// --- Add near the top, after globals ---
void parseUNOData(String input);
void sendSensorData();
// void intelligentAdjustments();
void controlGrowLight();
void handleWaterPump();
void handleSolenoid();
void handleMixer();
void handleLCD();
String htmlLoginPage();
String htmlDashboard();
String htmlWifiSettings();
void handleRoot();
void handleLogin();
void handleDashboard();
void handleSetMode();
void handleRelay();
void handleWifiPage();
void handleConnectWifi();
void handleDisconnect();
void handleReconnect();
void handleChangeWifi();
void handleDataJson();
void handleRelayControl();
void handleSetPlant();
void fetchRemoteCommands();
void saveSystemState();
void loadSystemState();
// void handleSerialCommands(); // fwd decl

// ============= ONLINE CONTROL CODE =============

// ============= ONLINE CONTROL CODE (with dosing) =============
//
// Features:
//  - Respects RELAY_AUTOMATION (automation=ON => cloud won't force water/grow/solenoid/mixer)
//  - Applies manual relay states (water/grow/solenoid/mixer) when automation=OFF
//  - Processes one-shot dosing commands (A/B/PH_UP/PH_DOWN) with ml→ms or direct ms
//  - Syncs plant profile + automation flag (no double control)
//  - Stores last applied 'rev' and last processed 'dose_req' in NVS (Preferences)
//  - ACKs actuals and dose acknowledgment to webhook.php
//
// Requirements from your sketch (already exist in your offline code):
//   - extern bool RELAY_AUTOMATION;
//   - extern float msFromMl(float ml);
//   - pin constants: RELAY_WATER_PUMP, RELAY_GROWLIGHT, RELAY_SOLENOID, RELAY_MIXER,
//                    RELAY_NUTRIENT_A, RELAY_NUTRIENT_B, RELAY_PH_UP, RELAY_PH_DOWN
//   - globals: currentPlant {name, ppm_min, ppm_max, ph_target, ph_min, ph_max,
//                            light_on_hour, light_on_minute, light_off_hour, light_off_minute}
//
//   - constants/vars: serverUrl (String), DEVICE_ID, DEVICE_CODE
//   - define SHVF_RELAY_ACTIVE_LOW true/false to match your board
/*
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ---- External items from your sketch ----
extern bool RELAY_AUTOMATION;
extern float msFromMl(float ml);

// If these are in another translation unit, uncomment externs for clarity:
// extern int RELAY_WATER_PUMP, RELAY_GROWLIGHT, RELAY_SOLENOID, RELAY_MIXER;
// extern int RELAY_NUTRIENT_A, RELAY_NUTRIENT_B, RELAY_PH_UP, RELAY_PH_DOWN;

extern String serverUrl;
extern const char* DEVICE_ID;
extern const char* DEVICE_CODE;

// ---- NVS (Preferences) for rev & dose_req ----
static Preferences SHVF_PREFS;
static int SHVF_lastAppliedRev = 0;
static int SHVF_lastDoseReq    = 0;

static unsigned long SHVF_lastPollMs = 0;
static const unsigned long SHVF_POLL_MS = 5000;

// ---- Relay helpers (active-low aware) ----
#ifndef SHVF_RELAY_ACTIVE_LOW
#define SHVF_RELAY_ACTIVE_LOW true  // set to false if your board is active-HIGH
#endif

*/

static inline int OUT_LEVEL(bool on) {
  return SHVF_RELAY_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
}

static inline void setWaterPump(bool on)    { digitalWrite(RELAY_WATER_PUMP, OUT_LEVEL(on)); }
static inline void setGrowLight(bool on)    { digitalWrite(RELAY_GROWLIGHT,  OUT_LEVEL(on)); }
static inline void setFillSolenoid(bool on) { digitalWrite(RELAY_SOLENOID,   OUT_LEVEL(on)); }
static inline void setMixerPump(bool on)    { digitalWrite(RELAY_MIXER,      OUT_LEVEL(on)); }

static inline bool isOnPin(int pin) {
  int v = digitalRead(pin);
  return SHVF_RELAY_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

// ---- HTTP helpers ----
static bool SHVF_httpGET(const String& url, DynamicJsonDocument &doc) {
  HTTPClient http;
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString(); http.end();
  return (deserializeJson(doc, payload) == DeserializationError::Ok);
}

static bool SHVF_httpPOST_JSON(const String& url, DynamicJsonDocument &body, DynamicJsonDocument &docOut) {
  HTTPClient http;
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type","application/json");
  String out; serializeJson(body, out);
  int code = http.POST(out);
  if (code != 200) { http.end(); return false; }
  String payload = http.getString(); http.end();
  return (deserializeJson(docOut, payload) == DeserializationError::Ok);
}

// ---- ACK states and dosing acknowledgment ----
static void SHVF_postAck(int rev, bool water, bool grow, bool solenoid, bool mixer, int doseAckReq /*0 if none*/) {
  DynamicJsonDocument body(320), out(256);
  body["device_id"] = DEVICE_ID;
  body["device_code"] = DEVICE_CODE;
  body["action"] = "ack";
  body["rev"] = rev;
  body["water_actual"]    = water    ? 1 : 0;
  body["grow_actual"]     = grow     ? 1 : 0;
  body["solenoid_actual"] = solenoid ? 1 : 0;
  body["mixer_actual"]    = mixer    ? 1 : 0;
  if (doseAckReq > 0) body["dose_ack"] = doseAckReq;   // server may ignore if not implemented
  (void)SHVF_httpPOST_JSON(String(serverUrl), body, out); // fire & forget
}

// ---- Apply relay intents in manual mode; automation gates control ----
static inline void SHVF_applyRelaysWithAutomation(bool autoFlag, bool water, bool grow, bool solenoid, bool mixer) {
  RELAY_AUTOMATION = autoFlag;
  if (RELAY_AUTOMATION) return;  // automation loop owns outputs
  setWaterPump(water);
  setGrowLight(grow);
  setFillSolenoid(solenoid);
  setMixerPump(mixer);
}

// ---- Execute a single dosing shot (blocking for ms duration) ----
static void SHVF_doDose(const String& channel, float ml, int ms, bool autoMixAfter) {
  // If ml provided and ms==0, convert via your calibrated function
  unsigned long dur = (ms > 0) ? (unsigned long)ms : (unsigned long)msFromMl(ml);
  if (dur == 0) return;

  int pin = -1;
  if (channel == "A")       pin = RELAY_NUTRIENT_A;
  else if (channel == "B")  pin = RELAY_NUTRIENT_B;
  else if (channel == "PH_UP")   pin = RELAY_PH_UP;
  else if (channel == "PH_DOWN") pin = RELAY_PH_DOWN;
  if (pin < 0) return;

  // Pulse the dosing relay
  digitalWrite(pin, OUT_LEVEL(true));
  delay(dur);
  digitalWrite(pin, OUT_LEVEL(false));

  // Optional brief mix after any manual dose (same as offline)
  if (autoMixAfter) {
    setMixerPump(true);
    delay(3000);
    setMixerPump(false);
  }
}

// ---- Poll, apply, dose, ACK ----
static void SHVF_pollOnce() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = String(serverUrl) + "?cmd=1&device_id=" + DEVICE_ID + "&device_code=" + DEVICE_CODE;
  DynamicJsonDocument doc(3072);
  if (!SHVF_httpGET(url, doc)) return;

  // ---- Revisioned relay/apply path ----
  const int  rev      = doc["rev"]    | 0;
  const bool autoFlag = doc["auto"]   | false;
  const bool water    = doc["water"]  | false;
  const bool grow     = doc["grow"]   | false;
  const bool solenoid = doc["solenoid"] | false;
  const bool mixer    = doc["mixer"]  | false;

  if (rev > 0 && rev != SHVF_lastAppliedRev) {
    SHVF_applyRelaysWithAutomation(autoFlag, water, grow, solenoid, mixer);
    SHVF_lastAppliedRev = rev;
    SHVF_PREFS.putInt("last_rev", SHVF_lastAppliedRev);

    // ACK current actuals (read pins even in automation mode)
    bool ackWater    = isOnPin(RELAY_WATER_PUMP);
    bool ackGrow     = isOnPin(RELAY_GROWLIGHT);
    bool ackSolenoid = isOnPin(RELAY_SOLENOID);
    bool ackMixer    = isOnPin(RELAY_MIXER);
    SHVF_postAck(rev, ackWater, ackGrow, ackSolenoid, ackMixer, 0);
  }

  // ---- Dosing command path (one-shot, deduped via dose_req) ----
  // Expected fields (optional): dose_req (int), dose_channel (string), dose_ml (float, optional), dose_ms (int, optional)
  // Device will execute once per unique dose_req.
  int dose_req = doc["dose_req"] | 0;
  if (dose_req > 0 && dose_req != SHVF_lastDoseReq) {
    String dose_channel = doc["dose_channel"] | "";
    float  dose_ml = doc.containsKey("dose_ml") ? (float)doc["dose_ml"] : 0.0f;
    int    dose_ms = doc.containsKey("dose_ms") ? (int)doc["dose_ms"] : 0;

    // Safety: ignore absurd values
    if (dose_channel.length() > 0 && (dose_ml >= 0.0f || dose_ms > 0)) {
      // Execute dosing shot (with post-mix)
      SHVF_doDose(dose_channel, dose_ml, dose_ms, /*autoMixAfter=*/true);

      // Remember we've processed this request
      SHVF_lastDoseReq = dose_req;
      SHVF_PREFS.putInt("last_dose_req", SHVF_lastDoseReq);

      // ACK dose with current actuals + dose_ack
      bool ackWater    = isOnPin(RELAY_WATER_PUMP);
      bool ackGrow     = isOnPin(RELAY_GROWLIGHT);
      bool ackSolenoid = isOnPin(RELAY_SOLENOID);
      bool ackMixer    = isOnPin(RELAY_MIXER);
      // Using latest rev we know; if rev==0 not changed, we still send ACK with rev we last applied
      int ackRev = (rev > 0 ? rev : SHVF_lastAppliedRev);
      SHVF_postAck(ackRev, ackWater, ackGrow, ackSolenoid, ackMixer, dose_req);
    }
  }

  // ---- Optional: plant profile sync (no pin forcing) ----
  if (doc.containsKey("plant")) {
    JsonObject p = doc["plant"];
    if (p.containsKey("name"))      currentPlant.name            = p["name"].as<String>();
    if (p.containsKey("ppm_min"))   currentPlant.ppm_min         = (int)p["ppm_min"];
    if (p.containsKey("ppm_max"))   currentPlant.ppm_max         = (int)p["ppm_max"];
    if (p.containsKey("ph_target")) currentPlant.ph_target       = (float)p["ph_target"];
    if (p.containsKey("ph_min"))    currentPlant.ph_min          = (float)p["ph_min"];
    if (p.containsKey("ph_max"))    currentPlant.ph_max          = (float)p["ph_max"];
    if (p.containsKey("on_h"))      currentPlant.light_on_hour   = (int)p["on_h"];
    if (p.containsKey("on_m"))      currentPlant.light_on_minute = (int)p["on_m"];
    if (p.containsKey("off_h"))     currentPlant.light_off_hour  = (int)p["off_h"];
    if (p.containsKey("off_m"))     currentPlant.light_off_minute= (int)p["off_m"];
  }
}

static inline void SHVF_pollLoop() {
  if (millis() - SHVF_lastPollMs >= SHVF_POLL_MS) {
    SHVF_lastPollMs = millis();
    SHVF_pollOnce();
  }
}

// ---- Call these in setup() once ----
//   SHVF_PREFS.begin("shvf", false);
//   SHVF_lastAppliedRev = SHVF_PREFS.getInt("last_rev", 0);
//   SHVF_lastDoseReq    = SHVF_PREFS.getInt("last_dose_req", 0);
//
// ---- And call SHVF_pollLoop() in loop() (non-blocking) ----




// ========================= 




// ===================
// ---- Logging buffer (for /logs) ----
String gLogBuf;
const size_t LOG_MAX = 4096;
void addLog(const String& s){
  gLogBuf += s; gLogBuf += "\n";
  if (gLogBuf.length() > LOG_MAX) {
    // keep last LOG_MAX chars
    gLogBuf.remove(0, gLogBuf.length() - LOG_MAX);
  }
}
// convenience
void logln(const String& s){ Serial.println(s); addLog(s); }

// Call addLog at key places you care about (already added below for UNO packet + dosing)

// ---- Manual dose: ms from ml (your formula) ----
unsigned long msFromMl(float ml){
  if (ml < 0) ml = 0;
  double ms = 595.0 * (double)ml + 150.0;
  if (ms < 0) ms = 0;
  return (unsigned long)ms;
}

// ===================


void fetchRemoteCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String cmdUrl = String(serverUrl) + "?device_id=" + DEVICE_ID + "&device_code=" + DEVICE_CODE + "&cmd=1";
  http.begin(cmdUrl);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      if (doc.containsKey("auto")) RELAY_AUTOMATION = (bool)doc["auto"];
      if (doc.containsKey("plant")) {
        JsonObject p = doc["plant"];
        if (p.containsKey("name"))      currentPlant.name            = p["name"].as<String>();
        if (p.containsKey("ppm_min"))   currentPlant.ppm_min         = (int)p["ppm_min"];
        if (p.containsKey("ppm_max"))   currentPlant.ppm_max         = (int)p["ppm_max"];
        if (p.containsKey("ph_target")) currentPlant.ph_target       = (float)p["ph_target"];
        if (p.containsKey("ph_min"))    currentPlant.ph_min          = (float)p["ph_min"];
        if (p.containsKey("ph_max"))    currentPlant.ph_max          = (float)p["ph_max"];
        if (p.containsKey("on_h"))      currentPlant.light_on_hour   = (int)p["on_h"];
        if (p.containsKey("on_m"))      currentPlant.light_on_minute = (int)p["on_m"];
        if (p.containsKey("off_h"))     currentPlant.light_off_hour  = (int)p["off_h"];
        if (p.containsKey("off_m"))     currentPlant.light_off_minute= (int)p["off_m"];
      }
    }
  }
  http.end();
}


void saveSystemState() {
  EEPROM.write(EEPROM_ADDR_AUTOMATION, RELAY_AUTOMATION ? 1 : 0);

  byte relays = 0;
  relays |= digitalRead(RELAY_WATER_PUMP) << 0;
  relays |= digitalRead(RELAY_GROWLIGHT) << 1;
  relays |= digitalRead(RELAY_SOLENOID) << 2;
  relays |= digitalRead(RELAY_MIXER) << 3;
  EEPROM.write(EEPROM_ADDR_RELAYS, relays);

  PlantConfigRaw raw{};
  strncpy(raw.name, currentPlant.name.c_str(), sizeof(raw.name)-1);
  raw.ppm_min = currentPlant.ppm_min;
  raw.ppm_max = currentPlant.ppm_max;
  raw.ph_target = currentPlant.ph_target;
  raw.ph_min = currentPlant.ph_min;
  raw.ph_max = currentPlant.ph_max;
  raw.light_on_hour = currentPlant.light_on_hour;
  raw.light_on_minute = currentPlant.light_on_minute;
  raw.light_off_hour = currentPlant.light_off_hour;
  raw.light_off_minute = currentPlant.light_off_minute;

  // Save currentPlant struct
  EEPROM.put(EEPROM_ADDR_PLANT, raw);
  EEPROM.commit();
}

void loadSystemState() {
  RELAY_AUTOMATION = EEPROM.read(EEPROM_ADDR_AUTOMATION);

  byte relays = EEPROM.read(EEPROM_ADDR_RELAYS);
  digitalWrite(RELAY_WATER_PUMP, (relays >> 0) & 0x01);
  digitalWrite(RELAY_GROWLIGHT,  (relays >> 1) & 0x01);
  digitalWrite(RELAY_SOLENOID,   (relays >> 2) & 0x01);
  digitalWrite(RELAY_MIXER,      (relays >> 3) & 0x01);

  PlantConfigRaw raw{};
  EEPROM.get(EEPROM_ADDR_PLANT, raw);

  if (raw.name[0] != '\0') { // simple validity check
    currentPlant.name = String(raw.name);
    currentPlant.ppm_min = raw.ppm_min;
    currentPlant.ppm_max = raw.ppm_max;
    currentPlant.ph_target = raw.ph_target;
    currentPlant.ph_min = raw.ph_min;
    currentPlant.ph_max = raw.ph_max;
    currentPlant.light_on_hour = raw.light_on_hour;
    currentPlant.light_on_minute = raw.light_on_minute;
    currentPlant.light_off_hour = raw.light_off_hour;
    currentPlant.light_off_minute = raw.light_off_minute;
  }
}

// ============== WEBSITE CODES ================

String htmlLoginPage() {
  return String(F(
  "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>"
  "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;background:#0f172a;color:#e2e8f0;display:flex;align-items:center;justify-content:center;height:100vh}"
  ".card{background:#111827;border:1px solid #1f2937;border-radius:16px;box-shadow:0 10px 30px rgba(0,0,0,.35);padding:28px;max-width:360px;width:92%}"
  "h1{font-size:22px;margin:0 0 18px;color:#93c5fd}"
  "input{width:100%;padding:12px 14px;border-radius:10px;border:1px solid #374151;background:#0b1220;color:#e5e7eb;margin:8px 0 14px;outline:none}"
  "button{width:100%;padding:12px 14px;border-radius:10px;border:0;background:#3b82f6;color:white;font-weight:600;cursor:pointer}"
  "button:hover{filter:brightness(1.05)}"
  "</style></head><body>"
  "<div class='card'>"
    "<h1>Smart HydroFarm</h1>"
    "<form action='/login' method='POST'>"
      "<input type='text' name='user' placeholder='Username' autocomplete='username'/>"
      "<input type='password' name='pass' placeholder='Password' autocomplete='current-password'/>"
      "<button type='submit'>Sign in</button>"
    "</form>"
  "</div></body></html>"
  ));
}

String htmlDashboard() {
  // Build plant options (existing presets)
  String plantOptions;
  auto addOpt = [&](const char* n){
    plantOptions += "<option value='"; plantOptions += n; plantOptions += "'";
    if (currentPlant.name == n) plantOptions += " selected";
    plantOptions += ">"; plantOptions += n; plantOptions += "</option>";
  };
  addOpt("lettuce");
  addOpt("Petchay");
  addOpt("test");

  // Read-only plant list
  String plantList =
    "<ul style='margin:8px 0 0 18px'>"
    "<li>lettuce &ndash; ppm 560&ndash;840, pH 6.0 (5.5&ndash;6.5)</li>"
    "<li>Petchay &ndash; ppm 560&ndash;840, pH 6.0 (5.5&ndash;6.5)</li>"
    "<li>test &ndash; ppm 560&ndash;840, pH 7.0 (6.5&ndash;7.5)</li>"
    "</ul>";

  String html;
  html.reserve(8000);

  html += "<!doctype html><html><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>"
          "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;background:#0b1220;color:#cbd5e1;margin:0;padding:20px}"
          "h2,h3{color:#93c5fd;margin:0 0 10px}"
          ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}"
          ".box{background:#0f172a;border:1px solid #1f2937;border-radius:16px;padding:16px;position:relative}"
          "button,input[type=submit]{background:#3b82f6;color:white;border:0;border-radius:10px;padding:10px 14px;cursor:pointer}"
          "button:hover,input[type=submit]:hover{filter:brightness(1.05)}"
          "input,select{width:100%;padding:10px 12px;border-radius:10px;border:1px solid #334155;background:#0b1220;color:#e5e7eb;margin:6px 0 10px}"
          ".row{display:flex;gap:10px;flex-wrap:wrap}"
          ".pill{display:inline-block;background:#1f2937;border:1px solid #334155;border-radius:999px;padding:6px 10px;margin:4px 6px 0 0;font-size:12px}"
          ".kbd{background:#111827;border:1px solid #374151;border-radius:6px;padding:2px 6px;font-family:ui-monospace,Menlo,Consolas,monospace}"
          ".logs{background:#030712;border:1px solid #1f2937;border-radius:12px;padding:12px;max-height:240px;overflow:auto;white-space:pre-wrap;font-family:ui-monospace,monospace;font-size:12px}"
          "a{color:#93c5fd;text-decoration:none}"
          ".btn{padding:10px 14px;border-radius:10px;border:0;margin:4px 6px}"
          ".btn.on{background:#16a34a;color:#fff}"
          ".btn.off{background:#dc2626;color:#fff}"
          ".logout{position:absolute;top:12px;right:16px;font-size:12px;background:#1f2937;padding:6px 10px;border-radius:999px}"
          "</style></head><body>";

  html += "<h2>Smart HydroFarm Dashboard</h2>";

  html += "<div class='grid'>";

  // Sensor data
  html += "<div class='box'><h3>Sensor Data</h3>";
  html += "<button class='logout' onclick='logout()'>Logout</button>";
  html += "<div id='data'>Loading...</div>";
  html += "<div style='margin-top:10px'><span class='pill'>STA: <span id='wstat'>checking...</span></span>";
  html += "<span class='pill'>IP: <span id='wip'>-</span></span></div>";
  html += "</div>";

  // Controls
  html += "<div class='box'><h3>Controls</h3>";

  // Automation toggle (no form, just button + fetch)
  html += "<div class='row'>"
          "<button id='btnAuto' type='button' onclick='toggleAutomation()'>Toggle Automation (";
  html += (RELAY_AUTOMATION ? "ON" : "OFF");
  html += ")</button></div>";

  // Manual relays (green/red), using fetch + no page reload
  html += "<div style='height:8px'></div><div class='row'>";

  auto btn = [&](const char* id, const char* label, const char* q, int pin) {
    bool on = (digitalRead(pin) == (SHVF_RELAY_ACTIVE_LOW ? LOW : HIGH));
    html += "<button id='";
    html += id;
    html += "' type='button' class='btn ";
    html += (on ? "on" : "off");
    html += "' onclick=\"toggleRelay('";
    html += q;
    html += "')\">";
    html += label;
    html += " (";
    html += (on ? "ON" : "OFF");
    html += ")</button>";
  };
  btn("btnWater",   "Water Pump", "water",     RELAY_WATER_PUMP);
  btn("btnGrow",    "Grow Light", "growlight", RELAY_GROWLIGHT);
  btn("btnSolenoid","Solenoid",   "solenoid",  RELAY_SOLENOID);
  btn("btnMixer",   "Mixer",      "mixer",     RELAY_MIXER);
  html += "</div>";


  // Manual dosing (fetch)
  html += "<div style='height:12px'></div>"
          "<h3>Manual Dosing (ml)</h3>"
          "<div class='row'>"
          "<input id='doseMl' type='number' step='0.1' min='0' placeholder='e.g., 1.5 ml' required>"
          "<select id='doseCh'>"
          "<option value='A'>Nutrient A</option>"
          "<option value='B'>Nutrient B</option>"
          "<option value='PH_UP'>pH Up</option>"
          "<option value='PH_DOWN'>pH Down</option>"
          "</select>"
          "<button type='button' onclick='doseManual()'>Dose</button>"
          "</div>"
          "<div style='font-size:12px;opacity:.8'>Manual action temporarily overrides ML for that one shot.</div>";
  html += "</div>"; // end Controls box

  // Plant config
  html += "<div class='box'>"
          "<h3>Plants / Crops</h3>"
          "<div><b>Current:</b> <span class='pill' id='curPlant'></span></div>"
          "<div style='height:8px'></div>"
          "<div class='row'>"
          "<select id='plantSelect' name='plant'>";
  html += plantOptions;
  html += "</select><button type='button' onclick='selectPlant()'>Load Preset</button></div>"
          "<div style='height:10px'></div>"
          "<div style='font-size:13px;opacity:.9'>Available presets:</div>";
  html += plantList;

  html += "<div style='height:12px'></div>"
          "<h3>Edit Current Plant</h3>"
          "<form action='/setPlant' method='POST'>"
          "<input name='name' placeholder='Plant Name' value=''>"
          "<div class='row'>"
          "<input name='ppm_min' type='number' placeholder='PPM Min'>"
          "<input name='ppm_max' type='number' placeholder='PPM Max'>"
          "</div>"
          "<div class='row'>"
          "<input name='ph_target' type='number' step='0.1' placeholder='pH Target'>"
          "<input name='ph_min' type='number' step='0.1' placeholder='pH Min'>"
          "<input name='ph_max' type='number' step='0.1' placeholder='pH Max'>"
          "</div>"
          "<div class='row'>"
          "<input name='light_on_hour' type='number' placeholder='On Hour (0-23)'>"
          "<input name='light_on_minute' type='number' placeholder='On Min (0-59)'>"
          "<input name='light_off_hour' type='number' placeholder='Off Hour (0-23)'>"
          "<input name='light_off_minute' type='number' placeholder='Off Min (0-59)'>"
          "</div>"
          "<input type='submit' value='Save Plant Settings'>"
          "</form>"
          "</div>";

  // Wi-Fi card (still normal forms; rarely used so ok even if reload)
  html += "<div class='box'>"
          "<h3>Wi-Fi</h3>"
          "<form action='/connectWifi' method='POST'>"
          "<input name='ssid' placeholder='SSID'>"
          "<input name='password' type='password' placeholder='Password'>"
          "<input type='submit' value='Connect / Save'>"
          "</form>"
          "<div class='row'>"
          "<form action='/reconnect' method='POST'><button>Reconnect</button></form>"
          "<form action='/disconnect' method='POST'><button>Disconnect</button></form>"
          "</div>"
          "<div style='font-size:12px;opacity:.8;margin-top:6px'>Status updates live above (STA/IP pills).</div>"
          "</div>";

  // Logs
  html += "<div class='box'>"
          "<h3>Serial Log</h3>"
          "<div id='logs' class='logs'>Loading...</div>"
          "<div style='margin-top:8px;font-size:12px'>Tip: press <span class='kbd'>CTRL</span> + <span class='kbd'>R</span> to refresh the page; logs auto-refresh anyway.</div>"
          "</div>";

  html += "</div>"; // grid

  // JS: session guard + fetch-based actions
  html += "<script>";
  // guard: if no login, go back to root
  html += "if(!localStorage.getItem('shvfLoggedIn')){window.location.href='/';}";
  html += "function logout(){localStorage.removeItem('shvfLoggedIn');window.location.href='/';}";
  html += "function setText(id,v){var e=document.getElementById(id); if(e) e.innerHTML=v;}";
    html += "function setRelayBtn(id,isOn,label){"
          "var b=document.getElementById(id);"
          "if(!b) return;"
          "b.classList.remove('on','off');"
          "b.classList.add(isOn?'on':'off');"
          "if(label){b.textContent=label+' ('+(isOn?'ON':'OFF')+')';}"
          "}";

    html += "function loadData(){"
          "fetch('/data').then(function(r){return r.json();}).then(function(d){"
            // sensor text

                  "setText('data',"
          "'<p><b>pH:</b> '+(d.ph===null?'-':d.ph)+'</p>' +"
          "'<p><b>Water Temp:</b> '+(d.water===null?'-':d.water)+' &deg;C</p>' +"
          "'<p><b>TDS:</b> '+(d.tds===null?'-':d.tds)+' ppm</p>' +"
          "'<p><b>Air Temp:</b> '+(d.air===null?'-':d.air)+' &deg;C</p>' +"
          "'<p><b>Humidity:</b> '+(d.hum===null?'-':d.hum)+' %</p>' +"
          "'<p><b>Drum Dist:</b> '+(d.drumD===null?'-':d.drumD+' cm')+'</p>' +"
          "'<p><b>Water Depth:</b> '+(d.drumDepth===null?'-':d.drumDepth+' cm')+'</p>' +"
          "'<p><b>Drum Volume:</b> '+(d.drumLiters===null?'-':d.drumLiters+' L')+'</p>'"
        ");"

          "setText('curPlant','";
  html += currentPlant.name;
  html += "');"

          // automation button label (in case nagbago from elsewhere)
          "var ba=document.getElementById('btnAuto');"
          "if(ba && typeof d.auto!=='undefined'){"
            "ba.textContent='Toggle Automation ('+(d.auto?'ON':'OFF')+')';"
          "}"

          // relay buttons (sync with live state; this runs sa lahat ng devices every 3s)
          "if(typeof d.water_on!=='undefined'){setRelayBtn('btnWater',d.water_on,'Water Pump');}"
          "if(typeof d.grow_on!=='undefined'){setRelayBtn('btnGrow',d.grow_on,'Grow Light');}"
          "if(typeof d.solenoid_on!=='undefined'){setRelayBtn('btnSolenoid',d.solenoid_on,'Solenoid');}"
          "if(typeof d.mixer_on!=='undefined'){setRelayBtn('btnMixer',d.mixer_on,'Mixer');}"

          "}).catch(function(err){ setText('data','Fetch failed: '+err); });}";

  html += "function loadWifi(){fetch('/wifiStatus').then(function(r){return r.json();}).then(function(s){"
          "setText('wstat', s.state); setText('wip', s.ip||'-');"
          "}).catch(function(){});}";

  html += "function loadLogs(){fetch('/logs').then(function(r){return r.text();}).then(function(t){"
          "var el=document.getElementById('logs'); if(!el) return;"
          "var atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 5;"
          "el.textContent = t; if(atBottom) el.scrollTop = el.scrollHeight;"
          "}).catch(function(){});}";

  // NEW: fetch actions
  html += "function toggleAutomation(){"
          "fetch('/setMode',{method:'POST'})"
          ".then(function(r){return r.json();})"
          ".then(function(j){var b=document.getElementById('btnAuto');"
          "if(b){b.textContent='Toggle Automation ('+(j.automation?'ON':'OFF')+')';}})"
          ".catch(function(e){console.log(e);});}";

  html += "function toggleRelay(which){"
          // id mapping
          "var map={water:'btnWater',growlight:'btnGrow',solenoid:'btnSolenoid',mixer:'btnMixer'};"
          "var labelMap={water:'Water Pump',growlight:'Grow Light',solenoid:'Solenoid',mixer:'Mixer'};"
          "var id=map[which];"
          "var b=document.getElementById(id);"
          "var wasOn=null;"
          "if(b){"
            "wasOn=b.classList.contains('on');"
            // optimistic toggle
            "setRelayBtn(id,!wasOn,labelMap[which]);"
          "}"
          "var fd=new FormData();fd.append('relay',which);"
          "fetch('/relayControl',{method:'POST',body:fd})"
            ".then(function(r){return r.json();})"
            ".then(function(j){"
              // optional: if later gusto mong magbalik ng exact states sa /relayControl,
              // pwede mong i-update dito gamit j.water_on, j.grow_on, etc.
              "loadData();"
              "loadLogs();"
            "})"
            ".catch(function(e){"
              "console.log(e);"
              // revert optimistic if failed
              "if(b && wasOn!==null){setRelayBtn(id,wasOn,labelMap[which]);}"
            "});"
          "}";


  html += "function doseManual(){"
          "var ml=document.getElementById('doseMl').value;"
          "var ch=document.getElementById('doseCh').value;"
          "var fd=new FormData();fd.append('ml',ml);fd.append('channel',ch);"
          "fetch('/doseManual',{method:'POST',body:fd})"
          ".then(function(){loadLogs();})"
          ".catch(function(e){console.log(e);});}";

  html += "function selectPlant(){"
          "var sel=document.getElementById('plantSelect');"
          "var fd=new FormData();fd.append('plant',sel.value);"
          "fetch('/selectPlant',{method:'POST',body:fd})"
          ".then(function(r){return r.json();})"
          ".then(function(j){setText('curPlant',j.name||sel.value);loadData();})"
          ".catch(function(e){console.log(e);});}";

  // intervals
  html += "setInterval(loadData,3000); loadData();";
  html += "setInterval(loadWifi,2000); loadWifi();";
  html += "setInterval(loadLogs,2000); loadLogs();";

  html += "</script>";

  html += "</body></html>";
  return html;
}


String htmlWifiSettings() {
  return "<html><body><h2>WiFi Settings</h2>"
         "<form action='/connectWifi' method='POST'>"
         "<input name='ssid' placeholder='SSID'><br>"
         "<input name='password' type='password' placeholder='Password'><br>"
         "<input type='submit' value='Connect'>"
         "</form>"
         "<form action='/disconnect' method='POST'><button>Disconnect</button></form>"
         "<form action='/reconnect' method='POST'><button>Reconnect</button></form>"
         "<form action='/change' method='POST'><button>Change WiFi</button></form>"
         "</body></html>";
}

void handleRoot() {
  server.send(200, "text/html", htmlLoginPage());
}

void handleLogin() {
  if (server.method() == HTTP_POST && server.arg("user") == "admin" && server.arg("pass") == "admin") {
    // Success: set localStorage in browser and redirect to /dashboard
    String page = F(
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body style='background:#0b1220;color:#cbd5e1;font-family:system-ui;padding:20px'>"
      "<h3>Login successful. Loading dashboard…</h3>"
      "<script>"
      "localStorage.setItem('shvfLoggedIn','1');"
      "window.location.href='/dashboard';"
      "</script>"
      "</body></html>"
    );
    server.send(200, "text/html", page);
  } else {
    server.send(401, "text/html", "Login Failed");
  }
}



void handleDashboard() {
  server.send(200, "text/html", htmlDashboard());
}

void handleSetMode() {
  RELAY_AUTOMATION = !RELAY_AUTOMATION;
  saveSystemState();

  String resp = String("{\"ok\":true,\"automation\":") + (RELAY_AUTOMATION ? "true" : "false") + "}";
  server.send(200, "application/json", resp);
}

void handleRelay() {
  String which = server.arg("relay");

  auto toggle = [](int pin){
    int cur = digitalRead(pin);
    digitalWrite(pin, !cur);
  };

  if (which == "water")           toggle(RELAY_WATER_PUMP);
  else if (which == "growlight")  toggle(RELAY_GROWLIGHT);
  else if (which == "solenoid")   toggle(RELAY_SOLENOID);
  else if (which == "mixer")      toggle(RELAY_MIXER);

  saveSystemState();

  auto relayOn = [&](int pin) -> bool {
    int cur = digitalRead(pin);
    return SHVF_RELAY_ACTIVE_LOW ? (cur == LOW) : (cur == HIGH);
  };

  String resp = "{";
  resp += "\"ok\":true,";
  resp += "\"water_on\":";   resp += relayOn(RELAY_WATER_PUMP) ? "true" : "false"; resp += ",";
  resp += "\"grow_on\":";    resp += relayOn(RELAY_GROWLIGHT)  ? "true" : "false"; resp += ",";
  resp += "\"solenoid_on\":";resp += relayOn(RELAY_SOLENOID)   ? "true" : "false"; resp += ",";
  resp += "\"mixer_on\":";   resp += relayOn(RELAY_MIXER)      ? "true" : "false";
  resp += "}";

  server.send(200, "application/json", resp);
}


void handleWifiPage() {
  server.send(200, "text/html", htmlWifiSettings());
}

void writeStringToEEPROM(int startAddr, const String& data) {
  for (int i = 0; i < data.length(); ++i) {
    EEPROM.write(startAddr + i, data[i]);
  }
  EEPROM.write(startAddr + data.length(), '\0'); // Null terminator
}

String readStringFromEEPROM(int startAddr) {
  char data[33];  // 32 chars max + null terminator
  int i = 0;
  while (i < 32) {
    char c = EEPROM.read(startAddr + i);
    if (c == '\0') break;
    data[i++] = c;
  }
  data[i] = '\0';
  return String(data);
}

void handleWifiStatus(){
  String s = "{";
  wl_status_t st = WiFi.status();
  String state = "DISCONNECTED";
  if (st == WL_CONNECTED) state = "CONNECTED";
  else if (st == WL_IDLE_STATUS) state = "IDLE";
  else if (st == WL_DISCONNECTED) state = "DISCONNECTED";
  else if (st == WL_NO_SSID_AVAIL) state = "NO_SSID";
  else if (st == WL_CONNECT_FAILED) state = "CONNECT_FAILED";
  else if (st == WL_SCAN_COMPLETED) state = "SCAN_DONE";

  s += "\"state\":\"" + state + "\"";
  s += ",\"ssid\":\"" + WiFi.SSID() + "\"";
  s += ",\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : String("")) + "\"";
  s += ",\"rssi\":"; s += (WiFi.isConnected()? String(WiFi.RSSI()) : "null");
  s += "}";
  server.send(200, "application/json", s);
}

void handleConnectWifi() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() > 31 || password.length() > 63) {
    server.send(400, "text/html", "SSID or Password too long!");
    return;
  }

  writeStringToEEPROM(EEPROM_ADDR_SSID, ssid);
  writeStringToEEPROM(EEPROM_ADDR_PASS, password);
  EEPROM.commit();

  WiFi.begin(ssid.c_str(), password.c_str());

  String page = F(
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:system-ui;background:#0b1220;color:#cbd5e1;padding:20px}"
    ".box{background:#0f172a;border:1px solid #1f2937;border-radius:16px;padding:16px;max-width:520px}"
    ".pill{display:inline-block;background:#1f2937;border:1px solid #334155;border-radius:999px;padding:6px 10px;margin:4px 6px 0 0;font-size:12px}"
    "a{color:#93c5fd;text-decoration:none}</style></head><body>"
    "<div class='box'><h3>Connecting…</h3>"
    "<div>Status: <span class='pill' id='wstat'>starting</span> <span class='pill'>IP: <span id='wip'>—</span></span></div>"
    "<div style='height:10px'></div>"
    "<div><a href='/dashboard'>&larr; Back to Dashboard</a></div>"
    "</div>"
    "<script>"
    "function set(id,v){var e=document.getElementById(id);if(e)e.innerHTML=v;}"
    "function poll(){fetch('/wifiStatus').then(r=>r.json()).then(s=>{"
      "set('wstat',s.state); set('wip',s.ip||'—');"
    "});}"
    "setInterval(poll,1000); poll();"
    "</script></body></html>"
  );
  server.send(200, "text/html", page);
}

void handleLogs(){
  server.send(200, "text/plain", gLogBuf);
}

void handleSelectPlant(){
  String p = server.arg("plant");
  if (p == "lettuce")        currentPlant = lettuce;
  else if (p == "Petchay")   currentPlant = petchay;
  else if (p == "test")      currentPlant = test;

  saveSystemState();

  String resp = "{\"ok\":true,\"name\":\"" + currentPlant.name + "\"}";
  server.send(200, "application/json", resp);
}


void handleDoseManual(){
  String ch = server.arg("channel");
  float ml = server.arg("ml").toFloat();
  if (ml < 0) ml = 0;

  unsigned long ms = msFromMl(ml);
  String msg = "Manual dose " + ch + " = " + String(ml, 2) + " ml (" + String(ms) + " ms)";
  logln(msg);

  if (ch == "A") {
    digitalWrite(RELAY_NUTRIENT_A, LOW); delay(ms); digitalWrite(RELAY_NUTRIENT_A, HIGH);
  } else if (ch == "B") {
    digitalWrite(RELAY_NUTRIENT_B, LOW); delay(ms); digitalWrite(RELAY_NUTRIENT_B, HIGH);
  } else if (ch == "PH_UP") {
    digitalWrite(RELAY_PH_UP, LOW); delay(ms); digitalWrite(RELAY_PH_UP, HIGH);
  } else if (ch == "PH_DOWN") {
    digitalWrite(RELAY_PH_DOWN, LOW); delay(ms); digitalWrite(RELAY_PH_DOWN, HIGH);
  }

  // Optional: brief mix after any manual dose
  digitalWrite(RELAY_MIXER, LOW); delay(3000); digitalWrite(RELAY_MIXER, HIGH);

  server.send(200, "application/json", "{\"ok\":true}");
}


void handleDisconnect() {
  WiFi.disconnect(false);
  server.send(200, "text/html", htmlWifiSettings());
}

void handleReconnect() {
  String ssid = readStringFromEEPROM(EEPROM_ADDR_SSID);
  String password = readStringFromEEPROM(EEPROM_ADDR_PASS);
  WiFi.begin(ssid.c_str(), password.c_str());
  server.send(200, "text/html", "Reconnecting...");
}

void handleChangeWifi() {
  server.send(200, "text/html", htmlWifiSettings());
}


void handleDataJson() {
  // Always emit valid JSON. Replace NaN/inf with null.

  auto relayOn = [&](int pin) -> bool {
    int cur = digitalRead(pin);
    return SHVF_RELAY_ACTIVE_LOW ? (cur == LOW) : (cur == HIGH);
  };

  String json = "{";

  auto addFloat = [&](const char* key, float value, uint8_t decimals, bool last) {
    json += "\""; json += key; json += "\":";
    if (isfinite(value)) {
      json += String(value, (unsigned int)decimals);
    } else {
      json += "null";
    }
    if (!last) json += ",";
  };

  auto addBool = [&](const char* key, bool v, bool last) {
    json += "\""; json += key; json += "\":";
    json += (v ? "true" : "false");
    if (!last) json += ",";
  };

  // --- sensor values ---
  addFloat("ph",         phValue,          2, false);
  addFloat("water",      waterTemp,        2, false);
  addFloat("tds",        tds,              0, false);
  addFloat("air",        airTemp,          1, false);
  addFloat("hum",        humidity,         1, false);
  addFloat("drumD",      drumDistanceCm,   1, false);  // distance sensor -> water
  addFloat("drumDepth",  drumWaterDepthCm, 1, false);  // water depth
  addFloat("drumLiters", drumLiters,       1, false);  // estimated liters

  // --- relay & automation state ---
  addBool("auto",        RELAY_AUTOMATION,            false);
  addBool("water_on",    relayOn(RELAY_WATER_PUMP),   false);
  addBool("grow_on",     relayOn(RELAY_GROWLIGHT),    false);
  addBool("solenoid_on", relayOn(RELAY_SOLENOID),     false);
  addBool("mixer_on",    relayOn(RELAY_MIXER),        true);

  json += "}";
  server.send(200, "application/json", json);
}


// =============================================

void setup() {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true); // optional, para sa saved creds

  SHVF_PREFS.begin("shvf", false);
  SHVF_lastAppliedRev = SHVF_PREFS.getInt("last_rev", 0);
  SHVF_lastDoseReq    = SHVF_PREFS.getInt("last_dose_req", 0);

  // --- Serial first for logs ---
  Serial.begin(115200);
  randomSeed(esp_random());

  // --- Non-WiFi init that doesn't depend on network ---
  EEPROM.begin(EEPROM_SIZE);


  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_NUTRIENT_A, OUTPUT);
  pinMode(RELAY_NUTRIENT_B, OUTPUT);
  pinMode(RELAY_PH_DOWN, OUTPUT);
  pinMode(RELAY_PH_UP, OUTPUT);
  pinMode(RELAY_WATER_PUMP, OUTPUT);
  pinMode(RELAY_GROWLIGHT, OUTPUT);
  pinMode(RELAY_SOLENOID, OUTPUT);
  pinMode(RELAY_MIXER, OUTPUT);

    // Safe defaults to avoid random boot states
  digitalWrite(RELAY_NUTRIENT_A, HIGH);
  digitalWrite(RELAY_NUTRIENT_B, HIGH);
  digitalWrite(RELAY_PH_DOWN, HIGH);
  digitalWrite(RELAY_PH_UP, HIGH);
  digitalWrite(RELAY_WATER_PUMP, HIGH);
  digitalWrite(RELAY_GROWLIGHT, HIGH);
  digitalWrite(RELAY_SOLENOID, HIGH);
  digitalWrite(RELAY_MIXER, HIGH);


  // Restore persisted states (automation + relays + plant)
  loadSystemState();

  // --- Always-on AP + Station mode ---
  WiFi.mode(WIFI_AP_STA);

  // Start AP (keeps offline dashboard reachable at 192.168.4.1 even if STA drops)
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password);
  dnsServer.start(53, "*", local_ip);
  Serial.println("AP running -> SSID: " + String(ap_ssid) + "  IP: " + local_ip.toString());

  // Simple STA event logging + auto-reconnect (AP is unaffected)
  WiFi.onEvent([](WiFiEvent_t event) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("STA connected: "); Serial.println(WiFi.localIP());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("STA disconnected. Reconnecting...");
        // WiFi.reconnect(); // do not touch AP
        break;
      default: break;
    }
  });

  // --- Load saved WiFi creds and connect STA (non-blocking) ---
  storedSSID = readStringFromEEPROM(EEPROM_ADDR_SSID);
  storedPASS = readStringFromEEPROM(EEPROM_ADDR_PASS);
  if (storedSSID.length() > 0) {
    Serial.print("Connecting STA to saved SSID: ");
    Serial.println(storedSSID);
    WiFi.begin(storedSSID.c_str(), storedPASS.c_str());
  } else {
    Serial.println("No saved STA credentials. AP only for now.");
  }

  // --- Serial for UNO after pins/Wire set up ---
  SerialUNO.begin(9600, SERIAL_8N1, RXD1, TXD1);

  // --- HTTP routes ---
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/dashboard", handleDashboard);
  server.on("/setMode", handleSetMode);
  server.on("/relay", handleRelay);
  server.on("/wifi", handleWifiPage);
  server.on("/connectWifi", handleConnectWifi);
  server.on("/disconnect", handleDisconnect);
  server.on("/reconnect", handleReconnect);
  server.on("/change", handleChangeWifi);
  server.on("/data", handleDataJson);
  server.on("/relayControl", handleRelayControl);
  server.on("/setPlant", handleSetPlant);
  server.on("/wifiStatus", HTTP_GET, handleWifiStatus);
server.on("/logs",       HTTP_GET, handleLogs);
server.on("/selectPlant",HTTP_POST, handleSelectPlant);
server.on("/doseManual", HTTP_POST, handleDoseManual);


  server.begin();
  Serial.println("Web server started");

}


void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  // handleSerialCommands();
  SHVF_pollLoop();   // non-blocking; checks server about every 5s

  if (SerialUNO.available()) {
      String incoming = SerialUNO.readStringUntil('\n');
      parseUNOData(incoming);
    }

    unsigned long now = millis();
    if (now - lastSendTime > sendInterval) {
      sendSensorData();
      fetchRemoteCommands();
      lastSendTime = now;
    }

    if (RELAY_AUTOMATION) {
    // intelligentAdjustments();
    controlGrowLight();
    handleWaterPump();
    handleSolenoid();
    handleMixer();
  }

    delay(300);
    handleLCD();
    

}

// === Parse UART String from UNO ===

void parseUNOData(String input) {
  input.trim();
  if (input.length() == 0) return;

  // --- 1) Normal sensor line: "ph:...,t:...,ppm:...,airT:...,hum:...,hour:...,minute:...,second:...,drumD:...,drumDepth:...,drumLiters:..." ---
  if (input.startsWith("ph:")) {

    // --- pH ---
    int p1 = input.indexOf("ph:");
    int p2 = input.indexOf(",", p1);
    String phStr = (p2 < 0) ? input.substring(p1 + 3) : input.substring(p1 + 3, p2);
    phValue = phStr.toFloat();

    // --- water temp t: (can be "nan") ---
    int t1 = input.indexOf("t:", p2);
    int t2 = input.indexOf(",", t1);
    String tStr = (t2 < 0) ? input.substring(t1 + 2) : input.substring(t1 + 2, t2);
    if (tStr == "nan" || tStr == "NaN") {
      waterTemp = NAN;
    } else {
      waterTemp = tStr.toFloat();
    }

    // --- ppm ---
    int s1 = input.indexOf("ppm:", t2);
    int s2 = input.indexOf(",", s1);
    String ppmStr = (s2 < 0) ? input.substring(s1 + 4) : input.substring(s1 + 4, s2);
    tds = ppmStr.toFloat();

    // --- air temp ---
    int a1 = input.indexOf("airT:", s2);
    int a2 = input.indexOf(",", a1);
    String airStr = (a2 < 0) ? input.substring(a1 + 5) : input.substring(a1 + 5, a2);
    airTemp = airStr.toFloat();

    // --- humidity ---
    int h1 = input.indexOf("hum:", a2);
    int h2 = input.indexOf(",", h1);
    String humStr = (h2 < 0) ? input.substring(h1 + 4) : input.substring(h1 + 4, h2);
    humidity = humStr.toFloat();

    // --- hour ---
    int hh1 = input.indexOf("hour:", h2);
    int hh2 = input.indexOf(",", hh1);
    String hourStr = (hh2 < 0) ? input.substring(hh1 + 5) : input.substring(hh1 + 5, hh2);
    hour = hourStr.toInt();

    // --- minute ---
    int mm1 = input.indexOf("minute:", hh2);
    int mm2 = input.indexOf(",", mm1);
    String minStr = (mm2 < 0) ? input.substring(mm1 + 7) : input.substring(mm1 + 7, mm2);
    minute = minStr.toInt();

    // --- second ---
    int ss1 = input.indexOf("second:", mm2);
    int ss2 = input.indexOf(",", ss1);  // might be -1 if last field before drums
    String secStr = (ss2 < 0) ? input.substring(ss1 + 7) : input.substring(ss1 + 7, ss2);
    second = secStr.toInt();

    // ---------- NEW: Drum ultrasonic values (optional) ----------

    // drumD (distance from sensor to water)
    int d1 = input.indexOf("drumD:", ss1);
    if (d1 >= 0) {
      int d2 = input.indexOf(",", d1);
      String dStr = (d2 < 0) ? input.substring(d1 + 6) : input.substring(d1 + 6, d2);
      if (dStr == "nan" || dStr == "NaN") {
        drumDistanceCm = NAN;
      } else {
        drumDistanceCm = dStr.toFloat();
      }
    }

    // drumDepth (water depth)
    int dd1 = input.indexOf("drumDepth:", ss1);
    if (dd1 >= 0) {
      int dd2 = input.indexOf(",", dd1);
      String ddStr = (dd2 < 0) ? input.substring(dd1 + 10) : input.substring(dd1 + 10, dd2);
      if (ddStr == "nan" || ddStr == "NaN") {
        drumWaterDepthCm = NAN;
      } else {
        drumWaterDepthCm = ddStr.toFloat();
      }
    }

    // drumLiters (estimated volume)
    int dl1 = input.indexOf("drumLiters:", ss1);
    if (dl1 >= 0) {
      int dl2 = input.indexOf(",", dl1);
      String dlStr = (dl2 < 0) ? input.substring(dl1 + 11) : input.substring(dl1 + 11, dl2);
      if (dlStr == "nan" || dlStr == "NaN") {
        drumLiters = NAN;
      } else {
        drumLiters = dlStr.toFloat();
      }
    }

    // Debug print (you can tweak format as you like)
    Serial.printf(
      "UNO -> PH: %.2f, Temp: %.2f, PPM: %.2f, airT: %.2f, hum: %.2f, time: %02d:%02d:%02d, "
      "drumD: %.1f, depth: %.1f, liters: %.1f\n",
      phValue, waterTemp, tds, airTemp, humidity,
      hour, minute, second,
      drumDistanceCm, drumWaterDepthCm, drumLiters
    );

    addLog(
      String("UNO -> pH: ") + String(phValue, 2) +
      ", ppm: " + String(tds, 0) +
      ", airT: " + String(airTemp, 1) +
      ", hum: " + String(humidity, 1) +
      ", drumL: " + String(drumLiters, 1)
    );
    return;
  }

  // --- 2) Bare constants line: "C7=502.44  C4=445.94" ---
  if (input.startsWith("C7=")) {
    calModeActive = true;

    int c7Pos = input.indexOf("C7=");
    int c4Pos = input.indexOf("C4=");

    if (c7Pos >= 0) {
      int end = input.indexOf(' ', c7Pos);
      if (end < 0) end = input.length();
      String c7Str = input.substring(c7Pos + 3, end);
      calC7 = c7Str.toFloat();
    }

    if (c4Pos >= 0) {
      int end = input.indexOf(' ', c4Pos);
      if (end < 0) end = input.length();
      String c4Str = input.substring(c4Pos + 3, end);
      calC4 = c4Str.toFloat();
    }

    calStatusLine = input;  // buong linya, pwede mong hatiin later sa LCD
    Serial.println("UNO CAL CONST: " + input);
    return;
  }

  // --- 3) CAL messages: "[CAL] ...." ---
  if (input.startsWith("[CAL]")) {
    calModeActive = true;

    // alisin lang yung "[CAL] " prefix para malinis sa LCD
    if (input.length() > 6) {
      calStatusLine = input.substring(6);  // from after "[CAL] "
    } else {
      calStatusLine = "";
    }

    Serial.println("UNO CAL MSG: " + calStatusLine);

    // If this is the final line, mark calibration as done
    if (input.indexOf("Calibration complete") >= 0) {
      calModeActive = false;
    }
    return;
  }

  // --- 4) Helper text: "Press D13 once to enter calibration mode ..." ---
  if (input.startsWith("Press D13")) {
    calModeActive = true;
    calStatusLine = input;  // full line
    Serial.println("UNO CAL HINT: " + calStatusLine);
    return;
  }

  // Anything else: ignore or log
  // Serial.println("UNO RAW: " + input);
}


// === HTTP Send ===
void sendSensorData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["device_id"] = DEVICE_ID;
    doc["device_code"] = DEVICE_CODE;
    doc["plant"] = currentPlant.name;
    doc["ppm"] = tds;
    doc["ph"] = phValue;
    doc["temp"] = airTemp;
    doc["humidity"] = humidity;
    doc["waterTemp"] = waterTemp;
    doc["hour"] =  hour;
    doc["minute"] = minute;
    doc["second"] = second;

       // New: water-level info (use null if NaN so JSON stays valid)
    if (isfinite(drumDistanceCm))   doc["drumD"]      = drumDistanceCm;   else doc["drumD"]      = nullptr;
    if (isfinite(drumWaterDepthCm)) doc["drumDepth"]  = drumWaterDepthCm; else doc["drumDepth"]  = nullptr;
    if (isfinite(drumLiters))       doc["drumLiters"] = drumLiters;       else doc["drumLiters"] = nullptr;


    String json;
    serializeJson(doc, json);
    int code = http.POST(json);
    Serial.print("Data sent. HTTP code: "); Serial.println(code);
    http.end();
  }
}


// ================== RELAY FUNCTIONS =================

// Manual Relay Handler
void handleRelayControl() {
  String relay = server.arg("relay");
  if (relay == "water") digitalWrite(RELAY_WATER_PUMP, !digitalRead(RELAY_WATER_PUMP));
  else if (relay == "growlight") digitalWrite(RELAY_GROWLIGHT, !digitalRead(RELAY_GROWLIGHT));
  else if (relay == "solenoid") digitalWrite(RELAY_SOLENOID, !digitalRead(RELAY_SOLENOID));
  else if (relay == "mixer") digitalWrite(RELAY_MIXER, !digitalRead(RELAY_MIXER));
  saveSystemState();
  server.send(200, "text/html", htmlDashboard());
}

void handleSolenoid() {
  //digitalWrite(RELAY_SOLENOID, HIGH);  // turn off pump
}
    
    
void handleMixer() {
  // do nothin, this is just a optional feature
  //digitalWrite(RELAY_MIXER, HIGH);  // turn off mixer
}

unsigned long lastPumpToggle = 0;
bool pumpOn = false;
int pumpOnDuration = 5;   // default in minutes 15
int pumpOffDuration = 10;  // default in minutes 30

void handleWaterPump() {
  // Adjust durations based on air temperature
  if (airTemp >= 30.0) {  // threshold for "hot"
    pumpOffDuration = 5; // 15 production level
  } else {
    pumpOffDuration = 10; // 30 production level
  }

  int currentTotalMinutes = hour * 60 + minute;
  static int lastChangeTotalMinutes = currentTotalMinutes;

  int elapsed = currentTotalMinutes - lastChangeTotalMinutes;
  if (elapsed < 0) elapsed += 1440; // handle midnight wrap-around

  if (pumpOn && elapsed >= pumpOnDuration) {
    digitalWrite(RELAY_WATER_PUMP, HIGH);  // turn off pump
    pumpOn = false;
    lastChangeTotalMinutes = currentTotalMinutes;
  } else if (!pumpOn && elapsed >= pumpOffDuration) {
    digitalWrite(RELAY_WATER_PUMP, LOW); // turn on pump
    pumpOn = true;
    lastChangeTotalMinutes = currentTotalMinutes;
  }
}
// ===============
// Set plant profile
void handleSetPlant() {
  currentPlant.name = server.arg("name");
    if (currentPlant.name.length() > 15) currentPlant.name.remove(15);
  currentPlant.ppm_min = server.arg("ppm_min").toInt();
  currentPlant.ppm_max = server.arg("ppm_max").toInt();
  currentPlant.ph_target = server.arg("ph_target").toFloat();
  currentPlant.ph_min = server.arg("ph_min").toFloat();
  currentPlant.ph_max = server.arg("ph_max").toFloat();
 
   // Clamp times
  int on_h  = server.arg("light_on_hour").toInt();
  int on_m  = server.arg("light_on_minute").toInt();
  int off_h = server.arg("light_off_hour").toInt();
  int off_m = server.arg("light_off_minute").toInt();

  currentPlant.light_on_hour     = constrain(on_h, 0, 23);
  currentPlant.light_on_minute   = constrain(on_m, 0, 59);
  currentPlant.light_off_hour    = constrain(off_h, 0, 23);
  currentPlant.light_off_minute  = constrain(off_m, 0, 59);

  saveSystemState();
  server.send(200, "text/html", htmlDashboard());
}

// === Growlight Control ===
void controlGrowLight() {

    int nowMinutes = hour * 60 + minute;
    int onMinutes = currentPlant.light_on_hour * 60 + currentPlant.light_on_minute; 
    int offMinutes = currentPlant.light_off_hour * 60 + currentPlant.light_off_minute; 

    if (nowMinutes >= onMinutes && nowMinutes < offMinutes) {
    digitalWrite(RELAY_GROWLIGHT, LOW); // ON
    } else {
    digitalWrite(RELAY_GROWLIGHT, HIGH); // OFF
    }

}

// ============================ END OF RELAY FUNCTION =====================

// === LCD Display ===


void handleLCD() {
  static int lastPage = -1;
  static unsigned long lastDraw = 0;
  const unsigned long refreshMs = 500; // redraw twice a second
  bool shouldRedraw = false;

  // For normal pages - last displayed values
  static float lastAirTemp = -999, lastHumidity = -999, lastPh = -999, lastTds = -999;
  static String lastPlantName = "";
  static float lastWaterTemp = -999;
  static int lastHour = -1, lastMinute = -1, lastSecond = -1;

  // For calibration page change detection
  static bool   lastCalModeActive = false;
  static String lastCalStatusLine = "";
  static float  lastCalC7 = NAN;
  static float  lastCalC4 = NAN;

  static float lastDrumLiters = NAN;
  static float lastDrumDepth  = NAN;
  static float lastDrumDist   = NAN;


  // --------- CALIBRATION TAKES OVER DISPLAY ---------
  // If calibration mode is active, ignore page button and just show calibration screen
  if (calModeActive) {
    // Only redraw if something changed or a bit of time has passed
    if (!lastCalModeActive ||
        calStatusLine != lastCalStatusLine ||
        calC7 != lastCalC7 ||
        calC4 != lastCalC4 ||
        millis() - lastDraw >= refreshMs) {

      lastDraw = millis();
      lastCalModeActive = true;
      lastCalStatusLine = calStatusLine;
      lastCalC7 = calC7;
      lastCalC4 = calC4;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("pH CALIBRATION");

      // Line 1–2: status text (split into 20-char chunks)
      String s = calStatusLine;
      while (s.startsWith(" ")) s.remove(0, 1); // trim leading spaces a bit

      // First line of status (row 1)
      lcd.setCursor(0, 1);
      if (s.length() > 0) {
        int len1 = (int)s.length();
        if (len1 > 20) len1 = 20;
        lcd.print(s.substring(0, len1));
      } else {
        lcd.print("Follow buffer steps");
      }

      // Second line of status (row 2)
      lcd.setCursor(0, 2);
      if (s.length() > 20) {
        int remaining = (int)s.length() - 20;
        if (remaining > 20) remaining = 20;
        lcd.print(s.substring(20, 20 + remaining));
      } else {
        // optional hint
        lcd.print("Use D13 button...");
      }

      // Line 3: show C7/C4 when available
      lcd.setCursor(0, 3);
      if (!isnan(calC7) || !isnan(calC4)) {
        lcd.print("C7:");
        if (!isnan(calC7)) lcd.print(calC7, 2);
        lcd.print(" C4:");
        if (!isnan(calC4)) lcd.print(calC4, 2);
      } else {
        lcd.print("Waiting for C7/C4");
      }
    }

    return; // IMPORTANT: do not draw normal pages while calibrating
  }

  // --------- CALIBRATION JUST FINISHED ---------
  if (!calModeActive && lastCalModeActive) {
    // calibration ended -> clear once and reset state so we go back to normal pages
    lastCalModeActive = false;
    lcd.clear();
    lastPage = -1;        // force page redraw layout
    lastDraw = millis();
  }

  // --------- NORMAL DISPLAY BELOW THIS LINE ---------

  // Page change debounced
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > 300) {
    lcdPage = (lcdPage + 1) % 3;
    lastButtonPress = millis();
    shouldRedraw = true; // force redraw on page change
  }

  // Periodic refresh
  if (millis() - lastDraw >= refreshMs) {
    shouldRedraw = true;
  }

  // Detect changes in displayed values
  if (lcdPage == 0) {
    if (airTemp != lastAirTemp || humidity != lastHumidity ||
        phValue != lastPh || tds != lastTds) {
      shouldRedraw = true;
    }
  } else if (lcdPage == 1) {
    if (currentPlant.name != lastPlantName || waterTemp != lastWaterTemp ||
        hour != lastHour || minute != lastMinute || second != lastSecond) {
      shouldRedraw = true;
    }
  } else if (lcdPage == 2) {
  if (drumLiters != lastDrumLiters ||
      drumWaterDepthCm != lastDrumDepth ||
      drumDistanceCm != lastDrumDist) {
    shouldRedraw = true;
  }
}


  // No redraw needed if nothing changed
  if (!shouldRedraw && lastPage == lcdPage) return;

  lastDraw = millis();
  if (lcdPage != lastPage) {
    lcd.clear(); // only clear when switching pages
    lastPage = lcdPage;
  }

  switch (lcdPage) {
    case 0:
      lcd.setCursor(0, 0); lcd.print("AirT: "); lcd.print(airTemp);    lcd.print("   ");
      lcd.setCursor(0, 1); lcd.print("Hum: ");  lcd.print(humidity);   lcd.print("   ");
      lcd.setCursor(0, 2); lcd.print("pH: ");   lcd.print(phValue, 2); lcd.print("   ");
      lcd.setCursor(0, 3); lcd.print("PPM: ");  lcd.print(tds, 0);     lcd.print("   ");

      // Update last displayed values
      lastAirTemp = airTemp;
      lastHumidity = humidity;
      lastPh = phValue;
      lastTds = tds;
      break;

    case 1: {
      lcd.setCursor(0, 0); lcd.print("Plant: "); lcd.print(currentPlant.name); lcd.print("      ");
      char timeStr[12];
      int displayHour = hour % 12; if (displayHour == 0) displayHour = 12;
      const char* ampm = (hour >= 12) ? "PM" : "AM";
      sprintf(timeStr, "%02d:%02d:%02d %s", displayHour, minute, second, ampm);
      lcd.setCursor(0, 1); lcd.print(timeStr); lcd.print("   ");
      lcd.setCursor(0, 2); lcd.print("WaterT: "); lcd.print(waterTemp); lcd.print("   ");
      lcd.setCursor(0, 3); lcd.print(local_ip);

      // Update last displayed values
      lastPlantName = currentPlant.name;
      lastWaterTemp = waterTemp;
      lastHour = hour;
      lastMinute = minute;
      lastSecond = second;
      break;
    }
        case 2: {
      lcd.setCursor(0, 0);
      lcd.print("Lvl: ");
      if (isfinite(drumLiters)) {
        lcd.print(drumLiters, 1);   // 1 decimal
        lcd.print(" L   ");
      } else {
        lcd.print("--.- L        ");
      }

      lcd.setCursor(0, 1);
      lcd.print("Depth: ");
      if (isfinite(drumWaterDepthCm)) {
        lcd.print(drumWaterDepthCm, 1);
        lcd.print(" cm   ");
      } else {
        lcd.print("--.- cm       ");
      }

      lcd.setCursor(0, 2);
      lcd.print("Dist: ");
      if (isfinite(drumDistanceCm)) {
        lcd.print(drumDistanceCm, 1);
        lcd.print(" cm   ");
      } else {
        lcd.print("--.- cm       ");
      }

      lcd.setCursor(0, 3);
      lcd.print("Drum Level Page   ");

      // update last values
      lastDrumLiters = drumLiters;
      lastDrumDepth  = drumWaterDepthCm;
      lastDrumDist   = drumDistanceCm;
      break;
    }

  }
}

