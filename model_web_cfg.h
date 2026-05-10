#ifndef MODEL_WEB_CFG_H
#define MODEL_WEB_CFG_H

#include <Arduino.h>
#include <SD.h>
#include "sd_fs.h"
#include "config_wifi.h"

struct ModelWebCfg {
  String display_name;
  String peer_mac;
  int steer_min;
  int steer_mid;
  int steer_max;
  int thr_min;
  int thr_mid;
  int thr_max;
  /** Travel 0–100 % polmesačného ramena od trim/stredu (ľavá/pravá). */
  int steer_travel_l;
  int steer_travel_r;
  int thr_travel_l;
  int thr_travel_r;
  /** Riadenie: elektrický stred v µs (1000–2000, typ. 1500). */
  int steer_trim_us;
  bool steer_reverse;
  bool thr_reverse;
};

inline String model_web_cfg_vpath(const String& modelName) {
  return "/models/" + modelName + "/model_web.json";
}

inline static int model_web_cfg_clamp_travel(int v) {
  if (v < 0) return 0;
  if (v > 100) return 100;
  return v;
}

inline static int model_web_cfg_clamp_trim_us(int v) {
  if (v < 1000) return 1000;
  if (v > 2000) return 2000;
  return v;
}

inline void model_web_cfg_defaults(ModelWebCfg& c) {
  c.display_name = "";
  c.peer_mac = "";
  c.steer_min = 0;
  c.steer_mid = 2047;
  c.steer_max = 4095;
  c.thr_min = 0;
  c.thr_mid = 2047;
  c.thr_max = 4095;
  c.steer_travel_l = 100;
  c.steer_travel_r = 100;
  c.thr_travel_l = 100;
  c.thr_travel_r = 100;
  c.steer_trim_us = 1500;
  c.steer_reverse = false;
  c.thr_reverse = false;
}

inline bool model_web_cfg_ensure_file(const String& modelName) {
  String vfs = sd_path(model_web_cfg_vpath(modelName));
  if (SD.exists(vfs)) return true;
  File out = SD.open(vfs, FILE_WRITE, true);
  if (!out) return false;
  out.print("{\"display_name\":\"\",\"peer_mac\":\"\"");
  out.print(",\"steer_min\":0,\"steer_mid\":2047,\"steer_max\":4095");
  out.print(",\"thr_min\":0,\"thr_mid\":2047,\"thr_max\":4095");
  out.print(",\"steer_travel_l\":100,\"steer_travel_r\":100,\"thr_travel_l\":100,\"thr_travel_r\":100");
  out.print(",\"steer_trim_us\":1500,\"steer_reverse\":0,\"thr_reverse\":0}\n");
  out.close();
  return true;
}

inline void model_web_cfg_read_raw(const String& modelName, String& rawOut) {
  rawOut = "";
  String vfs = sd_path(model_web_cfg_vpath(modelName));
  File f = SD.open(vfs, FILE_READ);
  if (!f) return;
  rawOut.reserve((unsigned)f.size() + 4);
  while (f.available()) rawOut += (char)f.read();
  f.close();
}

inline bool model_web_cfg_load(const String& modelName, ModelWebCfg& c) {
  model_web_cfg_defaults(c);
  String raw;
  model_web_cfg_read_raw(modelName, raw);
  if (raw.length() == 0) return false;
  c.display_name = wifi_json_get_string(raw, "display_name");
  c.peer_mac = wifi_json_get_string(raw, "peer_mac");
  c.steer_min = wifi_json_get_int(raw, "steer_min", c.steer_min);
  c.steer_mid = wifi_json_get_int(raw, "steer_mid", c.steer_mid);
  c.steer_max = wifi_json_get_int(raw, "steer_max", c.steer_max);
  c.thr_min = wifi_json_get_int(raw, "thr_min", c.thr_min);
  c.thr_mid = wifi_json_get_int(raw, "thr_mid", c.thr_mid);
  c.thr_max = wifi_json_get_int(raw, "thr_max", c.thr_max);
  c.steer_travel_l = model_web_cfg_clamp_travel(wifi_json_get_int(raw, "steer_travel_l", c.steer_travel_l));
  c.steer_travel_r = model_web_cfg_clamp_travel(wifi_json_get_int(raw, "steer_travel_r", c.steer_travel_r));
  c.thr_travel_l = model_web_cfg_clamp_travel(wifi_json_get_int(raw, "thr_travel_l", c.thr_travel_l));
  c.thr_travel_r = model_web_cfg_clamp_travel(wifi_json_get_int(raw, "thr_travel_r", c.thr_travel_r));
  c.steer_trim_us = model_web_cfg_clamp_trim_us(wifi_json_get_int(raw, "steer_trim_us", c.steer_trim_us));
  c.steer_reverse = wifi_json_get_int(raw, "steer_reverse", 0) != 0;
  c.thr_reverse = wifi_json_get_int(raw, "thr_reverse", 0) != 0;
  return true;
}

inline String model_web_cfg_load_display_name(const String& modelName) {
  String raw;
  model_web_cfg_read_raw(modelName, raw);
  return wifi_json_get_string(raw, "display_name");
}

inline String model_web_cfg_load_peer_mac(const String& modelName) {
  String raw;
  model_web_cfg_read_raw(modelName, raw);
  return wifi_json_get_string(raw, "peer_mac");
}

inline String model_web_cfg_list_label(const String& modelName) {
  String d = model_web_cfg_load_display_name(modelName);
  d.trim();
  if (d.length() > 0) return d;
  return modelName;
}

inline bool model_web_cfg_save(const String& modelName, const ModelWebCfg& c) {
  String vfs = sd_path(model_web_cfg_vpath(modelName));
  File out = SD.open(vfs, FILE_WRITE, true);
  if (!out) return false;
  out.print("{\"display_name\":\"");
  wifi_json_print_escaped(out, c.display_name);
  out.print("\",\"peer_mac\":\"");
  wifi_json_print_escaped(out, c.peer_mac);
  out.printf("\",\"steer_min\":%d,\"steer_mid\":%d,\"steer_max\":%d", c.steer_min, c.steer_mid, c.steer_max);
  out.printf(",\"thr_min\":%d,\"thr_mid\":%d,\"thr_max\":%d", c.thr_min, c.thr_mid, c.thr_max);
  out.printf(",\"steer_travel_l\":%d,\"steer_travel_r\":%d,\"thr_travel_l\":%d,\"thr_travel_r\":%d",
             c.steer_travel_l, c.steer_travel_r, c.thr_travel_l, c.thr_travel_r);
  out.printf(",\"steer_trim_us\":%d,\"steer_reverse\":%d,\"thr_reverse\":%d}\n",
             c.steer_trim_us, c.steer_reverse ? 1 : 0, c.thr_reverse ? 1 : 0);
  out.close();
  return true;
}

inline bool model_web_cfg_save_fields(const String& modelName, const String& display_name, const String& peer_mac) {
  ModelWebCfg c;
  model_web_cfg_load(modelName, c);
  c.display_name = display_name;
  c.peer_mac = peer_mac;
  return model_web_cfg_save(modelName, c);
}

#endif
