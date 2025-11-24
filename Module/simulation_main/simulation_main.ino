#include <math.h> 

/* ================= SIMULATION SETTINGS ================= */
// Set to TRUE to test logic without hardware. 
bool SIMULATION_MODE = true; 

/* ================= Configuration & Pins ================= */
// (These pins are ignored in Simulation Mode)
#define TEMP_PIN 15        
#define MQ135_PIN 34       
#define REED_PIN 27        
#define WATER_LEVEL_PIN 14 

// Sensor Thresholds
#define WATER_THRESHOLD 400     
#define SERIESRESISTOR 10000    
#define NOMINAL_RESISTANCE 10000 
#define NOMINAL_TEMP 25   
#define BCOEFFICIENT 3950

// Actuators
#define FAN_PIN 25         
#define HEATER_PIN 26      
#define HUMIDIFIER_PIN 33  

// Serial to Camera
#define CAM_TX 17
#define CAM_RX 16
HardwareSerial CAMSerial(2); 

/* ================= Global Variables ================= */
int g_stage = 0;        
int g_percent = 0;   
bool g_done = false;
bool g_tank_empty = false;

// Current Sensor Readings
float currentTemp = 0.0;
float currentHum = 0.0; // <--- ADDED HUMIDITY VARIABLE
int currentGas = 0;
bool currentWaterLow = false;

// Timers
unsigned long stageTimer = 0;      
unsigned long lastCamUpdate = 0;   
unsigned long lastSensorRead = 0;  

/* ================= Setup ================= */
void setup() {
  Serial.begin(115200);
  CAMSerial.begin(115200, SERIAL_8N1, CAM_RX, CAM_TX);

  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(WATER_LEVEL_PIN, INPUT); 
  pinMode(TEMP_PIN, INPUT);        

  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(HUMIDIFIER_PIN, OUTPUT);

  stopAllActuators();
  
  if (SIMULATION_MODE) {
    Serial.println("!!! RUNNING IN SIMULATION MODE WITH HUMIDITY !!!");
    
    // Initialize fake values
    currentGas = 2000;    // Start Dirty
    currentTemp = 25.0;   // Room Temp
    currentHum = 80.0;    // Start Wet (Wet clothes)
    currentWaterLow = false; 
  }

  readSensors(); 
  resetMachine();
}

/* ================= Helper Functions ================= */
void stopAllActuators() {
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(HUMIDIFIER_PIN, LOW);
  if(SIMULATION_MODE) Serial.println("[ACTUATOR] ALL STOPPED");
}

void resetMachine() {
  g_stage = 0;
  g_percent = 0;
  g_done = false;
  g_tank_empty = false;
  
  // Reset Simulation Data
  if(SIMULATION_MODE) {
    currentGas = 2000; 
    currentHum = 80.0; // Reset to wet state on restart
  }
  
  stopAllActuators();
  sendState();
  Serial.println("State Reset - Waiting for 'start' command...");
}

bool isDoorClosed() {
  if (SIMULATION_MODE) return true; 
  return digitalRead(REED_PIN) == LOW; 
}

/* ================= Sensor Logic ================= */
void readSensors() {
  if (SIMULATION_MODE) {
    simulateSensors(); 
  } else {
    readRealSensors(); 
  }
  
  // Debug Print
  Serial.print(" [SENSORS] Temp:"); Serial.print(currentTemp);
  Serial.print("C | Hum:"); Serial.print(currentHum); // <--- PRINT HUMIDITY
  Serial.print("% | Gas:"); Serial.print(currentGas);
  Serial.print(" | WaterLow:"); Serial.println(currentWaterLow ? "YES" : "NO");
}

// --- FAKE DATA GENERATOR ---
void simulateSensors() {
  // 1. Simulate Temp & Humidity based on Heater
  if (digitalRead(HEATER_PIN) == HIGH) {
    currentTemp += 0.5;   // Heat up
    if(currentHum > 10.0) currentHum -= 2.0; // Dry out fast
  } else {
    currentTemp -= 0.1;   // Cool down
    // Humidity stays stable or rises slowly if just sitting
  }

  // 2. Simulate Mist effect (Perfume Stage)
  if (digitalRead(HUMIDIFIER_PIN) == HIGH) {
    currentHum += 5.0; // Humidity spikes when spraying mist
  }

  // 3. Simulate Gas cleaning up
  if (g_stage == 1 && currentGas > 500) {
    currentGas -= 100; 
  }

  currentWaterLow = false; 
}

