#ifndef TX_MODEL_CFG_H
#define TX_MODEL_CFG_H

#include <Arduino.h>

extern volatile uint32_t tx_model_cfg_version;

inline void tx_model_cfg_bump() {
  tx_model_cfg_version++;
}

#endif
