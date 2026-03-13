/*
#include <DHT.h>
#include <RtcDS1302.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS A2        // DS18B20 data wire on A2 (used as digital pin)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// Smoothing (EMA) — adjust 0.2..0.4 for more/less smoothing
float waterEMA = NAN;
const float alpha = 0.3;

// Optional single-point trim (after testing with a reference therm.)
float tempOffsetC = 0.00;  // set later, e.g., +0.25 to add 0.25 °C

ThreeWire myWire(5, 4, 6);        // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);    // RTC Object

// === Arduino UNO sensor sender ===
// Reads sensors and sends data to ESP32 via Serial every 2 seconds
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
#define PH_SENSOR A0
#define TDS_SENSOR A1
#define WATER_TEMP_SENSOR A2

void setup() {
  Serial.begin(9600);
  dht.begin();

  Rtc.Begin();

  RtcDateTime currentTime = RtcDateTime(__DATE__ , __TIME__);
  //Rtc.SetDateTime(currentTime);

  ds18b20.begin();
  ds18b20.setResolution(12);           // 12-bit for best accuracy (±0.5 °C typical)
  ds18b20.setWaitForConversion(true);  // block until conversion done on requestTemperatures()
}

float readStableAnalog(int pin, int samples = 60) {
  float total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(5);
  }
  return total / samples;
}

float simulateWaterTemp() {
  return 25.5 + random(-10, 10) * 0.1;  // simulate small variation
}

float readWaterTempC_DS18B20() {
  // up to 3 tries to avoid occasional CRC/disconnect glitches
  for (int attempt = 0; attempt < 3; attempt++) {
    ds18b20.requestTemperatures();               // starts and waits for conversion (~750 ms at 12-bit)
    float t = ds18b20.getTempCByIndex(0);        // first device on the bus

    if (t != DEVICE_DISCONNECTED_C && t > -55 && t < 125) {
      // apply optional offset calibration
      t += tempOffsetC;

      // EMA smoothing
      if (isnan(waterEMA)) waterEMA = t;
      else waterEMA = alpha * t + (1.0 - alpha) * waterEMA;

      return waterEMA;  // smoothed reading
    }
    delay(50); // short pause before retry
  }
  // if all tries fail, keep last EMA (if any) or return NAN
  return waterEMA; 
}

const float C7 = 600; //590; //659; //515.0; 555 556
const float C4 = 689; //792; //620.0;

const float m_ph = (6.86 - 4.01) / (C7 - C4);
const float b_ph = 6.86 - m_ph * C7; // small offset tweak

void loop() {
  //float phRaw = readStableAnalog(PH_SENSOR);
  //float phVolt = phRaw * (5.0 / 1023.0);
  //float phValue = 7 + ((2.5 - phVolt) / 0.18); // Adjust based on calibration

  float phval = readStableAnalog(PH_SENSOR, 100); // analogRead(A0);
  float  voltage = m_ph * phval + b_ph;    //phval; //* (5.0/1023.0) - 0.7;
  float phValue = voltage;

  float tdsRaw = readStableAnalog(TDS_SENSOR);
  float tdsVolt = tdsRaw * (5.0 / 1023.0);
  float tds = (133.42 * tdsVolt * tdsVolt * tdsVolt - 255.86 * tdsVolt * tdsVolt + 857.39 * tdsVolt) * 0.5;

    // float waterTemp = analogRead(A2) * (5.0/1023.0); //simulateWaterTemp();
     float waterTemp = readStableAnalog(WATER_TEMP_SENSOR); // * (5.0/1023.0); // old (volts)
    //float waterTemp = readWaterTempC_DS18B20();        // °C, smoothed & calibrated


  float airTemp = dht.readTemperature();
  float humidity = dht.readHumidity();

  RtcDateTime now = Rtc.GetDateTime();

  Serial.print("ph:");
  Serial.print(phValue, 2);
  Serial.print(",t:");
  Serial.print(waterTemp, 2);
  Serial.print(",ppm:");
  Serial.print(tds,0); //tds, 0);
  Serial.print(",airT:");
  Serial.print(airTemp, 1);
  Serial.print(",hum:");
  Serial.print(humidity, 1);
  Serial.print(",hour:");
  Serial.print(now.Hour(), 1);
  Serial.print(",minute:");
  Serial.print(now.Minute(), 1);
  Serial.print(",second:");
  Serial.println(now.Second(), 1);

  delay(2000);
}
*/



