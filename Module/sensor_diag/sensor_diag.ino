#include "DHT.h"
#include <math.h> 

/* ================= Pin Definitions ================= */
#define DHT_PIN 4
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

#define TEMP_PIN 15        
#define MQ135_PIN 34
#define REED_PIN 27
#define WATER_LEVEL_PIN 14 // Must use Analog Read for HW-038

// Threshold: Below this number = EMPTY. Above = HAS WATER.
// HW-038 usually reads 0-100 when dry, and >1000 when wet.
#define WATER_THRESHOLD 400 

/* ================= Thermistor Config ================= */
#define SERIESRESISTOR 10000    
#define NOMINAL_RESISTANCE 10000 
#define NOMINAL_TEMP 25   
#define BCOEFFICIENT 3950

void setup() {
  Serial.begin(115200);
  
  dht.begin();
  Serial.println("Initializing Sensors... (Waiting 2 seconds for DHT11)");
  delay(2000); 

  pinMode(REED_PIN, INPUT_PULLUP);
  
  // CHANGE: Remove INPUT_PULLUP for the water sensor.
  // HW-038 needs a clean analog input.
  pinMode(WATER_LEVEL_PIN, INPUT); 
  pinMode(TEMP_PIN, INPUT); 
  
  Serial.println("--- HARDWARE DIAGNOSTIC START ---");
}

void loop() {
  delay(2000);

  // --- 1. DHT Humidity ---
  float hum = dht.readHumidity();
  
  // --- 2. Analog Temp Calculation ---
  int rawTemp = analogRead(TEMP_PIN); 
  float resistance = 4095.0 / (float)rawTemp - 1.0;
  resistance = SERIESRESISTOR / resistance;
  float steinhart = resistance / NOMINAL_RESISTANCE;     
  steinhart = log(steinhart);                      
  steinhart /= BCOEFFICIENT;                       
  steinhart += 1.0 / (NOMINAL_TEMP + 273.15);      
  steinhart = 1.0 / steinhart;                     
  float tempC = steinhart - 273.15;

  // --- 3. Other Sensors ---
  int gas = analogRead(MQ135_PIN);
  bool doorOpen = digitalRead(REED_PIN);        

  // --- 4. Water Level Logic (FIXED) ---
  int waterRaw = analogRead(WATER_LEVEL_PIN); // Read 0-4095
  bool tankEmpty = (waterRaw < WATER_THRESHOLD); // If < 400, it's empty

  // --- Print Data ---
  if (isnan(hum)) {
    Serial.print("Hum: ERROR");
  } else {
    Serial.print("Hum: "); Serial.print(hum); Serial.print("%");
  }

  Serial.print(" | Temp: "); Serial.print(tempC); 
  Serial.print("C | Gas: "); Serial.print(gas);
  Serial.print(" | Door: "); Serial.print(doorOpen ? "OPEN" : "CLOSED");
  
  // Print Raw Water Value so you can calibrate it
  Serial.print(" | WaterVal: "); Serial.print(waterRaw);
  Serial.print(" -> "); Serial.println(tankEmpty ? "EMPTY" : "FULL");
}