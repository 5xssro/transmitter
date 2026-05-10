#define SERIAL_DEBUG 1
/* LVGL: Arduino Library Manager → „lvgl“ (lvgl), verzia 8.3.x (nie 9.x). */
#include "serial_boot.h"

String DebugMessage = "";
void SerialDebug(String message){
  #ifdef SERIAL_DEBUG
    if(SERIAL_DEBUG==1){
      Serial.println(message);
    }

  #endif
}

#include "tft_sd.h"
#include "model.h"
#include "config_wifi.h"
#include "www_models.h"
#include "tx_model_cfg.h"
#include "config_sticks_cal.h"
#include "tx_active_model.h"
#include "tx_lvgl_ui.h"
#include "www.h"

#include "tx_sticks_lcd.h"

volatile bool sd_mounted = false;
String tx_active_model_dir;
volatile uint32_t tx_model_cfg_version = 0;
WifiConfig wifi_cfg;
bool wifi_cfg_loaded_from_sd = false;

String www_model_names[WWW_MODEL_MAX + 1];
int www_model_count = 0;
int www_model_selected = 0;

String www_model_session_dir;

void setup() {
  serial_boot_init();
  Serial.println("--- transmitter start ---");

  #include "tft_sd_setup.h"

  tx_active_model_load_prefs();

  if (sd_mounted) {
    sticks_cal_bootstrap_if_missing();
    wifi_cfg_ensure_dirs();
    wifi_cfg_load();
  } else {
    wifi_cfg_defaults();
  }
  wifi_cfg_log();

  #include "model_setup.h"

  tx_lvgl_ui_setup();

  #include "www_setup.h"
  tx_radio_begin();
}

void loop() {
  if (sd_mounted) transmitter_ftp_poll();
  tx_sticks_lcd_poll();

  //#include "model_loop.h"
  //#include "www_loop.h"
}