// --- REAL HARDWARE READER (No DHT here) ---
void readRealSensors() {
  // Temp
  int rawTemp = analogRead(TEMP_PIN);
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
  // Gas & Water
  currentGas = analogRead(MQ135_PIN);
  int waterRaw = analogRead(WATER_LEVEL_PIN);
  currentWaterLow = (waterRaw < WATER_THRESHOLD); 
  
  // Real sensor missing, set to 0 to avoid bugs
  currentHum = 0.0; 
}

/* ================= Main Logic ================= */
void processController() {
  
  if (!isDoorClosed()) {
    stopAllActuators();
    Serial.println("Door Open");
    return; 
  }

  unsigned long elapsed = millis() - stageTimer;

  // Stage 0: Drying
  if (g_stage == 0) {
    if (elapsed < 500) { 
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(HEATER_PIN, HIGH); 
        digitalWrite(HUMIDIFIER_PIN, LOW);
        Serial.println(">>> STAGE 0: DRYING (Fan ON, Heater ON)");
    }
    
    g_percent = map(elapsed, 0, 20000, 0, 25);
    if (elapsed > 20000) nextStage(1);
  }

  // Stage 1: Deodorizing
  else if (g_stage == 1) {
    if (elapsed < 500) {
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(HEATER_PIN, HIGH); 
        Serial.println(">>> STAGE 1: DEODORIZING (Checking Gas...)");
    }

    g_percent = map(elapsed, 0, 15000, 25, 50);
    if (currentGas < 1200 || elapsed > 15000) nextStage(2);
  }

  // Stage 2: Perfuming
  else if (g_stage == 2) {
    if (currentWaterLow) {
      g_tank_empty = true;
      Serial.println("ERR: Tank Empty!");
      nextStage(3);
      return;
    }

    if (elapsed < 500) {
        digitalWrite(HEATER_PIN, LOW);
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(HUMIDIFIER_PIN, HIGH);
        Serial.println(">>> STAGE 2: PERFUMING (Mist ON, Humidity Rising)");
    }

    g_percent = map(elapsed, 0, 5000, 50, 75);
    if (elapsed > 5000) {
      digitalWrite(HUMIDIFIER_PIN, LOW); 
      nextStage(3);
    }
  }

  // Stage 3: Final Dry (With Logic Restored)
  else if (g_stage == 3) {
    if (elapsed < 500) {
        digitalWrite(HEATER_PIN, HIGH);
        digitalWrite(FAN_PIN, HIGH);
        Serial.println(">>> STAGE 3: FINAL DRY (Waiting for Hum < 60%)");
    }

    g_percent = map(elapsed, 0, 10000, 75, 99);
    
    // RESTORED LOGIC: Wait for Humidity to drop OR timer
    if (currentHum < 60.0 || elapsed > 15000) {
        finishProcess();
    }
  }
}

void nextStage(int next) {
  g_stage = next;
  stageTimer = millis();
  Serial.print("--- SWITCHING TO STAGE "); Serial.print(next); Serial.println(" ---");
}

void finishProcess() {
  stopAllActuators();
  g_percent = 100;
  g_done = true;
  g_stage = 4; 
  sendState();
  Serial.println("✅ CLEANING CYCLE COMPLETE");
}

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
      Serial.println("COMMAND RECEIVED: START");
    }
  }
}

void loop() {
  listenSerialCommands();

  // Update Sensors every 2 seconds
  if (millis() - lastSensorRead > 2000) {
    readSensors();
    lastSensorRead = millis();
  }

  if (!g_done) processController();

  if (millis() - lastCamUpdate > 500) {
    sendState();
    lastCamUpdate = millis();
  }
}