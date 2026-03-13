#include <DHT.h>
#include <RtcDS1302.h>
// #include <OneWire.h>
// #include <DallasTemperature.h>
#include <EEPROM.h>

// === Pins ===
#define DHTPIN 2
#define DHTTYPE DHT11
#define PH_SENSOR A0
#define TDS_SENSOR A1
#define WATER_TEMP A2        // DS18B20 data line on A2 (digital-capable)

// Calibration I/O
#define PIN_BUTTON 3          // Button -> GND (INPUT_PULLUP)
#define PIN_BUZZER 11         // Buzzer -> GND (active HIGH)

// === Ultrasonic drum level sensor ===
#define ULTRASONIC_TRIG 9     // AJ-SR04M RX Trig
#define ULTRASONIC_ECHO 10     // AJ-SR04M TX Echo

// === RTC wiring (DS1302) ===
ThreeWire myWire(5, 4, 6);    // DS1302: DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

DHT dht(DHTPIN, DHTTYPE);

// === EEPROM layout & defaults ===
const int EE_MAGIC_ADDR       = 0;    // 4 bytes
const int EE_C7_ADDR          = 4;    // 4 bytes float
const int EE_C4_ADDR          = 8;    // 4 bytes float
const int EE_DRUM_EMPTY_ADDR  = 12;   // 4 bytes float (drum empty distance in cm)

const uint32_t EE_MAGIC = 0x50484341; // 'PHCA' marker

// Your previous defaults (used if EEPROM not set yet)
const float C7_DEFAULT = 567.50;     // ADC at pH ~6.86/7.00 buffer
const float C4_DEFAULT = 640.00;     // ADC at pH ~4.01 buffer

// Drum defaults
// 33 inch ≈ 84 cm (approx empty distance if sensor is on lid)
const float DRUM_EMPTY_DEFAULT_CM = 84.0f;

// Approx drum capacity (from previous computation)
const float DRUM_CAPACITY_LITERS = 161.0f;  // predicted full volume (est.)

// Runtime calibration points (persisted in EEPROM)
float C7 = NAN;   // raw ADC at pH 7.00
float C4 = NAN;   // raw ADC at pH 4.01

// Drum empty distance (sensor->bottom) in cm, persisted in EEPROM
float drumEmptyDistanceCm = DRUM_EMPTY_DEFAULT_CM;

// === TDS/EC options ===
const float TDS_FACTOR = 0.7;   // 0.5 (NaCl) or 0.7 (442) — match your handheld
const float EC_TCOMP   = 0.02;  // temperature coefficient vs 25C

// ====== timing (non-blocking) ======
unsigned long nowMs;
unsigned long lastSenseMs = 0;
const unsigned long SENSE_PERIOD_MS = 1000;  // do sensor/print once per second

// ====== calibration timing (adjust these to control capture length) ======
const unsigned long CAL_SETTLE_MS = 20000;   // wait in buffer before averaging (20 s). Set 0 to skip.
const int CAL_SAMPLES = 600;                 // 600×5 ms = ~3.0 s averaging window
const int CAL_DELAY_MS = 5;                  // per-sample delay during calibration averaging

// --- helpers ---
float readStableAnalog(int pin, int samples = 60) {
  float sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2); // small to keep UI responsive; used for runtime sampling
  }
  return sum / samples;
}

// Convert raw TDS sensor voltage to EC (µS/cm), with temp compensation, then to PPM.
float readTDSppm(float waterTempC) {
  float tdsRaw = readStableAnalog(TDS_SENSOR, 20);
  float v = tdsRaw * (5.0f / 1023.0f);  // 10-bit ADC @5V
  float compCoeff = 1.0f + EC_TCOMP * (waterTempC - 25.0f);
  float vComp = v / compCoeff;
  float ec_uS = (133.42f * vComp * vComp * vComp
               - 255.86f * vComp * vComp
               + 857.39f * vComp);
  if (ec_uS < 0) ec_uS = 0;
  float ppm = ec_uS * TDS_FACTOR;
  return ppm;
}

// ====== EEPROM helpers ======
void eepromWriteFloat(int addr, float v){ EEPROM.put(addr, v); }
float eepromReadFloat(int addr){ float v; EEPROM.get(addr, v); return v; }

