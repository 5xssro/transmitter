#ifndef TX_LVGL_UI_H
#define TX_LVGL_UI_H

/*
 * LVGL UI (240×320). Výber modelu: lv_roller so štandardnou fade maskou (LVGL 8),
 * menší font (LV_PART_MAIN) vs. väčší vybraný riadok (LV_PART_SELECTED).
 * Diakritika: tx_font_ui_14 / tx_font_ui_18 (Arial subset + WiFi / varovanie z FA).
 */
#include "lv_conf.h"
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>

#if (LVGL_VERSION_MAJOR != 8)
#error Tento skica vyzaduje LVGL 8.3.x (v Spravci kniznic zvol 8.3.x; v9 ma ine API).
#endif

#if !LV_USE_ROLLER
#error Zapni LV_USE_ROLLER 1 v lv_conf.h (aj kopiu v libraries/).
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include "tft_sd.h"
#include "tx_batt.h"
#include "tx_active_model.h"
#include "www_models.h"
#include "model_web_cfg.h"
#include "transmitter_prefs.h"
#include "sd_fs.h"
#include "tx_radio_task.h"
#include <SD.h>

LV_FONT_DECLARE(tx_font_ui_14);
LV_FONT_DECLARE(tx_font_ui_18);

#ifndef TX_ENC_PIN_A
#define TX_ENC_PIN_A 41
#endif
#ifndef TX_ENC_PIN_B
#define TX_ENC_PIN_B 42
#endif
#ifndef TX_ENC_BTN_PIN
#define TX_ENC_BTN_PIN 15
#endif
#ifndef TX_UI_LONGPRESS_MS
#define TX_UI_LONGPRESS_MS 2000
#endif

#ifndef TX_LV_ROLLER_BUF
#define TX_LV_ROLLER_BUF 700
#endif

#ifndef TX_LV_DISP_BUF_STRIDE
#define TX_LV_DISP_BUF_STRIDE 320
#endif
#ifndef TX_LV_DISP_BUF_LINES
#define TX_LV_DISP_BUF_LINES 48
#endif

static volatile int32_t tx_lv_enc_accum = 0;
static volatile uint8_t tx_lv_enc_prev_ab = 0;

