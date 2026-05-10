/**
 * LVGL 8.3.x — Arduino:
 * Okrem tejto kópie musí byť rovnaký súbor aj ako
 *   Documents/Arduino/libraries/lv_conf.h
 * (VEDĽA priečinka „lvgl“, NIE vnútri libraries/lvgl/).
 * Pri zmene tu ho tam znova skopíruj.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (96U * 1024U)

#define LV_DISP_DEF_REFR_PERIOD 15
#define LV_INDEV_DEF_READ_PERIOD 15

/** Vyžadované pre roller fade masku (tx_lvgl_ui). */
#define LV_DRAW_COMPLEX 1

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 1

#define LV_USE_FLEX 1

#define LV_USE_LABEL 1
#define LV_USE_BTN 0
#define LV_USE_IMG 0
#define LV_USE_ANIMIMG 0
#define LV_USE_LINE 0
#define LV_USE_TABLE 0
#define LV_USE_CHECKBOX 0
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_CANVAS 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_KEYBOARD 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 1
#define LV_USE_LIST 0
#define LV_USE_CHART 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0
#define LV_USE_CALENDAR 0
#define LV_SPINNER_DEF_ARC_LENGTH 60
#define LV_USE_SPINNER 0
#define LV_USE_MENU 0
#define LV_USE_LED 0

#endif /* LV_CONF_H */