void loadCalibrationFromEEPROM(){
  uint32_t magic = 0;
  EEPROM.get(EE_MAGIC_ADDR, magic);
  if (magic == EE_MAGIC) {
    C7 = eepromReadFloat(EE_C7_ADDR);
    C4 = eepromReadFloat(EE_C4_ADDR);
    drumEmptyDistanceCm = eepromReadFloat(EE_DRUM_EMPTY_ADDR);

    if (isnan(C7) || C7 < 0 || C7 > 1023) C7 = C7_DEFAULT;
    if (isnan(C4) || C4 < 0 || C4 > 1023) C4 = C4_DEFAULT;
    // validate drum empty distance (reasonable bounds: 10cm..300cm)
    if (isnan(drumEmptyDistanceCm) || drumEmptyDistanceCm < 10.0f || drumEmptyDistanceCm > 300.0f) {
      drumEmptyDistanceCm = DRUM_EMPTY_DEFAULT_CM;
    }
  } else {
    EEPROM.put(EE_MAGIC_ADDR, EE_MAGIC);
    C7 = C7_DEFAULT;
    C4 = C4_DEFAULT;
    drumEmptyDistanceCm = DRUM_EMPTY_DEFAULT_CM;
    eepromWriteFloat(EE_C7_ADDR, C7);
    eepromWriteFloat(EE_C4_ADDR, C4);
    eepromWriteFloat(EE_DRUM_EMPTY_ADDR, drumEmptyDistanceCm);
  }
}

void saveC7(float v){ C7 = v; eepromWriteFloat(EE_C7_ADDR, C7); }
void saveC4(float v){ C4 = v; eepromWriteFloat(EE_C4_ADDR, C4); }

// Save drum empty distance to EEPROM
void saveDrumEmpty(float v){
  drumEmptyDistanceCm = v;
  eepromWriteFloat(EE_DRUM_EMPTY_ADDR, drumEmptyDistanceCm);
}

// ====== Buzzer helpers (blocking but short) ======
void beepOnce(int onMs, int offMs){
  digitalWrite(PIN_BUZZER, HIGH);
  delay(onMs);
  digitalWrite(PIN_BUZZER, LOW);
  delay(offMs);
}
void beepFast4(){ for(int i=0;i<4;i++) beepOnce(60,60); }
void beepNormal2(){ for(int i=0;i<2;i++) beepOnce(160,120); }
void beepNormal3(){ for(int i=0;i<3;i++) beepOnce(160,120); }

// ====== Button (D13 to GND) with debounce ======
bool btnLast = true; // INPUT_PULLUP idle HIGH
unsigned long btnLastChange = 0;
const unsigned long DEBOUNCE_MS = 30;
unsigned long pressStart = 0;
bool pressed = false;

bool buttonPressed(){
  return false; // NOTE: dismissed temopraryly
  bool now = digitalRead(PIN_BUTTON); // HIGH idle, LOW pressed
  unsigned long t = nowMs;
  if (now != btnLast && (t - btnLastChange) > DEBOUNCE_MS) {
    btnLast = now;
    btnLastChange = t;
    if (!now) { // pressed
      pressStart = t;
      pressed = true;
    } else {    // released
      if (pressed) {
        pressed = false;
        return true;
      }
    }
  }
  return false;
}

// ====== Calibration state machine ======
enum CalState {
  CAL_IDLE,
  CAL_WAIT_P7_PRESS,
  CAL_DOING_P7,
  CAL_WAIT_P4_PRESS,
  CAL_DOING_P4
};
CalState calState = CAL_IDLE;

// ---- CAL helpers: settle then long average (blocking during CAL only) ----
void waitSettle(unsigned long ms){
  if (ms == 0) return;
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { delay(50); }
}

float calLongAverageADC(int pin){
  float sum = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sum += analogRead(pin);
    delay(CAL_DELAY_MS);
  }
  return sum / (float)CAL_SAMPLES;
}

// (kept) quick average for fast tasks
float quickAverageADC(int pin) {
  float sum = 0;
  for (int i=0;i<40;i++){ sum += analogRead(pin); delay(2); }
  return sum/40.0f;
}

// ====== Ultrasonic helpers ======
float readDrumDistanceCm() {
  digitalWrite(ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG, LOW);

  long duration = pulseIn(ULTRASONIC_ECHO, HIGH, 30000); // 30ms timeout (~5m)
  if (duration == 0) return -1.0f; // no echo
  return duration / 58.0f;         // µs -> cm
}

