#ifndef TX_STICKS_LCD_H
#define TX_STICKS_LCD_H

#include <Arduino.h>
#include "tx_lvgl_ui.h"

inline void tx_sticks_lcd_poll() {
  tx_lvgl_ui_poll();
}

#endif
