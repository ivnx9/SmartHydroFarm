#include <DHT.h>
#include <RtcDS1302.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// === Pins ===
#define DHTPIN 2
#define DHTTYPE DHT11
#define PH_SENSOR A0
#define TDS_SENSOR A1
#define DS18B20_PIN A2        // DS18B20 data line on A2 (digital-capable)

// Calibration I/O
#define PIN_BUTTON 13         // Button -> GND (INPUT_PULLUP)
#define PIN_BUZZER 11         // Buzzer -> GND (active HIGH)

// === RTC wiring (DS1302) ===
ThreeWire myWire(5, 4, 6);    // DS1302: DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// === DS18B20 smoothing & trim ===
float waterEMA = NAN;
const float EMA_ALPHA = 0.30;   // 0.2..0.4 typical
float tempOffsetC = 0.00;       // set after comparing vs reference thermometer

// === EEPROM layout & defaults ===
const int EE_MAGIC_ADDR = 0;    // 4 bytes
const int EE_C7_ADDR    = 4;    // 4 bytes float
const int EE_C4_ADDR    = 8;    // 4 bytes float
const uint32_t EE_MAGIC = 0x50484341; // 'PHCA' marker

// Your previous defaults (used if EEPROM not set yet)
const float C7_DEFAULT = 567.50;     // ADC at pH ~6.86/7.00 buffer
const float C4_DEFAULT = 640.00;     // ADC at pH ~4.01 buffer

// Runtime calibration points (persisted in EEPROM)
float C7 = NAN;   // raw ADC at pH 7.00
float C4 = NAN;   // raw ADC at pH 4.01

// === TDS/EC options ===
const float TDS_FACTOR = 0.7;   // 0.5 (NaCl) or 0.7 (442) — match your handheld
const float EC_TCOMP   = 0.02;  // temperature coefficient vs 25C

// --- helpers ---
float readStableAnalog(int pin, int samples = 60) {
  float sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return sum / samples;
}

float readWaterTempC_DS18B20() {
  for (int attempt = 0; attempt < 3; attempt++) {
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55 && t < 125) {
      t += tempOffsetC;
      if (isnan(waterEMA)) waterEMA = t;
      else waterEMA = EMA_ALPHA * t + (1.0 - EMA_ALPHA) * waterEMA;
      return waterEMA;
    }
    delay(50);
  }
  return waterEMA; // may be NAN on first run if sensor missing
}

// Convert raw TDS sensor voltage to EC (µS/cm), with temp compensation, then to PPM.
float readTDSppm(float waterTempC) {
  float tdsRaw = readStableAnalog(TDS_SENSOR, 60);
  float v = tdsRaw * (5.0f / 1023.0f);  // 10-bit ADC @5V

  // Temperature compensation (reference 25°C)
  float compCoeff = 1.0f + EC_TCOMP * (waterTempC - 25.0f);
  float vComp = v / compCoeff;

  // DFRobot polynomial: voltage -> EC (µS/cm)
  float ec_uS = (133.42f * vComp * vComp * vComp
               - 255.86f * vComp * vComp
               + 857.39f * vComp);
  if (ec_uS < 0) ec_uS = 0;

  // Convert EC to PPM by factor
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
    // Sanity
    if (isnan(C7) || C7 < 0 || C7 > 1023) C7 = C7_DEFAULT;
    if (isnan(C4) || C4 < 0 || C4 > 1023) C4 = C4_DEFAULT;
  } else {
    // First time init
    EEPROM.put(EE_MAGIC_ADDR, EE_MAGIC);
    C7 = C7_DEFAULT;
    C4 = C4_DEFAULT;
    eepromWriteFloat(EE_C7_ADDR, C7);
    eepromWriteFloat(EE_C4_ADDR, C4);
  }
}

void saveC7(float v){ C7 = v; eepromWriteFloat(EE_C7_ADDR, C7); }
void saveC4(float v){ C4 = v; eepromWriteFloat(EE_C4_ADDR, C4); }

// ====== Buzzer helpers ======
void beepOnce(int onMs, int offMs){
  digitalWrite(PIN_BUZZER, HIGH);
  delay(onMs);
  digitalWrite(PIN_BUZZER, LOW);
  delay(offMs);
}
void beepFast4(){ for(int i=0;i<4;i++) beepOnce(80,80); }
void beepNormal2(){ for(int i=0;i<2;i++) beepOnce(200,150); }
void beepNormal3(){ for(int i=0;i<3;i++) beepOnce(200,150); }