static const int8_t TX_LV_ENC_TBL[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR tx_lvgl_encoder_isr(void) {
  uint8_t a = digitalRead(TX_ENC_PIN_A) ? 1u : 0u;
  uint8_t b = digitalRead(TX_ENC_PIN_B) ? 1u : 0u;
  uint8_t s = (uint8_t)((a << 1) | b);
  uint8_t idx = (uint8_t)((tx_lv_enc_prev_ab << 2) | s);
  tx_lv_enc_accum += TX_LV_ENC_TBL[idx & 15u];
  tx_lv_enc_prev_ab = s;
}

static int tx_lvgl_take_encoder_steps(void) {
  int steps = 0;
  noInterrupts();
  int32_t v = tx_lv_enc_accum;
  while (v >= 4) {
    steps++;
    v -= 4;
  }
  while (v <= -4) {
    steps--;
    v += 4;
  }
  tx_lv_enc_accum = v;
  interrupts();
  return steps;
}

enum TxLvUiMode : uint8_t { TX_LV_PICK = 0, TX_LV_ACTIVE = 1 };

static TxLvUiMode tx_lv_mode = TX_LV_PICK;
static int tx_lv_pick_idx = 1;
static uint32_t tx_lv_last_full_ms = 0;
static uint32_t tx_lv_btn_press_start = 0;
static uint8_t tx_lv_btn_prev_low = 0;

static lv_obj_t* tx_lv_bar = nullptr;
static lv_obj_t* tx_lv_lbl_batt = nullptr;
static lv_obj_t* tx_lv_lbl_ip = nullptr;
static lv_obj_t* tx_lv_lbl_wifi = nullptr;

static lv_obj_t* tx_lv_cont_active = nullptr;
static lv_obj_t* tx_lv_card_model = nullptr;
static lv_obj_t* tx_lv_lbl_model = nullptr;
static lv_obj_t* tx_lv_lbl_sticks = nullptr;

static lv_obj_t* tx_lv_cont_pick = nullptr;
static lv_obj_t* tx_lv_lbl_pick_title = nullptr;
static lv_obj_t* tx_lv_roller = nullptr;

static char tx_lv_roller_opts[TX_LV_ROLLER_BUF];

static lv_timer_t* tx_lv_timer_status = nullptr;

static lv_disp_draw_buf_t tx_lv_disp_buf;
static lv_color_t tx_lv_buf1[(size_t)TX_LV_DISP_BUF_STRIDE * TX_LV_DISP_BUF_LINES];
static lv_disp_drv_t tx_lv_disp_drv;

static bool tx_lv_model_dir_valid(const String& d) {
  if (!sd_mounted || d.length() == 0) return false;
  if (!www_model_dir_token_valid(d)) return false;
  www_models_scan_sd();
  return www_model_is_listed(d);
}

static int tx_lv_wifi_level(void) {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int r = WiFi.RSSI();
  if (r >= -50) return 5;
  if (r >= -60) return 4;
  if (r >= -70) return 3;
  if (r >= -78) return 2;
  return 1;
}

/** UTF-8 názov modelu z SD (diakritika ak je v cfg). */
static String tx_lv_title_for_index(int idx) {
  if (www_model_count <= 0) return String("-");
  if (idx < 1 || idx > www_model_count) return String("-");
  return model_web_cfg_list_label(www_model_names[idx]);
}

#if LV_DRAW_COMPLEX
static void tx_lv_roller_mask_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* obj = lv_event_get_target(e);

  static int16_t mask_top_id = -1;
  static int16_t mask_bottom_id = -1;

  if (code == LV_EVENT_COVER_CHECK) {
    lv_event_set_cover_res(e, LV_COVER_RES_MASKED);
  } else if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
    const lv_font_t* font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    lv_coord_t line_space = lv_obj_get_style_text_line_space(obj, LV_PART_MAIN);
    lv_coord_t font_h = lv_font_get_line_height(font);

    lv_area_t roller_coords;
    lv_obj_get_coords(obj, &roller_coords);

    lv_area_t rect_area;
    rect_area.x1 = roller_coords.x1;
    rect_area.x2 = roller_coords.x2;
    rect_area.y1 = roller_coords.y1;
    rect_area.y2 = roller_coords.y1 + (lv_obj_get_height(obj) - font_h - line_space) / 2;

    lv_draw_mask_fade_param_t* fade_mask_top =
        (lv_draw_mask_fade_param_t*)lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
    lv_draw_mask_fade_init(fade_mask_top, &rect_area, LV_OPA_TRANSP, rect_area.y1, LV_OPA_COVER, rect_area.y2);
    mask_top_id = lv_draw_mask_add(fade_mask_top, NULL);

    rect_area.y1 = rect_area.y2 + font_h + line_space - 1;
    rect_area.y2 = roller_coords.y2;

    lv_draw_mask_fade_param_t* fade_mask_bottom =
        (lv_draw_mask_fade_param_t*)lv_mem_buf_get(sizeof(lv_draw_mask_fade_param_t));
    lv_draw_mask_fade_init(fade_mask_bottom, &rect_area, LV_OPA_COVER, rect_area.y1, LV_OPA_TRANSP, rect_area.y2);
    mask_bottom_id = lv_draw_mask_add(fade_mask_bottom, NULL);
  } else if (code == LV_EVENT_DRAW_POST_END) {
    lv_draw_mask_fade_param_t* fade_mask_top = (lv_draw_mask_fade_param_t*)lv_draw_mask_remove_id(mask_top_id);
    lv_draw_mask_fade_param_t* fade_mask_bottom = (lv_draw_mask_fade_param_t*)lv_draw_mask_remove_id(mask_bottom_id);
    lv_draw_mask_free_param(fade_mask_top);
    lv_draw_mask_free_param(fade_mask_bottom);
    lv_mem_buf_release(fade_mask_top);
    lv_mem_buf_release(fade_mask_bottom);
    mask_top_id = -1;
    mask_bottom_id = -1;
  }
}
#endif

