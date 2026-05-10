#ifndef TX_LOG_H
#define TX_LOG_H

#include <Arduino.h>
#include "transmitter_prefs.h"

inline bool tx_serial_log_enabled() {
  return tx_prefs_get_serial_log_enabled();
}

#define TX_LOG(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    if (tx_serial_log_enabled()) Serial.printf(fmt, ##__VA_ARGS__);                                                     \
  } while (0)

#define TX_LOGLN(x)                                                                                                    \
  do {                                                                                                                 \
    if (tx_serial_log_enabled()) Serial.println(x);                                                                   \
  } while (0)

#endif
