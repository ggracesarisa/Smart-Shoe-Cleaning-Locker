#include "DHT.h"
#include <math.h> 

/* ================= Configuration & Pins ================= */

// --- Sensors ---
#define DHT_PIN 4
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

#define TEMP_PIN 15        // Analog Temp Sensor (Thermistor)
#define MQ135_PIN 34       // Gas sensor
#define REED_PIN 27        // Door sensor
#define WATER_LEVEL_PIN 14 // HW-038 Water Sensor (Analog)

// --- Sensor Settings ---
#define WATER_THRESHOLD 400     // < 400 = Empty, > 400 = Water Present
#define SERIESRESISTOR 10000    // Thermistor Resistor (10k)
#define NOMINAL_RESISTANCE 10000 
#define NOMINAL_TEMP 25   
#define BCOEFFICIENT 3950

// --- Actuators ---
#define HEATER_PIN 26      
#define FAN_PIN 25         
#define HUMIDIFIER_PIN 33  

// --- Serial Communication (to ESP32-CAM) ---
#define CAM_TX 17
#define CAM_RX 16
HardwareSerial CAMSerial(2); 

/* ================= Global Variables ================= */
// Process State
int g_stage = 0;        // 0:Dry, 1:Deodorize, 2:Perfume, 3:Final Dry, 4:Done
int g_percent = 0;   
bool g_done = false;
bool g_tank_empty = false;

// Current Sensor Readings (Updated every 2s)
float currentTemp = 0.0;
float currentHum = 0.0;
int currentGas = 0;
bool currentWaterLow = false;

/* ================= Timing ================= */
unsigned long stageTimer = 0;      // Tracks how long we are in a stage
unsigned long lastCamUpdate = 0;   // Tracks Serial updates
unsigned long lastSensorRead = 0;  // Tracks Sensor reading (2s limit)

/* ================= Setup ================= */
void setup() {
  Serial.begin(115200);
  CAMSerial.begin(115200, SERIAL_8N1, CAM_RX, CAM_TX);

  // 1. Start DHT & Wait
  dht.begin();
  Serial.println("Booting... Waiting 2s for Sensors...");
  delay(2000); // Essential warm-up

  // 2. Pin Modes
  pinMode(REED_PIN, INPUT_PULLUP);
  
  // FIXED: Water sensor must be INPUT (Analog), not PULLUP
  pinMode(WATER_LEVEL_PIN, INPUT); 
  pinMode(TEMP_PIN, INPUT);

  pinMode(HEATER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HUMIDIFIER_PIN, OUTPUT);

  stopAllActuators();
  
  // Initial Sensor Read
  readSensors(); 
  
  resetMachine();
  Serial.println("System Ready: Integrated Version");
}

/* ================= Helper Functions ================= */
void stopAllActuators() {
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(HUMIDIFIER_PIN, LOW);
}

void resetMachine() {
  g_stage = 0;
  g_percent = 0;
  g_done = false;
  g_tank_empty = false;
  stopAllActuators();
  sendState();
  Serial.println("State Reset");
}

bool isDoorClosed() {
  return digitalRead(REED_PIN) == LOW; // Low = Magnet present (Closed)
}

/* ================= Sensor Logic (The Fixes) ================= */
void readSensors() {
  // 1. Humidity (DHT11)
  float h = dht.readHumidity();
  if (!isnan(h)) currentHum = h;

  // 2. Temperature (Analog Thermistor Logic)
  int rawTemp = analogRead(TEMP_PIN);
  // Prevent divide by zero if sensor disconnected
  if (rawTemp > 0 && rawTemp < 4095) {
      float resistance = 4095.0 / (float)rawTemp - 1.0;
      resistance = SERIESRESISTOR / resistance;
      float steinhart = resistance / NOMINAL_RESISTANCE;
      steinhart = log(steinhart);
      steinhart /= BCOEFFICIENT;
      steinhart += 1.0 / (NOMINAL_TEMP + 273.15);
      steinhart = 1.0 / steinhart;
      currentTemp = steinhart - 273.15;
  }

  // 3. Gas
  currentGas = analogRead(MQ135_PIN);

  // 4. Water Level (Analog Threshold Logic)
  int waterRaw = analogRead(WATER_LEVEL_PIN);
  currentWaterLow = (waterRaw < WATER_THRESHOLD); // True if Empty
  
  // Debug Print to Monitor
  Serial.print("T:"); Serial.print(currentTemp);
  Serial.print(" H:"); Serial.print(currentHum);
  Serial.print(" G:"); Serial.print(currentGas);
  Serial.print(" W_Raw:"); Serial.println(waterRaw);
}

