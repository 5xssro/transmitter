LCD grafika pre vysielač (WiFi ikony — 5 paličiek, wifi_off so šikmým krížom)

Na SD skopíruj tieto súbory do priečinka (od koreňa karty FAT):

  /lcd/wifi_off.bmp
  /lcd/wifi_1.bmp
  /lcd/wifi_2.bmp
  /lcd/wifi_3.bmp
  /lcd/wifi_4.bmp
  /lcd/wifi_5.bmp

Môžeš skopírovať celý priečinok sd_assets/lcd z projektu a na karte ho premenovať na „lcd“ (alebo len vytvoriť /lcd/ a súbory doň dať).

Bez týchto súborov firmware zobrazí v pravom hornom rohu textovú náhradu (WiFi / WiFi X).

UTF-8 text na LCD: skopíruj aj /fonts/tx_ui14.vlw z sd_assets/fonts/ a v TFT_eSPI User_Setup.h pridaj #define SMOOTH_FONT (podrobnosti v sd_assets/fonts/README.txt).