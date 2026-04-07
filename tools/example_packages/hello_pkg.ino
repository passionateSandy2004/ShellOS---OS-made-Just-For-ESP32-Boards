// Package: test_pkg
// Prints "hii guys 2" every 6 seconds
// Author: ShellOS

void setup() {
    Serial.println("test_pkg: started");
}

void loop() {
    Serial.println("hii guys 2");
    delay(6000);
}
