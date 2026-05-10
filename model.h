#ifndef MODEL_H
#define MODEL_H

#include <SD.h>
#include "sd_fs.h"

const int model_max = 10;                                                 // Maximum of directories/models
String model_name[model_max];                                             // Initialize array for directories/model names
int model_found = 0;                                                      // Counter for found directories/models
int model_choice = 0;                                                   // Counter for found directories/models
int model_select = 0;
boolean model_selected = false;
String model_root = "/models";
String model_mac= "";


int speed_forward_limit = 0;
int speed_reverse_limit = 0;

void readDirectories(File dir) {
  model_found = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break; // Ak nie sú ďalšie položky, ukonči cyklus
    }
    
    if (entry.isDirectory()) {
      String dirName = entry.name();
      if (!dirName.startsWith("System")) {
        if (model_found <= model_max) {
          model_found++;
          model_name[model_found] = dirName; // Uloženie názvu adresára do poľa
        }
      }
    }
    entry.close();
  }
}

String readFileContentString(const char* filepath) {
  String content = ""; // Premenná na uloženie obsahu
  File file = SD.open(sd_path(filepath), FILE_READ); // Otvoríme súbor na čítanie
  
  if (!file) {
    Serial.println("Nepodarilo sa otvoriť súbor na čítanie!");
    return content; // Vráť prázdny reťazec ako indikátor chyby
  } else {
    Serial.println("subor je ok");
  }

  // Načítanie obsahu súboru do premennej String
  while (file.available()) {
    content += String((char)file.read()); // Čítanie znaku po znaku
  }

  file.close(); // Zatvor súbor
  return content; // Vráť načítaný obsah
}
String readFileContentInt(const char* filepath) {
  String content = ""; // Premenná na uloženie obsahu
  String fileroot = sd_path("/" + model_name[model_select] + "/" + String(speed_forward_limit) + ".h");
  File file = SD.open(fileroot, FILE_READ); // Otvoríme súbor na čítanie
  
  if (!file) {
    Serial.println("Nepodarilo sa otvoriť súbor na čítanie!");
    return content; // Vráť prázdny reťazec ako indikátor chyby
  } else {
    Serial.println("subor je ok");
  }

  // Načítanie obsahu súboru do premennej String
  while (file.available()) {
    content += String((char)file.read()); // Čítanie znaku po znaku
  }

  file.close(); // Zatvor súbor
  return content; // Vráť načítaný obsah
}

#endif
