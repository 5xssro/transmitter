/**
 * TFT_eSPI — LOLIN ESP32-S3 Pro, onboard ILI9341 (HSPI).
 * Ak máš iný panel/piny, uprav podľa Bodmer User_Setups alebo vlastného zapojenia.
 */
#pragma once
#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TFT_CS   48
#define TFT_DC   47
#define TFT_RST  21

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define USE_HSPI_PORT

#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000