// ====== Serial command handler ======
// Commands example (Serial Monitor, NL or NL+CR):
//   set_default   -> calibrate drum empty distance (drumEmptyDistanceCm)
//   show_drum     -> print current drumEmptyDistanceCm
void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "set_default") {
    Serial.println();
    Serial.println(F("=== Drum EMPTY calibration ==="));
    Serial.println(F("Make sure the drum has NO water (or your chosen '0 level')."));
    Serial.println(F("Measuring ultrasonic distance..."));

    const int samples = 5;
    float sum = 0;
    int valid = 0;

    for (int i = 0; i < samples; i++) {
      float d = readDrumDistanceCm();
      if (d > 0) {
        sum += d;
        valid++;
      }
      delay(150);
    }

    if (valid > 0) {
      float avg = sum / valid;
      saveDrumEmpty(avg);
      Serial.print(F("New EMPTY drum distance saved: "));
      Serial.print(drumEmptyDistanceCm);
      Serial.println(F(" cm"));
    } else {
      Serial.println(F("Calibration FAILED: no valid ultrasonic readings."));
    }

    Serial.println(F("================================"));
    Serial.println();
  }
  else if (cmd == "show_drum") {
    Serial.print(F("Current drumEmptyDistanceCm: "));
    Serial.print(drumEmptyDistanceCm);
    Serial.println(F(" cm"));
  }
}

// ====== Setup / Loop ======
void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PH_SENSOR, INPUT);
  pinMode(TDS_SENSOR, INPUT);

  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);

  Serial.begin(9600);
  dht.begin();
  Rtc.Begin();

  loadCalibrationFromEEPROM();

  Serial.println(F("Nano Ready. One-button pH Calibration (D13), Buzzer (D11)."));
  Serial.print(F("C7=")); Serial.print(C7, 2);
  Serial.print(F("  C4=")); Serial.println(C4, 2);
  Serial.println(F("Press D13 once to enter pH calibration mode (4 fast beeps)."));
  Serial.println(F("Type 'set_default' over Serial when DRUM is EMPTY to calibrate water level sensor."));
  Serial.print(F("Drum empty distance (cm): "));
  Serial.println(drumEmptyDistanceCm, 2);
}

