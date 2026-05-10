Smooth font pre TFT (UTF-8, slovenská diakritika)

Skopíruj na SD kartu:
  /fonts/tx_ui14.vlw

V Arduino knižnici TFT_eSPI otvor User_Setup.h a pridaj:
  #define SMOOTH_FONT

Zdroj .vlw v projekte: sd_assets/fonts/tx_ui14.vlw  
Po aktualizácii skriptu vždy **znova skopíruj** súbor na SD.

Regenerácia: `python tools/gen_tx_ui_vlw.py`