static void tx_lvgl_flush_cb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
  (void)disp_drv;
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)color_p, (uint32_t)(w * h), true);
  tft.endWrite();
  lv_disp_flush_ready(disp_drv);
}

static void tx_lvgl_status_update(void) {
  if (!tx_lv_lbl_batt || !tx_lv_lbl_ip || !tx_lv_lbl_wifi) return;

  uint16_t bv = tx_batt_pack_mv();
  char vb[14];
  snprintf(vb, sizeof(vb), "%u.%uV", (unsigned)(bv / 1000), (unsigned)((bv % 1000) / 100));
  lv_label_set_text(tx_lv_lbl_batt, vb);

  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  lv_label_set_text(tx_lv_lbl_ip, ip.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    int lv = tx_lv_wifi_level();
    char wbuf[16];
    snprintf(wbuf, sizeof(wbuf), "%s %d", LV_SYMBOL_WIFI, lv);
    lv_label_set_text(tx_lv_lbl_wifi, wbuf);
    lv_obj_set_style_text_color(tx_lv_lbl_wifi, lv_color_hex(0x55CC55), 0);
  } else {
    lv_label_set_text(tx_lv_lbl_wifi, LV_SYMBOL_WARNING " AP");
    lv_obj_set_style_text_color(tx_lv_lbl_wifi, lv_color_hex(0xCC9040), 0);
  }

  if (tx_lv_bar) lv_obj_invalidate(tx_lv_bar);
}

static void tx_lvgl_status_cb(lv_timer_t* t) {
  (void)t;
  tx_lvgl_status_update();
}

static void tx_lv_roller_sync_pick_from_widget(void) {
  if (!tx_lv_roller || www_model_count <= 0) return;
  uint16_t sel = lv_roller_get_selected(tx_lv_roller);
  tx_lv_pick_idx = (int)sel + 1;
}

