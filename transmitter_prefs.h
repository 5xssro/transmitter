#ifndef TRANSMITTER_PREFS_H
#define TRANSMITTER_PREFS_H

#include <Arduino.h>
#include <Preferences.h>

#define TX_PREFS_NS "txrun"

inline bool tx_parse_mac_colon(const String& in, uint8_t out[6]) {
  String s = in;
  s.trim();
  if (s.length() == 0) return false;
  s.toUpperCase();
  s.replace('-', ':');
  int part = 0;
  unsigned val = 0;
  int nhex = 0;
  for (unsigned i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s[i] : ':';
    if (c == ':') {
      if (nhex < 1 || nhex > 2 || part >= 6) return false;
      out[part++] = (uint8_t)val;
      val = 0;
      nhex = 0;
      continue;
    }
    int d = -1;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c>= 'A' && c <= 'F')
      d = 10 + (c - 'A');
    else
      return false;
    val = (val << 4) | (unsigned)d;
    nhex++;
    if (nhex > 2) return false;
  }
  return part == 6;
}

inline void tx_prefs_set_last_model_dir(const String& d) {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, false)) return;
  p.putString("mdir", d);
  p.end();
}

inline String tx_prefs_get_last_model_dir() {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, true)) return "";
  String s = p.getString("mdir", "");
  p.end();
  s.trim();
  return s;
}

/** Predvolene vypnuté: výpisy cez Serial z www prepínačom. */
inline bool tx_prefs_get_serial_log_enabled() {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, true)) return false;
  bool v = p.getBool("serlog", false);
  p.end();
  return v;
}

inline void tx_prefs_set_serial_log_enabled(bool on) {
  Preferences p;
  if (!p.begin(TX_PREFS_NS, false)) return;
  p.putBool("serlog", on);
  p.end();
}

#endif
