#define MQ135_PIN 34   // Define GPIO pin

void setup() {
  Serial.begin(115200);   
  delay(1000);           
}

void loop() {

  int analogValue = analogRead(MQ135_PIN);

  // check whether values are valid (ESP32 gives values from 0 to 4095)
  if (analogValue < 0 || analogValue > 4095) {
    Serial.println("Failed to read data");
  } 
  else {
    Serial.print("gas concentration level: ");
    Serial.println(analogValue);
  }

  delay(2000);  // Read every 2 seconds
}
