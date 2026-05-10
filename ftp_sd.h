#ifndef FTP_SD_H
#define FTP_SD_H

// Úložisko FTP (STORAGE_SD vs FFAT) určuje FtpServerKey.h v knižnici SimpleFTPServer
// pri kompilácii FtpServer.cpp — nie makrá v tomto súbore. Pre SPI SD musí byť v
// .../libraries/SimpleFTPServer/FtpServerKey.h: DEFAULT_STORAGE_TYPE_ESP32 = STORAGE_SD.

#include <FtpServer.h>

/** Jedna inštancia na celý sketch (ftp_sd.h je cez guard vrátane z jedného reťazca include). */
FtpServer transmitter_ftp;

inline void transmitter_ftp_begin(const char* user, const char* pass) {
  transmitter_ftp.begin(user, pass);
}

inline void transmitter_ftp_poll() {
  transmitter_ftp.handleFTP();
}

#endif
