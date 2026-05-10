#ifndef TX_RADIO_TASK_H
#define TX_RADIO_TASK_H

/**
 * ADC reading in FreeRTOS task + TXESP_CONTROL (1000-2000 us) via ESP-NOW.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include <WiFi.h>
#include <esp_now.h>

#include "model_web_cfg.h"
#include "espnow_tx.h"
#include "www_models.h"
#include "transmitter_prefs.h"
#include "config_sticks_cal.h"
#include "tx_model_cfg.h"
#include "sd_fs.h"
#include <SD.h>

#ifndef TX_STICK_PIN_STEERING
#define TX_STICK_PIN_STEERING 1
#endif
#ifndef TX_STICK_PIN_THROTTLE
#define TX_STICK_PIN_THROTTLE 2
#endif

extern volatile bool sd_mounted;
extern String www_model_session_dir;
extern String tx_active_model_dir;

#define TX_RADIO_STACK 4096
#define TX_RADIO_PRI 3
#define TX_RADIO_CORE 1

static portMUX_TYPE tx_adc_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int tx_adc_steer = 0;
static volatile int tx_adc_thr = 0;

/** ADC calibration to 1000-2000 us (PWM), before travel. */
static int tx_map_stick_us(int raw, int mn, int mid, int mx) {
  if (mx <= mn) return 1500;
  if (raw < mn) raw = mn;
  if (raw > mx) raw = mx;
  if (raw <= mid) {
    if (mid <= mn) return 1500;
    return (int)(1000 + (int64_t)(raw - mn) * 500 / (mid - mn));
  }
  if (mx <= mid) return 1500;
  return (int)(1500 + (int64_t)(raw - mid) * 500 / (mx - mid));
}

/** Travel % of half-span from center_us (0=center only, 100=full). */
static uint16_t tx_apply_travel_us(int base_us, int center_us, int travel_neg_pct, int travel_pos_pct) {
  if (center_us < 1000) center_us = 1000;
  if (center_us > 2000) center_us = 2000;
  int d = base_us - center_us;
  int t;
  if (d <= 0)
    t = center_us + (int)((int64_t)d * travel_neg_pct / 100);
  else
    t = center_us + (int)((int64_t)d * travel_pos_pct / 100);
  if (t < 1000) t = 1000;
  if (t > 2000) t = 2000;
  return (uint16_t)t;
}

static uint16_t tx_clamp_us16(int v) {
  if (v < 1000) return 1000;
  if (v > 2000) return 2000;
  return (uint16_t)v;
}

inline void tx_radio_live_adc(int* steer_raw, int* thr_raw) {
  portENTER_CRITICAL(&tx_adc_mux);
  if (steer_raw) *steer_raw = tx_adc_steer;
  if (thr_raw) *thr_raw = tx_adc_thr;
  portEXIT_CRITICAL(&tx_adc_mux);
}

static ModelWebCfg tx_cached_cfg;
static uint8_t tx_cached_rxmac[6];
static bool tx_cached_have_mac = false;
static SticksCal tx_sticks_cal{};

/** Obnoví kalibráciu palíc + model pre ESP-NOW. Pri zmene RX MAC zmaže cache telemetrie. */
static bool tx_radio_reload_cfg() {
  uint8_t prev_mac[6];
  bool had_prev = tx_cached_have_mac;
  if (had_prev) memcpy(prev_mac, tx_cached_rxmac, 6);

  tx_cached_have_mac = false;
  sticks_cal_defaults(tx_sticks_cal);
  if (sd_mounted) {
    (void)sticks_cal_load(tx_sticks_cal);
  }

  if (!sd_mounted) {
    if (had_prev) txesp_telem_clear();
    return false;
  }

  String dir = tx_active_model_dir;
  dir.trim();
  if (dir.length() == 0) {
    if (had_prev) txesp_telem_clear();
    return false;
  }

  if (!www_model_dir_token_valid(dir)) {
    if (had_prev) txesp_telem_clear();
    return false;
  }
  www_models_scan_sd();
  if (!www_model_is_listed(dir)) {
    if (had_prev) txesp_telem_clear();
    return false;
  }

  if (!model_web_cfg_load(dir, tx_cached_cfg)) model_web_cfg_defaults(tx_cached_cfg);
  String mac = tx_cached_cfg.peer_mac;
  mac.trim();
  bool new_have = false;
  if (mac.length() > 0 && tx_parse_mac_colon(mac, tx_cached_rxmac)) {
    tx_cached_have_mac = true;
    new_have = true;
  }

  bool mac_changed =
      (had_prev != new_have) || (had_prev && new_have && memcmp(prev_mac, tx_cached_rxmac, 6) != 0);
  if (mac_changed) txesp_telem_clear();
  return new_have && (!had_prev || memcmp(prev_mac, tx_cached_rxmac, 6) != 0);
}

