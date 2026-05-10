#ifndef WWW_GITHUB_OTA_H
#define WWW_GITHUB_OTA_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <cctype>
#include <cstring>
#include <ESPAsyncWebServer.h>
#include "build_info.h"

namespace {

static String www_gh_lower_sha(const char* hex40) {
  String o;
  if (!hex40) return o;
  for (size_t i = 0; i < strlen(hex40) && i < 40; i++) {
    char c = hex40[i];
    if (c >= 'A' && c <= 'F')
      c = (char)(c - 'A' + 'a');
    o += c;
  }
  return o;
}

/** Očakávaný asset: firmware-<40 hex>.bin (malé písmená). */
static bool www_gh_parse_asset_sha(const String& name, String& out_sha) {
  if (!name.startsWith("firmware-") || !name.endsWith(".bin")) return false;
  String mid = name.substring(9, name.length() - 4);
  mid.trim();
  if (mid.length() != 40) return false;
  for (unsigned i = 0; i < 40; i++) {
    char c = mid[i];
    if (!isxdigit((unsigned char)c)) return false;
  }
  out_sha = www_gh_lower_sha(mid.c_str());
  return out_sha.length() == 40;
}

static bool www_gh_url_allowed(const String& url) {
  if (!url.startsWith("https://")) return false;
  return url.indexOf("github.com") >= 0 || url.indexOf("githubusercontent.com") >= 0;
}

static volatile uint8_t www_gh_busy = 0;
static volatile uint8_t www_gh_state = 0;  // 0 idle 1 dl 2 err 3 ok
static volatile uint32_t www_gh_written = 0;
static volatile uint32_t www_gh_total = 0;
static String www_gh_cached_url;
static String www_gh_cached_remote_sha;
static TaskHandle_t www_gh_task_handle = nullptr;

static String www_gh_current_sha_norm() {
  return www_gh_lower_sha(FIRMWARE_GIT_SHA_FULL);
}

static void www_gh_ota_task(void* pv) {
  String url = *(String*)pv;
  delete (String*)pv;

  www_gh_state = 1;
  www_gh_written = 0;
  www_gh_total = 0;

  if (!www_gh_url_allowed(url)) {
    www_gh_state = 2;
    www_gh_busy = 0;
    www_gh_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(60000);
  HTTPClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(cli, url)) {
    www_gh_state = 2;
    www_gh_busy = 0;
    www_gh_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    www_gh_state = 2;
    www_gh_busy = 0;
    www_gh_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  int len = http.getSize();
  www_gh_total = (len > 0) ? (uint32_t)len : 0;

  if (Update.isRunning()) Update.abort();
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    http.end();
    www_gh_state = 2;
    www_gh_busy = 0;
    www_gh_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[4096];
  while (http.connected() && (len > 0 || len == -1)) {
    size_t avail = stream->available();
    if (!avail) {
      if (len == 0 && !http.connected()) break;
      delay(1);
      yield();
      continue;
    }
    size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
    int c = stream->readBytes(buf, toRead);
    if (c <= 0) break;
    size_t w = Update.write(buf, (size_t)c);
    if (w != (size_t)c) {
      Serial.println("OTA GitHub: write zlyhal");
      Update.abort();
      http.end();
      www_gh_state = 2;
      www_gh_busy = 0;
      www_gh_task_handle = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    www_gh_written += (uint32_t)c;
    if (len > 0) len -= c;
    yield();
  }

  if (!Update.end(true)) {
    Update.printError(Serial);
    http.end();
    www_gh_state = 2;
    www_gh_busy = 0;
    www_gh_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  http.end();
  www_gh_state = 3;
  www_gh_busy = 0;
  www_gh_task_handle = nullptr;
  delay(800);
  ESP.restart();
}

static void www_handle_github_check(AsyncWebServerRequest* request) {
  if (WiFi.status() != WL_CONNECTED) {
    request->send(200, "application/json; charset=utf-8",
                  "{\"ok\":0,\"err\":\"no_sta\",\"hint\":\"Pripoj STA WiFi v /config/wifi\"}");
    return;
  }

  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(20000);
  HTTPClient http;
  String path = "/repos/";
  path += GITHUB_RELEASE_OWNER;
  path += "/";
  path += GITHUB_RELEASE_REPO;
  path += "/releases/latest";
  String url = "https://api.github.com" + path;
  if (!http.begin(cli, url)) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"begin\"}");
    return;
  }
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "transmitter-esp32-ota");
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    String j = "{\"ok\":0,\"err\":\"api\",\"code\":";
    j += String(code);
    j += "}";
    request->send(200, "application/json; charset=utf-8", j);
    return;
  }

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, http.getStream());
  http.end();
  if (jerr) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"json\"}");
    return;
  }

  JsonArray assets = doc["assets"].as<JsonArray>();
  String remoteSha;
  String assetUrl;
  for (JsonObject a : assets) {
    const char* nm = a["name"];
    const char* u = a["browser_download_url"];
    if (!nm || !u) continue;
    String sh;
    if (www_gh_parse_asset_sha(String(nm), sh)) {
      remoteSha = sh;
      assetUrl = String(u);
      break;
    }
  }

  if (remoteSha.length() == 0 || assetUrl.length() == 0) {
    request->send(200, "application/json; charset=utf-8",
                  "{\"ok\":0,\"err\":\"no_firmware_asset\",\"hint\":\"očakávam firmware-<40hex>.bin v najnovšom release\"}");
    return;
  }

  if (!www_gh_url_allowed(assetUrl)) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"bad_url\"}");
    return;
  }

  String cur = www_gh_current_sha_norm();
  bool upd = (remoteSha != cur);
  www_gh_cached_url = assetUrl;
  www_gh_cached_remote_sha = remoteSha;

  String j = "{\"ok\":1,\"sta\":true,\"current_sha\":\"";
  j += cur;
  j += "\",\"remote_sha\":\"";
  j += remoteSha;
  j += "\",\"update_available\":";
  j += upd ? "true" : "false";
  j += "}";
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handle_github_status(AsyncWebServerRequest* request) {
  String j = "{\"ok\":1,\"sta\":";
  j += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  j += ",\"busy\":";
  j += www_gh_busy ? "true" : "false";
  j += ",\"phase\":";
  j += String((int)www_gh_state);
  j += ",\"written\":";
  j += String((unsigned long)www_gh_written);
  j += ",\"total\":";
  j += String((unsigned long)www_gh_total);
  j += ",\"current_sha\":\"";
  j += www_gh_current_sha_norm();
  j += "\"}";
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handle_github_install(AsyncWebServerRequest* request) {
  if (WiFi.status() != WL_CONNECTED) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_sta\"}");
    return;
  }
  if (www_gh_busy) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"busy\"}");
    return;
  }
  if (www_gh_cached_url.length() == 0) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"check_first\"}");
    return;
  }
  if (!www_gh_url_allowed(www_gh_cached_url)) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"bad_cache\"}");
    return;
  }

  www_gh_busy = 1;
  www_gh_state = 0;
  www_gh_written = 0;
  www_gh_total = 0;

  String* heapUrl = new String(www_gh_cached_url);
  BaseType_t ok = xTaskCreatePinnedToCore(www_gh_ota_task, "ota_github", 24576, heapUrl, 5, &www_gh_task_handle, 1);
  if (ok != pdPASS) {
    delete heapUrl;
    www_gh_busy = 0;
    request->send(500, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"task\"}");
    return;
  }

  request->send(200, "application/json; charset=utf-8", "{\"ok\":1,\"msg\":\"started\"}");
}

}  // namespace

inline void www_github_ota_begin() {
  www_server.on("/config/firmware/github/check", HTTP_GET, [](AsyncWebServerRequest* r) { www_handle_github_check(r); });
  www_server.on("/config/firmware/github/status", HTTP_GET, [](AsyncWebServerRequest* r) { www_handle_github_status(r); });
  www_server.on("/config/firmware/github/install", HTTP_POST, [](AsyncWebServerRequest* r) { www_handle_github_install(r); });
}

#endif
