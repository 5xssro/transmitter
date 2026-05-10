#ifndef SD_FS_H
#define SD_FS_H

#include <Arduino.h>

// Cesty pre SD.open / exists / mkdir majú byť vždy od koreňa *karty* (/config, /models),
// nie plné VFS (/sd/...). Arduino FS doplní bod pripojenia (/sd alebo /) sám — inak vznikne /sd/sd/...
inline String sd_path(const String& logical_abs) {
  String p = logical_abs;
  if (p.length() == 0) p = "/";
  if (!p.startsWith("/")) p = "/" + p;
  return p;
}

inline String sd_path(const char* logical_abs) {
  return sd_path(String(logical_abs ? logical_abs : ""));
}

#endif