static void tx_radio_task(void* /*arg*/) {
  (void)tx_espnow_begin();

  pinMode(TX_STICK_PIN_STEERING, INPUT);
  pinMode(TX_STICK_PIN_THROTTLE, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(TX_STICK_PIN_STEERING, ADC_11db);
  analogSetPinAttenuation(TX_STICK_PIN_THROTTLE, ADC_11db);

  int smooth_s = analogRead(TX_STICK_PIN_STEERING);
  int smooth_t = analogRead(TX_STICK_PIN_THROTTLE);

  uint32_t cfg_applied_ver = 0xffffffffu;
  unsigned loop_div = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5));

    int rs = analogRead(TX_STICK_PIN_STEERING);
    int rt = analogRead(TX_STICK_PIN_THROTTLE);
    smooth_s = (smooth_s * 7 + rs) / 8;
    smooth_t = (smooth_t * 7 + rt) / 8;

    portENTER_CRITICAL(&tx_adc_mux);
    tx_adc_steer = smooth_s;
    tx_adc_thr = smooth_t;
    portEXIT_CRITICAL(&tx_adc_mux);

    uint32_t cv = tx_model_cfg_version;
    if (cv != cfg_applied_ver) {
      bool pair_bind = tx_radio_reload_cfg();
      cfg_applied_ver = cv;
      if (pair_bind && tx_cached_have_mac)
        (void)tx_espnow_send_pair_bind(tx_cached_rxmac);
    }

    loop_div++;
    if (loop_div < 4) continue;
    loop_div = 0;

    if (!sd_mounted || !tx_cached_have_mac) continue;

    int base_s = tx_map_stick_us(smooth_s, tx_sticks_cal.steer_min, tx_sticks_cal.steer_mid, tx_sticks_cal.steer_max);
    int base_t = tx_map_stick_us(smooth_t, tx_sticks_cal.thr_min, tx_sticks_cal.thr_mid, tx_sticks_cal.thr_max);
    int trim = tx_cached_cfg.steer_trim_us;
    if (trim < 1000) trim = 1000;
    if (trim > 2000) trim = 2000;
    int adj_s = base_s + (trim - 1500);
    uint16_t su = tx_apply_travel_us(adj_s, trim, tx_cached_cfg.steer_travel_l, tx_cached_cfg.steer_travel_r);
    if (tx_cached_cfg.steer_reverse) su = tx_clamp_us16(2 * trim - (int)su);

    uint16_t tu = tx_apply_travel_us(base_t, 1500, tx_cached_cfg.thr_travel_l, tx_cached_cfg.thr_travel_r);
    if (tx_cached_cfg.thr_reverse) tu = tx_clamp_us16(3000 - (int)tu);

    (void)tx_espnow_send_control(tx_cached_rxmac, su, tu);
  }
}

inline void tx_radio_begin() {
  static bool started = false;
  if (started) return;
  started = true;
  (void)tx_espnow_begin();
  bool pair_bind = tx_radio_reload_cfg();
  if (pair_bind && tx_cached_have_mac)
    (void)tx_espnow_send_pair_bind(tx_cached_rxmac);
  BaseType_t ok = xTaskCreatePinnedToCore(tx_radio_task, "txradio", TX_RADIO_STACK, nullptr, TX_RADIO_PRI, nullptr, TX_RADIO_CORE);
  if (ok != pdPASS) {
    Serial.println(F("tx_radio: xTaskCreate zlyhalo"));
  } else {
    Serial.println(F("tx_radio: uloha ADC+ESP-NOW spustena"));
  }
}

#endif
