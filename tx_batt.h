#ifndef TX_BATT_H
#define TX_BATT_H

#include <Arduino.h>

/** LOLIN S3 Pro: AD_BAT na GPIO3, delič 100k/100k → na pine ~0,5× VBAT. */
#ifndef TX_BATT_ADC_PIN
#define TX_BATT_ADC_PIN 3
#endif

inline uint16_t tx_batt_pin_mv() {
  analogReadResolution(12);
  analogSetPinAttenuation(TX_BATT_ADC_PIN, ADC_11db);
  uint32_t sum = 0;
  const int n = 6;
  for (int i = 0; i < n; i++) sum += (uint32_t)analogReadMilliVolts(TX_BATT_ADC_PIN);
  return (uint16_t)(sum / (uint32_t)n);
}

/** Odhadované napätie batérie (mV) na vstupe LiPo. */
inline uint16_t tx_batt_pack_mv() {
  uint32_t v = (uint32_t)tx_batt_pin_mv() * 2u;
  if (v > 65535u) v = 65535u;
  return (uint16_t)v;
}

#endif