// ====== Button (D13 to GND) with debounce ======
bool btnLast = true; // INPUT_PULLUP idle HIGH
unsigned long btnLastChange = 0;
const unsigned long DEBOUNCE_MS = 30;
unsigned long pressStart = 0;
bool pressed = false;

bool buttonPressed(){
  bool now = digitalRead(PIN_BUTTON); // HIGH idle, LOW pressed
  unsigned long t = millis();
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

// ====== Setup / Loop ======
void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PH_SENSOR, INPUT);
  pinMode(TDS_SENSOR, INPUT);

  Serial.begin(9600);
  dht.begin();
  Rtc.Begin();

  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(true);

  loadCalibrationFromEEPROM();

  Serial.println(F("Nano Ready. One-button Calibration (D13), Buzzer (D11)."));
  Serial.print(F("C7=")); Serial.print(C7, 2);
  Serial.print(F("  C4=")); Serial.println(C4, 2);
  Serial.println(F("Press D13 once to enter calibration mode (4 fast beeps)."));
}

void loop() {
  // ====== Handle calibration button flow ======
  if (calState == CAL_IDLE) {
    if (buttonPressed()) {
      // Enter calibration mode
      beepFast4(); // 4 fast beeps
      Serial.println(F("[CAL] Entered calibration mode."));
      Serial.println(F("[CAL] Rinse, dip probe into pH 7.00 buffer, then press button to start capture."));
      calState = CAL_WAIT_P7_PRESS;
    }
  }
  else if (calState == CAL_WAIT_P7_PRESS) {
    if (buttonPressed()) {
      beepNormal2(); // 2 normal beeps
      Serial.println(F("[CAL] Capturing pH 7.00... keep still 10–20s."));
      calState = CAL_DOING_P7;
    }
  }
  else if (calState == CAL_DOING_P7) {
    // Your required stable ADC read
    float phADC = readStableAnalog(PH_SENSOR, 100);
    saveC7(phADC);
    Serial.print(F("[CAL] Saved C7 = ")); Serial.println(C7, 2);
    beepNormal3(); // 3 beeps done
    Serial.println(F("[CAL] Now rinse, dip probe into pH 4.01 buffer, then press to start."));
    calState = CAL_WAIT_P4_PRESS;
  }
  else if (calState == CAL_WAIT_P4_PRESS) {
    if (buttonPressed()) {
      beepNormal2(); // 2 normal beeps
      Serial.println(F("[CAL] Capturing pH 4.01... keep still 10–20s."));
      calState = CAL_DOING_P4;
    }
  }
  else if (calState == CAL_DOING_P4) {
    float phADC = readStableAnalog(PH_SENSOR, 100);
    saveC4(phADC);
    Serial.print(F("[CAL] Saved C4 = ")); Serial.println(C4, 2);
    beepNormal3(); // 3 beeps done
    Serial.println(F("[CAL] Calibration complete. Exiting calibration mode."));
    calState = CAL_IDLE;
  }

  // ====== Normal sensing/printing (unchanged format) ======
  // Compute linear pH mapping from current C7/C4
  // m_ph = (7 - 4) / (C7 - C4); b_ph = 7 - m_ph*C7
  float m_ph = (7.00f - 4.00f) / (C7 - C4 + 1e-6f);
  float b_ph = 7.00f - m_ph * C7;

  // Water temperature
  float waterTempC = readWaterTempC_DS18B20(); // °C (could be NAN if missing)

  // pH reading
  float phADC_now = readStableAnalog(PH_SENSOR, 60);
  float phValue = m_ph * phADC_now + b_ph;

  // TDS (with temp comp; default 25C if temp missing)
  float tC = isnan(waterTempC) ? 25.0f : waterTempC;
  float tdsPPM = readTDSppm(tC);

  // Air temp & humidity (kept your variables; switch back to DHT if desired)
  // float airTemp = dht.readTemperature();
  // float humidity = dht.readHumidity();
  float airTemp = phADC_now;                       // (your previous placeholder)
  float humidity = readStableAnalog(TDS_SENSOR,60);// (your previous placeholder)

  // Time
  RtcDateTime now = Rtc.GetDateTime();

  // Output (same format you had)
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
  Serial.println(now.Second());

  delay(2000);
}
