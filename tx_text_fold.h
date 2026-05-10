#ifndef TX_TEXT_FOLD_H
#define TX_TEXT_FOLD_H

#include <Arduino.h>

/** UTF-8 (web/SD) → ASCII pre LCD (bez diakritiky). */
inline String tx_text_ascii_fold(const String& in) {
  String o;
  o.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length();) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80u) {
      o += (char)c;
      i++;
      continue;
    }
    if ((c & 0xE0u) == 0xC0u && i + 1 < in.length()) {
      uint8_t c1 = (uint8_t)in[i + 1];
      if ((c1 & 0xC0u) != 0x80u) {
        i++;
        continue;
      }
      uint16_t u = (uint16_t)(((uint16_t)(c & 0x1Fu) << 6) | (uint16_t)(c1 & 0x3Fu));
      i += 2;
      char rep = '?';
      switch (u) {
        case 0x00E1:
        case 0x00C1:
        case 0x00E4:
        case 0x00C4:
          rep = 'a';
          break;
        case 0x010D:
        case 0x010C:
          rep = 'c';
          break;
        case 0x010F:
        case 0x010E:
          rep = 'd';
          break;
        case 0x00E9:
        case 0x00C9:
        case 0x011B:
        case 0x011A:
          rep = 'e';
          break;
        case 0x00ED:
        case 0x00CD:
          rep = 'i';
          break;
        case 0x013E:
        case 0x013D:
        case 0x013A:
        case 0x0139:
          rep = 'l';
          break;
        case 0x0148:
        case 0x0147:
          rep = 'n';
          break;
        case 0x00F3:
        case 0x00D3:
        case 0x00F4:
        case 0x00D4:
          rep = 'o';
          break;
        case 0x0155:
        case 0x0154:
          rep = 'r';
          break;
        case 0x0161:
        case 0x0160:
          rep = 's';
          break;
        case 0x0165:
        case 0x0164:
          rep = 't';
          break;
        case 0x00FA:
        case 0x00DA:
        case 0x016F:
        case 0x016E:
          rep = 'u';
          break;
        case 0x00FD:
        case 0x00DD:
          rep = 'y';
          break;
        case 0x017E:
        case 0x017D:
          rep = 'z';
          break;
        default:
          rep = '?';
          break;
      }
      o += rep;
      continue;
    }
    if ((c & 0xF0u) == 0xE0u) {
      i += 3;
      continue;
    }
    if ((c & 0xF8u) == 0xF0u) {
      i += 4;
      continue;
    }
    i++;
  }
  return o;
}

inline String tx_text_display(const String& s) { return tx_text_ascii_fold(s); }

#endif
