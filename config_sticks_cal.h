#ifndef CONFIG_STICKS_CAL_H
#define CONFIG_STICKS_CAL_H

#include <Arduino.h>
#include <SD.h>
#include <limits.h>
#include "sd_fs.h"
#include "config_wifi.h"

/** Globálna kalibrácia palíc (jedna pre všetky modely). Uložené na SD: `/config/sticks_cal.json`. */
struct SticksCal {
  int steer_min;
  int steer_mid;
  int steer_max;
  int thr_min;
  int thr_mid;
  int thr_max;
};

inline String sticks_cal_vpath() {
  return "/config/sticks_cal.json";
}

inline void sticks_cal_defaults(SticksCal& c) {
  c.steer_min = 0;
  c.steer_mid = 2047;
  c.steer_max = 4095;
  c.thr_min = 0;
  c.thr_mid = 2047;
  c.thr_max = 4095;
}

inline bool sticks_cal_read_raw(String& rawOut) {
  rawOut = "";
  String vfs = sd_path(sticks_cal_vpath());
  if (!SD.exists(vfs)) return false;
  File f = SD.open(vfs, FILE_READ);
  if (!f) return false;
  rawOut.reserve((unsigned)f.size() + 4);
  while (f.available()) rawOut += (char)f.read();
  f.close();
  return rawOut.length() > 0;
}

inline bool sticks_cal_load(SticksCal& c) {
  sticks_cal_defaults(c);
  String raw;
  if (!sticks_cal_read_raw(raw)) return false;
  c.steer_min = wifi_json_get_int(raw, "steer_min", c.steer_min);
  c.steer_mid = wifi_json_get_int(raw, "steer_mid", c.steer_mid);
  c.steer_max = wifi_json_get_int(raw, "steer_max", c.steer_max);
  c.thr_min = wifi_json_get_int(raw, "thr_min", c.thr_min);
  c.thr_mid = wifi_json_get_int(raw, "thr_mid", c.thr_mid);
  c.thr_max = wifi_json_get_int(raw, "thr_max", c.thr_max);
  return true;
}

inline bool sticks_cal_save(const SticksCal& c) {
  String dcfg = sd_path("/config");
  if (!SD.exists(dcfg)) {
    if (!SD.mkdir(dcfg)) return false;
  }
  String vfs = sd_path(sticks_cal_vpath());
  File out = SD.open(vfs, FILE_WRITE, true);
  if (!out) return false;
  out.printf("{\"steer_min\":%d,\"steer_mid\":%d,\"steer_max\":%d,", c.steer_min, c.steer_mid, c.steer_max);
  out.printf("\"thr_min\":%d,\"thr_mid\":%d,\"thr_max\":%d}\n", c.thr_min, c.thr_mid, c.thr_max);
  out.close();
  return true;
}

extern volatile bool sd_mounted;

#include "www_models.h"
#include "model_web_cfg.h"
#include "transmitter_prefs.h"

/** Pri prvý beh vytvorí `sticks_cal.json` z uloženej kalibrácie v model_web.json (posledný / prvý model). */
inline void sticks_cal_bootstrap_if_missing() {
  if (!sd_mounted) return;
  if (SD.exists(sd_path(sticks_cal_vpath()))) return;
  SticksCal c;
  sticks_cal_defaults(c);
  String seed = tx_prefs_get_last_model_dir();
  seed.trim();
  www_models_scan_sd();
  if (seed.length() == 0 || !www_model_dir_token_valid(seed) || !www_model_is_listed(seed)) {
    if (www_model_count > 0) {
      seed = www_model_names[1];
      seed.trim();
    }
  }
  if (seed.length() && www_model_dir_token_valid(seed) && www_model_is_listed(seed)) {
    ModelWebCfg m;
    if (model_web_cfg_load(seed, m)) {
      c.steer_min = m.steer_min;
      c.steer_mid = m.steer_mid;
      c.steer_max = m.steer_max;
      c.thr_min = m.thr_min;
      c.thr_mid = m.thr_mid;
      c.thr_max = m.thr_max;
    }
  }
  (void)sticks_cal_save(c);
}

#endif
