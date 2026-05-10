#ifndef NETWORK_SERVICES_H
#define NETWORK_SERVICES_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config_wifi.h"
#include "ftp_sd.h"

extern volatile bool sd_mounted;

// mDNS napr. transmitter.local — len [a-z0-9-]
inline String network_hostname_from_ap(const String& ap_ssid) {
  String h;
  h.reserve(32);
  for (unsigned i = 0; i < ap_ssid.length(); i++) {
    char c = (char)ap_ssid[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (h.length() < 24) h += c;
    } else if (c == ' ' || c == '_' || c == '.') {
      if (h.length() > 0 && h.charAt(h.length() - 1) != '-') h += '-';
    }
    if (h.length() >= 24) break;
  }
  while (h.length() > 0 && h.charAt(h.length() - 1) == '-') h.remove(h.length() - 1);
  if (h.length() == 0) h = "transmitter";
  return h;
}

inline bool network_mdns_start(const String& hostname) {
  if (!MDNS.begin(hostname.c_str())) {
    Serial.println("mDNS: begin zlyhalo");
    return false;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ftp", "tcp", 21);
  Serial.print("mDNS: http://");
  Serial.print(hostname);
  Serial.println(".local  (AP aj STA v rovnakej sieti)");
  return true;
}

// FTP: rovnaké heslo ako AP (ak je kratšie ako 4 zn., fallback 12345678)
inline void network_start_ftp_disk_share() {
  if (!sd_mounted) {
    Serial.println("FTP: vypnute — nie je SD");
    return;
  }
  const char* user = "ftp";
  const char* pass =
      (wifi_cfg.ap_password.length() >= 4) ? wifi_cfg.ap_password.c_str() : "12345678";
  // PASV musí ohlásiť správnu IP (AP 192.168.4.1, inak Windows/Linux otvoria dátový kanál zle).
  if (WiFi.getMode() & WIFI_AP) {
    transmitter_ftp.setLocalIp(WiFi.softAPIP());
  } else if (WiFi.status() == WL_CONNECTED) {
    transmitter_ftp.setLocalIp(WiFi.localIP());
  }
  transmitter_ftp_begin(user, pass);
  Serial.print("FTP: port 21, uzivatel ");
  Serial.print(user);
  Serial.print(", heslo = ako AP heslo (min. 4 zn.; inak 12345678). IP napr. ");
  Serial.println(WiFi.softAPIP());
}

#endif
