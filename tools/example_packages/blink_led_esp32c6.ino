// Package: blink_led_esp32c6
// Blinks the Seeed XIAO ESP32-C6 user LED (GPIO 15 — "User Light" on the wiki pinout)
// Author: ShellOS
//
// Note: blink_led.ino uses GPIO 4 for the ESP32-CAM flash LED; that pin is not
// the onboard LED on C6. Use this file when building/running packages on ESP32-C6.

#define LED_PIN 15

void setup() {
    pinMode(LED_PIN, OUTPUT);
    Serial.println("blink_led_esp32c6: started");
}

void loop() {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("blink_led_esp32c6: LED ON");
    delay(3000);

    digitalWrite(LED_PIN, LOW);
    Serial.println("blink_led_esp32c6: LED OFF");
    delay(3000);
}
