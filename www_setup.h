#include "network_services.h"

  String mdns_host = network_hostname_from_ap(wifi_cfg.ap_ssid);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(mdns_host.c_str());
  WiFi.setSleep(false);

  if (wifi_cfg.ap_password.length() > 0 && wifi_cfg.ap_password.length() < 8) {
    Serial.println("VAROVANIE: AP heslo ma pod 8 znakov — softAP moze zlyhat (WPA2 potrebuje min. 8).");
  }

  Serial.print("Hostname (STA/mDNS): ");
  Serial.println(mdns_host.c_str());

  Serial.print("Spustam softAP: ");
  Serial.println(wifi_cfg.ap_ssid.c_str());
  if (!WiFi.softAP(wifi_cfg.ap_ssid.c_str(), wifi_cfg.ap_password.c_str())) {
    Serial.println("softAP zlyhalo — skontroluj SSID/heslo (dlzka hesla?).");
  } else {
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  if (wifi_cfg.sta_ssid.length() > 0) {
    Serial.print("STA pripojenie na: ");
    Serial.println(wifi_cfg.sta_ssid.c_str());
    WiFi.begin(wifi_cfg.sta_ssid.c_str(), wifi_cfg.sta_password.c_str());
    int n = 0;
    while (WiFi.status() != WL_CONNECTED && n < 40) {
      delay(250);
      Serial.print('.');
      n++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("STA IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.print("STA stav: ");
      Serial.println((int)WiFi.status());
      Serial.println("STA: nepripojene — skontroluj SSID/heslo, /config/wifi po pripojeni k AP.");
    }
  } else {
    Serial.println("STA vypnute (prazdny sta_ssid). Po AP: http://192.168.4.1/config/wifi");
  }

  if (!network_mdns_start(mdns_host)) {
    Serial.println("mDNS: sluzby http + ftp neboli ohlasene");
  }

  network_start_ftp_disk_share();

  www_initEspNowLater();
  www_begin();
  Serial.println("HTTP: / = modely, /config = SD/siet/firmware, FTP 21");
