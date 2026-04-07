// Package: parallel_test
// Prints "Parallelly IM" every 3 seconds to prove concurrency

void setup() {
    Serial.println("parallel_test: started");
}

void loop() {
    Serial.println("Parallelly IM");
    delay(3000);
}