void loop() {
  nowMs = millis();

  // ====== 0) Handle serial commands (drum calibration etc.) ======
  handleSerialCommands();

  // ====== 1) Handle button FIRST for instant response ======
  if (calState == CAL_IDLE) {
    if (buttonPressed()) {
      beepFast4(); // instant feedback on entry
      Serial.println(F("[CAL] Entered pH calibration mode."));
      Serial.println(F("[CAL] Rinse, dip probe into pH 7.00 buffer, then press button to start capture."));
      calState = CAL_WAIT_P7_PRESS;
    }
  }
  else if (calState == CAL_WAIT_P7_PRESS) {
    if (buttonPressed()) {
      beepNormal2(); // immediate confirm
      Serial.print(F("[CAL] Settling in pH 7.00 for "));
      Serial.print(CAL_SETTLE_MS/1000); Serial.println(F(" s..."));
      calState = CAL_DOING_P7;
    }
  }
  else if (calState == CAL_DOING_P7) {
    // Settle + longer average for reliable capture
    waitSettle(CAL_SETTLE_MS);
    Serial.print(F("[CAL] Averaging for ~"));
    Serial.print((CAL_SAMPLES*CAL_DELAY_MS)/1000.0f,1); Serial.println(F(" s..."));
    float phADC = calLongAverageADC(PH_SENSOR);
    saveC7(phADC);
    Serial.print(F("[CAL] Saved C7 = ")); Serial.println(C7, 2);
    beepNormal3(); // done
    Serial.println(F("[CAL] Now rinse, dip probe into pH 4.01 buffer, then press to start."));
    calState = CAL_WAIT_P4_PRESS;
  }
  else if (calState == CAL_WAIT_P4_PRESS) {
    if (buttonPressed()) {
      beepNormal2();
      Serial.print(F("[CAL] Settling in pH 4.01 for "));
      Serial.print(CAL_SETTLE_MS/1000); Serial.println(F(" s..."));
      calState = CAL_DOING_P4;
    }
  }
  else if (calState == CAL_DOING_P4) {
    waitSettle(CAL_SETTLE_MS);
    Serial.print(F("[CAL] Averaging for ~"));
    Serial.print((CAL_SAMPLES*CAL_DELAY_MS)/1000.0f,1); Serial.println(F(" s..."));
    float phADC = calLongAverageADC(PH_SENSOR);
    saveC4(phADC);
    Serial.print(F("[CAL] Saved C4 = ")); Serial.println(C4, 2);
    beepNormal3();
    Serial.println(F("[CAL] Calibration complete. Exiting pH calibration mode."));
    calState = CAL_IDLE;
  }

  // ====== 2) Do sensor work on a timed schedule (non-blocking main loop) ======
  if (nowMs - lastSenseMs >= SENSE_PERIOD_MS) {
    lastSenseMs = nowMs;

    // Compute linear pH mapping from current C7/C4
    float m_ph = (7.00f - 4.00f) / (C7 - C4 + 1e-6f);
    float b_ph = 7.00f - m_ph * C7;

    // Water temperature
    float waterTempraw = analogRead(WATER_TEMP); // readWaterTempC_DS18B20(); // °C (could be NAN if missing)
    float waterTempV =  waterTempraw * (5.0/1023.0);
    float Rntc = (5.0 / waterTempV - 1.0) * 10000;
    float A = 0.001129148;
    float B = 0.000234125;
    float C = 0.0000000876741;
    float lnR = log(Rntc);
    float waterTempC = 1.0 / (A+B*lnR+C*lnR*lnR*lnR);
    waterTempC = waterTempC - 273.15; // Kelvin to Celcius

    // pH reading (lighter average to keep it snappy at runtime)
    float phADC_now = readStableAnalog(PH_SENSOR, 15);
    float phValue = m_ph * phADC_now + b_ph;

    // TDS (with temp comp; default 25C if temp missing)
    float tC = isnan(waterTempC) ? 29.0f : waterTempC;
    float tdsPPM = readTDSppm(tC);

    // Air temp & humidity
    float airTemp = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Time
    RtcDateTime now = Rtc.GetDateTime();

    // === Drum ultrasonic reading ===
    float drumDistanceCm = readDrumDistanceCm();  // sensor -> water
    float drumWaterDepthCm = NAN;
    float drumLiters = NAN;

    if (drumDistanceCm > 0) {
      drumWaterDepthCm = drumEmptyDistanceCm - drumDistanceCm;
      if (drumWaterDepthCm < 0) drumWaterDepthCm = 0;
      if (drumWaterDepthCm > drumEmptyDistanceCm) drumWaterDepthCm = drumEmptyDistanceCm;

      drumLiters = (drumWaterDepthCm / drumEmptyDistanceCm) * DRUM_CAPACITY_LITERS;
    }

    // ====== Output (line for ESP32 via RX/TX) ======
    Serial.print("ph:");
    Serial.print(phValue, 2);

    Serial.print(",t:");
    if (isnan(waterTempC)) Serial.print("nan");
    else Serial.print(waterTempC, 2);

    Serial.print(",ppm:");
    Serial.print(tdsPPM, 0);

    Serial.print(",airT:");
    Serial.print(airTemp, 1);

    Serial.print(",hum:");
    Serial.print(humidity, 1);

    Serial.print(",hour:");
    Serial.print(now.Hour());

    Serial.print(",minute:");
    Serial.print(now.Minute());

    Serial.print(",second:");
    Serial.print(now.Second());

    // New: drum ultrasonic data
    Serial.print(",drumD:");   // distance sensor -> water (cm)
    if (drumDistanceCm <= 0) Serial.print("nan");
    else Serial.print(drumDistanceCm, 1);

    Serial.print(",drumDepth:");  // water depth (cm)
    if (isnan(drumWaterDepthCm)) Serial.print("nan");
    else Serial.print(drumWaterDepthCm, 1);

    Serial.print(",drumLiters:"); // predicted liters (estimated)
    if (isnan(drumLiters)) Serial.print("nan");
    else Serial.print(drumLiters, 1);

    Serial.println();
  }

  // ====== 3) Tiny idle — keeps loop cycling fast for button responsiveness ======
  delay(1);  // ~1ms to avoid a hot spin; still very responsive
}
