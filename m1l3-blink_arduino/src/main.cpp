#include <Arduino.h>

void setup() {
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);
}

void loop() {
  digitalWrite(15, HIGH);
  delay(1000);
  digitalWrite(15, LOW);
  digitalWrite(16, HIGH);
  delay(1000);
  digitalWrite(16, LOW);
}
