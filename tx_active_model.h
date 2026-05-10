#ifndef TX_ACTIVE_MODEL_H
#define TX_ACTIVE_MODEL_H

#include <Arduino.h>
#include <Preferences.h>

#include "espnow_tx.h"
#include "tx_model_cfg.h"

#ifndef TX_PREFS_NS
#define TX_PREFS_NS "txrun"
#endif

extern String tx_active_model_dir;

inline void tx_active_model_load_prefs() {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, true)) return;
  tx_active_model_dir = p.getString("active_m", "");
  p.end();
  tx_active_model_dir.trim();
}

inline void tx_active_model_save_prefs() {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, false)) return;
  p.putString("active_m", tx_active_model_dir);
  p.end();
}

/** Potvrdený model na vysielači — načíta model_web.json a spustí CONTROL. */
inline void tx_active_model_set(const String& dir) {
  tx_active_model_dir = dir;
  tx_active_model_dir.trim();
  tx_active_model_save_prefs();
  tx_model_cfg_bump();
  txesp_telem_clear();
}

inline void tx_active_model_clear_selection() {
  tx_active_model_dir = "";
  tx_active_model_save_prefs();
  tx_model_cfg_bump();
  txesp_telem_clear();
}

#endif
