/*
 * MINIMAL WOKWI TEST — verifies serial capture works end-to-end.
 * Temporary file — will be reverted to the full alarm clock sketch.
 */

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("HELLO WOKWI");
}

void loop() {
    delay(1000);
    Serial.println("TICK");
}