#include <DHT.h>
#include <RtcDS1302.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// === Pins ===
#define DHTPIN 2
#define DHTTYPE DHT11
#define PH_SENSOR A0
#define TDS_SENSOR A1
#define DS18B20_PIN A2        // DS18B20 data line on A2 (as a digital-capable pin)

ThreeWire myWire(5, 4, 6);           // DS1302: DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

// === DS18B20 smoothing & trim ===
float waterEMA = NAN;
const float EMA_ALPHA = 0.30;         // 0.2..0.4 typical
float tempOffsetC = 0.00;             // set after comparing vs reference thermometer

// === pH calibration (from your code) ===
const float C7 =  567.50; //600; // ADC at pH ~6.86/7.00 buffer
const float C4 = 640; //689; // ADC at pH ~4.01 buffer
const float m_ph = (7.00 - 4.00) / (C7 - C4); // 6.86 yung 7.00 tapos 4.01 yung 4.00
const float b_ph = 7.00 - m_ph * C7;

// === TDS/EC options ===
const float TDS_FACTOR = 0.7;         // 0.5 (NaCl) most meters; set 0.7 if your handheld uses 442/0.7 scale
// Temp compensation coefficient for EC (per °C from 25°C). 0.02 is common.
const float EC_TCOMP = 0.02;

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
    ds18b20.requestTemperatures();         // waits until conversion done (with setWaitForConversion(true))
    float t = ds18b20.getTempCByIndex(0);  // first device
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
  // Average the analog input for less noise
  float tdsRaw = readStableAnalog(TDS_SENSOR, 60);
  float v = tdsRaw * (5.0f / 1023.0f);       // sensor output voltage (V) on 5V ADC

  // Temperature compensation (reference 25°C)
  float compCoeff = 1.0f + EC_TCOMP * (waterTempC - 25.0f);
  float vComp = v / compCoeff;

  // DFRobot polynomial: maps voltage -> EC (µS/cm)
  // Note: Many TDS boards output a voltage proportional to EC. This poly assumes their circuit.
  float ec_uS = (133.42f * vComp * vComp * vComp
               - 255.86f * vComp * vComp
               + 857.39f * vComp);

  if (ec_uS < 0) ec_uS = 0;

  // Convert EC (µS/cm) to TDS (ppm) using selected factor
  float ppm = ec_uS * TDS_FACTOR;

  return ppm;
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  Rtc.Begin();

  // If you need to set RTC once:
  // RtcDateTime currentTime = RtcDateTime(__DATE__ , __TIME__);
  // Rtc.SetDateTime(currentTime);

  ds18b20.begin();
  ds18b20.setResolution(12);
  ds18b20.setWaitForConversion(true);
}

void loop() {
  // --- Water temperature (DS18B20) ---
  float waterTempC = 28.5; // readWaterTempC_DS18B20();   // °C

  // --- pH (from your linearized calibration) ---
  float phADC = readStableAnalog(PH_SENSOR, 100);
  float phValue = m_ph * phADC + b_ph;

  // --- TDS/PPM (with temp compensation) ---
  float tdsPPM = readTDSppm(isnan(waterTempC) ? 25.0f : waterTempC); // default 25°C if temp missing

  // --- Air temp & humidity ---
  float airTemp = dht.readTemperature();
  float humidity = dht.readHumidity();

  // --- Time ---
  RtcDateTime now = Rtc.GetDateTime();

  // --- Output ---
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
