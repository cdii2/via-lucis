#include <Arduino.h>

#include "vialucis/version.h"

void setup() {
    Serial.begin(115200);
    Serial.printf("Via Lucis v%s\n", vialucis::kVersion);
}

void loop() {
    delay(1000);
}
