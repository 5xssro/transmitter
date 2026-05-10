#ifndef CONFIG_WIFI_H
#define CONFIG_WIFI_H

#include <Arduino.h>
#include <limits.h>
#include <SD.h>
#include "sd_fs.h"

struct WifiConfig {
  String ap_ssid;
  String ap_password;
  String sta_ssid;
  String sta_password;
};

extern WifiConfig wifi_cfg;
extern bool wifi_cfg_loaded_from_sd;

inline void wifi_cfg_defaults() {
  wifi_cfg.ap_ssid = "transmitter";
  wifi_cfg.ap_password = "12345678";
  wifi_cfg.sta_ssid = "";
  wifi_cfg.sta_password = "";
  wifi_cfg_loaded_from_sd = false;
}

inline bool wifi_cfg_ensure_dirs() {
  String dcfg = sd_path("/config");
  if (!SD.exists(dcfg)) {
    if (!SD.mkdir(dcfg)) return false;
  }
  (void)SD.mkdir(sd_path("/models"));
  return SD.exists(dcfg);
}

inline void wifi_json_print_escaped(File& out, const String& s) {
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out.print('\\');
      out.print(c);
    } else if (c == '\n')
      out.print("\\n");
    else if (c == '\r')
      out.print("\\r");
    else if (c == '\t')
      out.print("\\t");
    else
      out.print(c);
  }
}

inline static void wifi_json_skip_ws(const String& j, int& p) {
  while (p < (int)j.length()) {
    char c = j.charAt((unsigned)p);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
      p++;
    else
      break;
  }
}

// Jednoduchý výrez "value" z JSON pre daný kľúč (súbor zapisujeme sami, formát je predvídateľný).
inline static String wifi_json_get_string(const String& j, const char* key) {
  String needle = String("\"") + key + "\"";
  int p = j.indexOf(needle);
  if (p < 0) return "";
  p += needle.length();
  wifi_json_skip_ws(j, p);
  if (p >= (int)j.length() || j.charAt((unsigned)p) != ':') return "";
  p++;
  wifi_json_skip_ws(j, p);
  if (p >= (int)j.length() || j.charAt((unsigned)p) != '"') return "";
  p++;
  String out;
  while (p < (int)j.length()) {
    char c = j.charAt((unsigned)p);
    if (c == '"') break;
    if (c == '\\' && p + 1 < (int)j.length()) {
      char n = j.charAt((unsigned)p + 1);
      if (n == '"') {
        out += '"';
        p += 2;
        continue;
      }
      if (n == '\\') {
        out += '\\';
        p += 2;
        continue;
      }
      if (n == 'n') {
        out += '\n';
        p += 2;
        continue;
      }
      if (n == 'r') {
        out += '\r';
        p += 2;
        continue;
      }
      if (n == 't') {
        out += '\t';
        p += 2;
        continue;
      }
      p++;
      continue;
    }
    out += c;
    p++;
  }
  return out;
}

/** Jednoduché celé číslo z JSON (môže byť záporné). */
inline int wifi_json_get_int(const String& j, const char* key, int defaultVal) {
  String needle = String("\"") + key + "\"";
  int p = j.indexOf(needle);
  if (p < 0) return defaultVal;
  p += needle.length();
  wifi_json_skip_ws(j, p);
  if (p >= (int)j.length() || j.charAt((unsigned)p) != ':') return defaultVal;
  p++;
  wifi_json_skip_ws(j, p);
  bool neg = false;
  if (p < (int)j.length() && j.charAt((unsigned)p) == '-') {
    neg = true;
    p++;
  }
  long v = 0;
  bool any = false;
  while (p < (int)j.length()) {
    char c = j.charAt((unsigned)p);
    if (c >= '0' && c <= '9') {
      v = v * 10L + (long)(c - '0');
      any = true;
      p++;
      continue;
    }
    break;
  }
  if (!any) return defaultVal;
  if (neg) v = -v;
  if (v > (long)INT_MAX) v = INT_MAX;
  if (v < (long)INT_MIN) v = INT_MIN;
  return (int)v;
}

inline bool wifi_cfg_save() {
  String dcfg = sd_path("/config");
  String fcfg = sd_path("/config/config.json");
  if (!SD.exists(dcfg)) {
    if (!SD.mkdir(dcfg)) return false;
  }
  // create=true: vytvorí chýbajúce priečinky v ceste (ESP32 FS)
  File out = SD.open(fcfg, FILE_WRITE, true);
  if (!out) return false;
  out.println("{");
  out.print("  \"ap_ssid\": \"");
  wifi_json_print_escaped(out, wifi_cfg.ap_ssid);
  out.println("\",");
  out.print("  \"ap_password\": \"");
  wifi_json_print_escaped(out, wifi_cfg.ap_password);
  out.println("\",");
  out.print("  \"sta_ssid\": \"");
  wifi_json_print_escaped(out, wifi_cfg.sta_ssid);
  out.println("\",");
  out.print("  \"sta_password\": \"");
  wifi_json_print_escaped(out, wifi_cfg.sta_password);
  out.println("\"");
  out.println("}");
  out.close();
  return true;
}

inline bool wifi_cfg_load() {
  wifi_cfg_defaults();
  String fcfg = sd_path("/config/config.json");
  if (!SD.exists(fcfg)) {
    Serial.printf("Konfig: %s neexistuje — predvolene transmitter / 12345678.\n", fcfg.c_str());
    return false;
  }
  File f = SD.open(fcfg, FILE_READ);
  if (!f) {
    Serial.printf("Konfig: nepodarilo sa otvorit %s\n", fcfg.c_str());
    return false;
  }
  String raw;
  raw.reserve((unsigned)f.size() + 8);
  while (f.available()) raw += (char)f.read();
  f.close();

  if (raw.indexOf("\"ap_ssid\"") >= 0) wifi_cfg.ap_ssid = wifi_json_get_string(raw, "ap_ssid");
  if (raw.indexOf("\"ap_password\"") >= 0) wifi_cfg.ap_password = wifi_json_get_string(raw, "ap_password");
  if (raw.indexOf("\"sta_ssid\"") >= 0) wifi_cfg.sta_ssid = wifi_json_get_string(raw, "sta_ssid");
  if (raw.indexOf("\"sta_password\"") >= 0) wifi_cfg.sta_password = wifi_json_get_string(raw, "sta_password");

  wifi_cfg_loaded_from_sd = true;
  Serial.println("Konfig: nacitane z SD (config.json).");
  return true;
}

inline void wifi_cfg_log() {
  Serial.println("--- WiFi konfig (bez hesiel) ---");
  Serial.printf("AP SSID: %s\n", wifi_cfg.ap_ssid.c_str());
  Serial.printf("AP heslo: dlzka %u znakov\n", (unsigned)wifi_cfg.ap_password.length());
  Serial.printf("STA SSID: %s\n", wifi_cfg.sta_ssid.c_str());
  Serial.printf("STA heslo: dlzka %u znakov\n", (unsigned)wifi_cfg.sta_password.length());
  Serial.println("---------------------------------");
}

#endif
