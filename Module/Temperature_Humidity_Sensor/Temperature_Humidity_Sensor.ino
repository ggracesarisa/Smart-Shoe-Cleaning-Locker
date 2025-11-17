#include "DHT.h"

#define DHTPIN 4       // define GPIO pin
#define DHTTYPE DHT11 

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);  
  dht.begin();
}

void loop() {
  float hum = dht.readHumidity();       // read humidity value
  float temp = dht.readTemperature();   // read temperature value (°C)

  // check whether values are valid
  if (isnan(hum) || isnan(temp)) {
    Serial.println("Failed to read data");
  } 
  else {
    Serial.print("Humidity: ");
    Serial.print(hum);
    Serial.print("%\t");
    Serial.print("Temperature: ");
    Serial.print(temp);
    Serial.println("C");
  }
  delay(2000);  // read every 2 seconds
}
