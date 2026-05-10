  if (sd_mounted) {
    SD.mkdir(sd_path("/models").c_str());
    File root = SD.open(model_root);
    if (root) {
      readDirectories(root);
      root.close();
    }
    for (int i = 1; i <= model_found; i++) {
      SerialDebug(model_name[i]);
    }
  }