static void tx_lv_roller_rebuild_and_select(void) {
  if (!tx_lv_roller) return;
  www_models_scan_sd();

  if (www_model_count <= 0) {
    lv_roller_set_options(tx_lv_roller, "-", LV_ROLLER_MODE_NORMAL);
    return;
  }

  if (tx_lv_pick_idx < 1) tx_lv_pick_idx = 1;
  if (tx_lv_pick_idx > www_model_count) tx_lv_pick_idx = www_model_count;

  size_t pos = 0;
  tx_lv_roller_opts[0] = '\0';
  for (int i = 1; i <= www_model_count; i++) {
    if (i > 1) {
      if (pos + 1 >= sizeof(tx_lv_roller_opts)) break;
      tx_lv_roller_opts[pos++] = '\n';
    }
    String t = tx_lv_title_for_index(i);
    const char* p = t.c_str();
    while (*p && pos < sizeof(tx_lv_roller_opts) - 1) {
      tx_lv_roller_opts[pos++] = *p++;
    }
  }
  tx_lv_roller_opts[pos] = '\0';

  lv_roller_set_options(tx_lv_roller, tx_lv_roller_opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(tx_lv_roller, (uint16_t)(tx_lv_pick_idx - 1), LV_ANIM_OFF);
}

static void tx_lvgl_refresh_active_title(void) {
  if (!tx_lv_lbl_model || tx_lv_mode != TX_LV_ACTIVE) return;
  www_models_scan_sd();
  String title = "-";
  if (www_model_count > 0 && tx_active_model_dir.length()) {
    for (int i = 1; i <= www_model_count; i++) {
      if (www_model_names[i] == tx_active_model_dir) {
        title = tx_lv_title_for_index(i);
        break;
      }
    }
  }
  lv_label_set_text(tx_lv_lbl_model, title.c_str());
  if (tx_lv_card_model) {
    lv_obj_update_layout(tx_lv_card_model);
    lv_obj_invalidate(tx_lv_lbl_model);
    lv_obj_invalidate(tx_lv_card_model);
  }
}

static void tx_lvgl_apply_mode_ui(void) {
  if (!tx_lv_cont_active || !tx_lv_cont_pick) return;
  if (tx_lv_mode == TX_LV_ACTIVE) {
    lv_obj_clear_flag(tx_lv_cont_active, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tx_lv_cont_pick, LV_OBJ_FLAG_HIDDEN);
    tx_lvgl_refresh_active_title();
  } else {
    lv_obj_add_flag(tx_lv_cont_active, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(tx_lv_cont_pick, LV_OBJ_FLAG_HIDDEN);
    tx_lv_roller_rebuild_and_select();
  }
  if (tx_lv_cont_active) lv_obj_invalidate(tx_lv_cont_active);
  if (tx_lv_cont_pick) lv_obj_invalidate(tx_lv_cont_pick);
  if (tx_lv_bar) lv_obj_invalidate(tx_lv_bar);
}

static void tx_lvgl_build_ui(void) {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_disp_t* d = const_cast<lv_disp_t*>(lv_disp_get_default());
  const lv_coord_t w = lv_disp_get_hor_res(d);
  const lv_coord_t h = lv_disp_get_ver_res(d);
  const lv_coord_t bar_h = 28;

  tx_lv_bar = lv_obj_create(scr);
  lv_obj_remove_style_all(tx_lv_bar);
  lv_obj_set_size(tx_lv_bar, w, bar_h);
  lv_obj_align(tx_lv_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(tx_lv_bar, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tx_lv_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_left(tx_lv_bar, 4, 0);
  lv_obj_set_style_pad_right(tx_lv_bar, 4, 0);
  lv_obj_set_style_pad_top(tx_lv_bar, 4, 0);
  lv_obj_set_style_pad_bottom(tx_lv_bar, 2, 0);
  lv_obj_clear_flag(tx_lv_bar, LV_OBJ_FLAG_SCROLLABLE);

  tx_lv_lbl_batt = lv_label_create(tx_lv_bar);
  lv_label_set_long_mode(tx_lv_lbl_batt, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(tx_lv_lbl_batt, &tx_font_ui_14, 0);
  lv_obj_set_style_text_color(tx_lv_lbl_batt, lv_color_hex(0xC6E050), 0);
  lv_label_set_text(tx_lv_lbl_batt, "-.-V");
  lv_obj_align(tx_lv_lbl_batt, LV_ALIGN_LEFT_MID, 0, 0);

  tx_lv_lbl_wifi = lv_label_create(tx_lv_bar);
  lv_label_set_long_mode(tx_lv_lbl_wifi, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(tx_lv_lbl_wifi, &tx_font_ui_14, 0);
  lv_label_set_text(tx_lv_lbl_wifi, LV_SYMBOL_WIFI);
  lv_obj_align(tx_lv_lbl_wifi, LV_ALIGN_RIGHT_MID, 0, 0);

  tx_lv_lbl_ip = lv_label_create(tx_lv_bar);
  lv_label_set_long_mode(tx_lv_lbl_ip, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(tx_lv_lbl_ip, &tx_font_ui_14, 0);
  lv_obj_set_style_text_color(tx_lv_lbl_ip, lv_color_hex(0xE0D040), 0);
  lv_label_set_text(tx_lv_lbl_ip, "0.0.0.0");
  lv_obj_align(tx_lv_lbl_ip, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(tx_lv_lbl_ip, (lv_coord_t)(w - 100));

  tx_lv_cont_active = lv_obj_create(scr);
  lv_obj_remove_style_all(tx_lv_cont_active);
  lv_obj_set_size(tx_lv_cont_active, w, (lv_coord_t)(h - bar_h));
  lv_obj_align(tx_lv_cont_active, LV_ALIGN_TOP_MID, 0, bar_h);
  lv_obj_set_style_bg_opa(tx_lv_cont_active, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(tx_lv_cont_active, LV_OBJ_FLAG_SCROLLABLE);

  tx_lv_card_model = lv_obj_create(tx_lv_cont_active);
  lv_obj_set_width(tx_lv_card_model, (lv_coord_t)(w - 16));
  lv_obj_set_height(tx_lv_card_model, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(tx_lv_card_model, lv_color_hex(0x1A1A22), 0);
  lv_obj_set_style_bg_opa(tx_lv_card_model, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(tx_lv_card_model, lv_color_hex(0x5E5E6A), 0);
  lv_obj_set_style_border_width(tx_lv_card_model, 1, 0);
  lv_obj_set_style_radius(tx_lv_card_model, 8, 0);
  lv_obj_set_style_pad_top(tx_lv_card_model, 10, 0);
  lv_obj_set_style_pad_bottom(tx_lv_card_model, 10, 0);
  lv_obj_set_style_pad_left(tx_lv_card_model, 8, 0);
  lv_obj_set_style_pad_right(tx_lv_card_model, 8, 0);
  lv_obj_clear_flag(tx_lv_card_model, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(tx_lv_card_model, LV_ALIGN_TOP_MID, 0, 6);

  tx_lv_lbl_model = lv_label_create(tx_lv_card_model);
  lv_label_set_long_mode(tx_lv_lbl_model, LV_LABEL_LONG_DOT);
  lv_obj_set_style_bg_opa(tx_lv_lbl_model, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_color(tx_lv_lbl_model, lv_color_hex(0xF0F0F4), 0);
  lv_obj_set_style_text_font(tx_lv_lbl_model, &tx_font_ui_18, 0);
  lv_obj_set_style_text_align(tx_lv_lbl_model, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(tx_lv_lbl_model, (lv_coord_t)(w - 40));
  {
    lv_coord_t lh = lv_font_get_line_height(&tx_font_ui_18);
    lv_obj_set_height(tx_lv_lbl_model, (lv_coord_t)(lh + 4));
  }
  lv_label_set_text(tx_lv_lbl_model, "-");
  lv_obj_align(tx_lv_lbl_model, LV_ALIGN_CENTER, 0, 0);

  tx_lv_lbl_sticks = lv_label_create(tx_lv_cont_active);
  lv_label_set_long_mode(tx_lv_lbl_sticks, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(tx_lv_lbl_sticks, &tx_font_ui_14, 0);
  lv_obj_set_style_text_color(tx_lv_lbl_sticks, lv_color_hex(0x55CC55), 0);
  lv_obj_set_style_bg_opa(tx_lv_lbl_sticks, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tx_lv_lbl_sticks, lv_color_hex(0x000000), 0);
  lv_label_set_text(tx_lv_lbl_sticks, "ST ----  TH ----");
  lv_obj_set_width(tx_lv_lbl_sticks, (lv_coord_t)(w - 12));
  lv_obj_align(tx_lv_lbl_sticks, LV_ALIGN_BOTTOM_MID, 0, -6);

  tx_lv_cont_pick = lv_obj_create(scr);
  lv_obj_remove_style_all(tx_lv_cont_pick);
  lv_obj_set_size(tx_lv_cont_pick, w, (lv_coord_t)(h - bar_h));
  lv_obj_align(tx_lv_cont_pick, LV_ALIGN_TOP_MID, 0, bar_h);
  lv_obj_set_style_bg_opa(tx_lv_cont_pick, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(tx_lv_cont_pick, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tx_lv_cont_pick, LV_OBJ_FLAG_HIDDEN);

  tx_lv_lbl_pick_title = lv_label_create(tx_lv_cont_pick);
  lv_label_set_long_mode(tx_lv_lbl_pick_title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_bg_color(tx_lv_lbl_pick_title, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tx_lv_lbl_pick_title, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(tx_lv_lbl_pick_title, &tx_font_ui_18, 0);
  lv_obj_set_style_text_color(tx_lv_lbl_pick_title, lv_color_hex(0xE8E8EE), 0);
  lv_obj_set_style_text_align(tx_lv_lbl_pick_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(tx_lv_lbl_pick_title, "Výber modelu");
  lv_obj_set_width(tx_lv_lbl_pick_title, w - 8);
  lv_obj_set_height(tx_lv_lbl_pick_title, (lv_coord_t)(lv_font_get_line_height(&tx_font_ui_18) + 14));
  lv_obj_align(tx_lv_lbl_pick_title, LV_ALIGN_TOP_MID, 0, 2);

  tx_lv_roller = lv_roller_create(tx_lv_cont_pick);
  lv_obj_set_width(tx_lv_roller, (lv_coord_t)(w - 10));
  lv_obj_set_style_bg_color(tx_lv_roller, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tx_lv_roller, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tx_lv_roller, 0, 0);
  lv_obj_set_style_pad_all(tx_lv_roller, 2, 0);
  lv_obj_set_style_text_color(tx_lv_roller, lv_color_hex(0x9898A8), LV_PART_MAIN);
  lv_obj_set_style_text_font(tx_lv_roller, &tx_font_ui_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(tx_lv_roller, lv_color_hex(0xF2F2F8), LV_PART_SELECTED);
  lv_obj_set_style_text_font(tx_lv_roller, &tx_font_ui_18, LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(tx_lv_roller, LV_OPA_30, LV_PART_SELECTED);
  lv_roller_set_visible_row_count(tx_lv_roller, 5);
  lv_obj_align(tx_lv_roller, LV_ALIGN_CENTER, 0, 10);
#if LV_DRAW_COMPLEX
  lv_obj_add_event_cb(tx_lv_roller, tx_lv_roller_mask_cb, LV_EVENT_ALL, NULL);
#endif

  tx_lv_timer_status = lv_timer_create(tx_lvgl_status_cb, 400, nullptr);
  (void)tx_lv_timer_status;
}

inline void tx_lvgl_init_display(void) {
  lv_init();
  const uint32_t tw = (uint32_t)tft.width();
  const uint32_t stripe = tw * (uint32_t)TX_LV_DISP_BUF_LINES;
  const uint32_t cap = (uint32_t)(sizeof(tx_lv_buf1) / sizeof(tx_lv_buf1[0]));
  const uint32_t use = (stripe > 0 && stripe <= cap) ? stripe : cap;
  lv_disp_draw_buf_init(&tx_lv_disp_buf, tx_lv_buf1, nullptr, use);

  lv_disp_drv_init(&tx_lv_disp_drv);
  tx_lv_disp_drv.hor_res = (lv_coord_t)tft.width();
  tx_lv_disp_drv.ver_res = (lv_coord_t)tft.height();
  tx_lv_disp_drv.flush_cb = tx_lvgl_flush_cb;
  tx_lv_disp_drv.draw_buf = &tx_lv_disp_buf;
  lv_disp_drv_register(&tx_lv_disp_drv);
}

inline void tx_lvgl_ui_setup(void) {
  pinMode(TX_ENC_PIN_A, INPUT_PULLUP);
  pinMode(TX_ENC_PIN_B, INPUT_PULLUP);
  pinMode(TX_ENC_BTN_PIN, INPUT_PULLUP);
  tx_lv_enc_prev_ab =
      (uint8_t)((digitalRead(TX_ENC_PIN_A) ? 1u : 0u) << 1) | (uint8_t)(digitalRead(TX_ENC_PIN_B) ? 1u : 0u);
  attachInterrupt(digitalPinToInterrupt(TX_ENC_PIN_A), tx_lvgl_encoder_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(TX_ENC_PIN_B), tx_lvgl_encoder_isr, CHANGE);

  www_models_scan_sd();
  if (tx_lv_model_dir_valid(tx_active_model_dir))
    tx_lv_mode = TX_LV_ACTIVE;
  else
    tx_lv_mode = TX_LV_PICK;

  if (www_model_count <= 0) {
    tx_lv_pick_idx = 1;
  } else {
    tx_lv_pick_idx = 1;
    for (int i = 1; i <= www_model_count; i++) {
      if (www_model_names[i] == tx_active_model_dir) {
        tx_lv_pick_idx = i;
        break;
      }
    }
    if (tx_active_model_dir.length() == 0) {
      String last = tx_prefs_get_last_model_dir();
      last.trim();
      if (last.length()) {
        for (int i = 1; i <= www_model_count; i++) {
          if (www_model_names[i] == last) {
            tx_lv_pick_idx = i;
            break;
          }
        }
      }
    }
  }

  tx_lvgl_init_display();
  tx_lvgl_build_ui();
  tx_lvgl_status_update();
  tx_lvgl_apply_mode_ui();
}

inline void tx_lvgl_ui_poll(void) {
  uint32_t now = millis();

  if (sd_mounted && tx_lv_mode == TX_LV_ACTIVE && !tx_lv_model_dir_valid(tx_active_model_dir)) {
    tx_active_model_clear_selection();
    tx_lv_mode = TX_LV_PICK;
  }

  int enc = tx_lvgl_take_encoder_steps();
  if (tx_lv_mode == TX_LV_PICK && enc != 0 && www_model_count > 0 && tx_lv_roller) {
    int n = www_model_count;
    int sel = (int)lv_roller_get_selected(tx_lv_roller);
    sel += enc;
    while (sel < 0) sel += n;
    while (sel >= n) sel -= n;
    lv_roller_set_selected(tx_lv_roller, (uint16_t)sel, LV_ANIM_ON);
    tx_lv_pick_idx = sel + 1;
  }

  uint8_t btnLow = (digitalRead(TX_ENC_BTN_PIN) == LOW) ? 1u : 0u;
  if (btnLow) {
    if (!tx_lv_btn_prev_low) tx_lv_btn_press_start = now;
  } else {
    if (tx_lv_btn_prev_low) {
      uint32_t dur = now - tx_lv_btn_press_start;
      if (tx_lv_mode == TX_LV_ACTIVE && dur >= TX_UI_LONGPRESS_MS) {
        www_models_scan_sd();
        if (www_model_count > 0 && tx_active_model_dir.length()) {
          for (int i = 1; i <= www_model_count; i++) {
            if (www_model_names[i] == tx_active_model_dir) {
              tx_lv_pick_idx = i;
              break;
            }
          }
        }
        tx_active_model_clear_selection();
        tx_lv_mode = TX_LV_PICK;
        tx_lvgl_apply_mode_ui();
      } else if (dur < TX_UI_LONGPRESS_MS && tx_lv_mode == TX_LV_PICK && www_model_count > 0) {
        tx_lv_roller_sync_pick_from_widget();
        String pick = www_model_names[tx_lv_pick_idx];
        tx_active_model_set(pick);
        tx_lv_mode = TX_LV_ACTIVE;
        tx_lvgl_apply_mode_ui();
      }
    }
  }
  tx_lv_btn_prev_low = btnLow;

  bool need_refresh = false;
  if (tx_lv_last_full_ms == 0) need_refresh = true;
  static TxLvUiMode prev_m = TX_LV_PICK;
  if (tx_lv_mode != prev_m) {
    prev_m = tx_lv_mode;
    need_refresh = true;
  }
  if (need_refresh) {
    tx_lvgl_apply_mode_ui();
    tx_lv_last_full_ms = now;
  }

  if (tx_lv_mode == TX_LV_ACTIVE && tx_lv_lbl_sticks) {
    static uint32_t st_tick;
    if (now - st_tick >= 200) {
      st_tick = now;
      int s = 0, t = 0;
      tx_radio_live_adc(&s, &t);
      char b[28];
      snprintf(b, sizeof(b), "ST %d  TH %d", s, t);
      lv_label_set_text(tx_lv_lbl_sticks, b);
      lv_obj_invalidate(tx_lv_lbl_sticks);
    }
  }

  lv_timer_handler();
}

#endif
