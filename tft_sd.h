#ifndef TFT_SD_H
#define TFT_SD_H

#include "FS.h"                                                             // File system library
#include <LittleFS.h>
#include "SD.h"                                                             // SD Card Library
#include <TFT_eSPI.h>                                                       // Graphics and font library for ILI9341 driver chip
#include <SPI.h>                                                            // SPI library
#include "Free_Fonts.h"                                                     // Include the header file attached to this sketch



TFT_eSPI tft = TFT_eSPI();                                                  // Invoke library for tft

// Define Center point for tft
  int centerY = TFT_WIDTH / 2;
  int centerX = TFT_HEIGHT / 2;

// LOLIN S3 Pro: TF_CS = GPIO 46. Pri inom module tu zmeň pin.
#ifndef SD_SPI_CS_PIN
#define SD_SPI_CS_PIN 46
#endif

extern volatile bool sd_mounted;

#include "sd_fs.h"

#endif
