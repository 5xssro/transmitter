#ifndef SERIAL_BOOT_H
#define SERIAL_BOOT_H

#include <Arduino.h>

// ESP32-S3 s USB CDC: pre spoľahlivý výstup v Seriálnom monitore zapni v doske
// „USB CDC On Boot: Enabled“ a správny COM port (USB JTAG/serial).
inline void serial_boot_init() {
  Serial.begin(115200);
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
  const uint32_t wait_ms = 12000;
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < wait_ms)) {
    delay(10);
  }
#endif
  delay(300);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println();
}

#endif
