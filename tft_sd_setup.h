  tft.init();
  /* Predtým 1 (landscape). 0 = o 90° proti smeru hodinových ručičiek. */
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  /* TFT init; LVGL kreslí v tx_lvgl_ui_poll(). */

  sd_mounted = false;

  // Montáž na "/" alebo predvolené "/sd" — v SD.open musíme vždy používať len /config, /models
  // (nie /sd/...), lebo VFS knižnica mountpoint pripája sama.
  delay(20);
  if (SD.begin(SD_SPI_CS_PIN, SPI, 4000000, "/", 5, false)) {
    sd_mounted = true;
    Serial.println("SD: montaz OK (vfs /)");
  } else {
    Serial.println("SD: montaz na / zlyhala — skusam /sd ...");
    SD.end();
    delay(20);
    if (SD.begin(SD_SPI_CS_PIN, SPI, 4000000, "/sd", 5, false)) {
      sd_mounted = true;
      Serial.println("SD: montaz OK (vfs /sd)");
    } else {
      SD.end();
      Serial.println("SD: Card Mount Failed — karta, CS (LOLIN S3 Pro: GPIO 46), FAT32, napajanie?");
    }
  }

#if defined(ESP32)
  __sync_synchronize();
#endif

  if (sd_mounted) {
    model_root = sd_path("/models");
  }

  if (!sd_mounted) {
    Serial.println("SD: pokracujem bez karty — web prehliadac suborov nepojde bez SD.");
  }

  uint8_t cardType = sd_mounted ? SD.cardType() : CARD_NONE;

  if (!sd_mounted || cardType == CARD_NONE) {
    Serial.println("No SD card or mount failed");
  } else {
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
  }
