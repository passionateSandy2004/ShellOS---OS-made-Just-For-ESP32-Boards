// Package: blink_led
// Blinks the ESP32-CAM flash LED (GPIO 4) every 3 seconds
// Author: ShellOS

#define LED_PIN 4

void setup() {
    pinMode(LED_PIN, OUTPUT);
    Serial.println("blink_led: started");
}

void loop() {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("blink_led: LED ON");
    delay(3000);

    digitalWrite(LED_PIN, LOW);
    Serial.println("blink_led: LED OFF");
    delay(3000);
}
