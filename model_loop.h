  if(model_select==0 && !model_choice==0){
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(centerX-(tft.textWidth(model_name[model_choice].c_str())/2),centerY);
    tft.print(model_name[model_choice]);
  }

  if(model_selected==false && !model_select==0){

    model_selected=true;
    String model_mac_file = "/"+model_name[model_select]+"/"+"mac.h";
    const char* cStringContentModel_mac_file = model_mac_file.c_str();
    model_mac = readFileContentString(cStringContentModel_mac_file);
    Serial.println(model_mac);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE,TFT_BLACK);  
    tft.setTextSize(1);
    tft.setCursor(centerX-(tft.textWidth(model_name[model_select].c_str())/2),0);
    tft.println(model_mac+" - "+model_name[model_select]);
    tft.print(model_mac);


    speed_forward_limit = readFileContentInt("speed_forward_limit").toInt();
    speed_reverse_limit = readFileContentInt("speed_reverse_limit").toInt();

  }