/* ================= Main Process Controller ================= */
void processController() {
  
  // Safety Interlock
  if (!isDoorClosed()) {
    stopAllActuators();
    Serial.println("ALARM: Door Open");
    return; // Pause logic
  }

  unsigned long elapsed = millis() - stageTimer;

  /* ===== Stage 0: Drying (Heater + Fan) ===== */
  if (g_stage == 0) {
    digitalWrite(FAN_PIN, HIGH);
    digitalWrite(HEATER_PIN, HIGH);
    digitalWrite(HUMIDIFIER_PIN, LOW);

    g_percent = map(elapsed, 0, 20000, 0, 25);
    if (elapsed > 20000) nextStage(1);
  }

  /* ===== Stage 1: Deodorizing (Heater + Fan) ===== */
  else if (g_stage == 1) {
    // Wait until Gas is clean (<1200) OR 15 seconds passed
    g_percent = map(elapsed, 0, 15000, 25, 50);
    
    // We use 'currentGas' which is updated every 2s
    if (currentGas < 1200 || elapsed > 15000) {
      nextStage(2);
    }
  }

  /* ===== Stage 2: Perfuming (Fan + Humidifier ONLY) ===== */
  else if (g_stage == 2) {
    
    // Check Water Level before starting
    if (currentWaterLow) {
      Serial.println("SKIP: Tank Empty");
      g_tank_empty = true;
      nextStage(3);
      return;
    }

    // Heater OFF (Safety), Fan ON, Mist ON
    digitalWrite(HEATER_PIN, LOW);
    digitalWrite(FAN_PIN, HIGH);
    digitalWrite(HUMIDIFIER_PIN, HIGH);

    g_percent = map(elapsed, 0, 5000, 50, 75);

    // Run for 5 seconds
    if (elapsed > 5000) {
      digitalWrite(HUMIDIFIER_PIN, LOW); 
      nextStage(3);
    }
  }

  /* ===== Stage 3: Final Dry (Heater + Fan) ===== */
  else if (g_stage == 3) {
    digitalWrite(HEATER_PIN, HIGH);
    digitalWrite(FAN_PIN, HIGH);

    // Wait until Humidity < 60% OR 10 seconds passed
    g_percent = map(elapsed, 0, 10000, 75, 99);

    if (currentHum < 60 || elapsed > 10000) {
      finishProcess();
    }
  }
}

void nextStage(int next) {
  g_stage = next;
  stageTimer = millis();
}

void finishProcess() {
  stopAllActuators();
  g_percent = 100;
  g_done = true;
  g_stage = 4; 
  sendState();
  Serial.println("CLEANING COMPLETE");
}

/* ================= Communications ================= */
void sendState() {
  CAMSerial.println(String("stage ") + g_stage);
  CAMSerial.println(String("percent ") + g_percent);
  CAMSerial.println(String("done ") + g_done);
  if(g_tank_empty) CAMSerial.println("err_water 1");
}

void listenSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "start") {
      resetMachine();
      stageTimer = millis();
    }
  }
}

/* ================= Main Loop ================= */
void loop() {
  listenSerialCommands();

  // 1. Update Sensors (Only every 2 seconds)
  if (millis() - lastSensorRead > 2000) {
    readSensors();
    lastSensorRead = millis();
  }

  // 2. Run Logic (Only if running)
  if (!g_done) {
    processController();
  }

  // 3. Update Camera Display (Every 500ms)
  if (millis() - lastCamUpdate > 500) {
    sendState();
    lastCamUpdate = millis();
  }
}