#ifndef WWW_MODELS_H
#define WWW_MODELS_H

#include <Arduino.h>
#include <SD.h>
#include "sd_fs.h"

#define WWW_MODEL_MAX 10

// Výhradne pre web — TFT / model.h sa nepoužívajú na WWW.
extern String www_model_names[WWW_MODEL_MAX + 1];
extern int www_model_count;
extern int www_model_selected;

/** Záznam zo SD môže byť „podadresár“, „/models/x“ alebo „1:/models/x“ — vráti len názov modelu. */
inline String www_model_normalize_entry_name(String dn) {
  dn.trim();
  while (dn.length() && (dn[0] == '/' || dn[0] == '\\')) dn = dn.substring(1);
  int slash = dn.lastIndexOf('/');
  if (slash >= 0) dn = dn.substring(slash + 1);
  if (slash < 0) {
    slash = dn.lastIndexOf('\\');
    if (slash >= 0) dn = dn.substring(slash + 1);
  }
  dn.trim();
  return dn;
}

/** Povolený názov priečinka modelu: len A–Z, a–z, 0–9, _ a - */
inline bool www_model_dir_token_valid(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    if (c >= '0' && c <= '9') continue;
    if (c == '_' || c == '-') continue;
    return false;
  }
  return true;
}

inline void www_models_scan_sd() {
  www_model_count = 0;
  String mr = sd_path("/models");
  if (!SD.exists(mr)) return;
  File root = SD.open(mr);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  for (;;) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      String dn = www_model_normalize_entry_name(String(entry.name()));
      if (dn.length() == 0) {
        entry.close();
        continue;
      }
      if (!dn.startsWith("System") && www_model_dir_token_valid(dn) && www_model_count < WWW_MODEL_MAX) {
        www_model_count++;
        www_model_names[www_model_count] = dn;
      }
    }
    entry.close();
  }
  root.close();
  for (int a = 1; a < www_model_count; a++)
    for (int b = a + 1; b <= www_model_count; b++)
      if (www_model_names[a].compareTo(www_model_names[b]) > 0) {
        String t = www_model_names[a];
        www_model_names[a] = www_model_names[b];
        www_model_names[b] = t;
      }
}

/** Je dirname v aktuálnom zozname z scanu? */
inline bool www_model_is_listed(const String& name) {
  String nn = name;
  nn.trim();
  for (int i = 1; i <= www_model_count; i++) {
    String mn = www_model_names[i];
    mn.trim();
    if (mn == nn) return true;
  }
  return false;
}

#endif
