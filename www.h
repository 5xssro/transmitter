#ifndef WWW_H
#define WWW_H

#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <cstring>
#include <cstdio>
#include "build_info.h"
#include "config_wifi.h"
#include "network_services.h"
#include "www_models.h"
#include "model_web_cfg.h"
#include "espnow_tx.h"
#include "transmitter_prefs.h"
#include "tx_model_cfg.h"
#include "tx_active_model.h"
#include "config_sticks_cal.h"
#include "tx_radio_task.h"

extern volatile bool sd_mounted;
extern String www_model_session_dir;
extern String tx_active_model_dir;

AsyncWebServer www_server(80);

#include "www_github_ota.h"

inline void www_initEspNowLater() {
  (void)tx_espnow_begin();
}

static String www_htmlEscape(const String& s) {
  String o;
  o.reserve(s.length() * 2);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c; break;
    }
  }
  return o;
}

static String www_jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (c == '\n')
      o += "\\n";
    else if (c == '\r')
      o += "\\r";
    else if (c == '\t')
      o += "\\t";
    else
      o += c;
  }
  return o;
}

static String www_uriEncodePathQuery(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ' ')
      o += "%20";
    else if (c == '&')
      o += "%26";
    else if (c == '#')
      o += "%23";
    else if (c == '%')
      o += "%25";
    else
      o += c;
  }
  return o;
}

static int www_hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

/** Path segment — len %HH (odkazy z www_uriEncodePathQuery). */
static String www_uriDecodePathSeg(const String& s) {
  String o;
  o.reserve(s.length());
  for (unsigned i = 0; i < s.length();) {
    if (s[i] == '%' && i + 2 < s.length()) {
      int hi = www_hexDigit(s[i + 1]), lo = www_hexDigit(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        o += (char)((hi << 4) | lo);
        i += 3;
        continue;
      }
    }
    o += s[i++];
  }
  return o;
}

static bool www_uriPathSegUnres(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
}

/** Bezpečné segmenty cesty /model/MENO (UTF-8 bajt po bajte). */
static String www_uriEncodePathSeg(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t b = (uint8_t)s[i];
    if (www_uriPathSegUnres((char)b)) {
      o += (char)b;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned)b);
      o += buf;
    }
  }
  return o;
}

static String www_format_mac_bytes(const uint8_t m[6]) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

static bool www_parse_mac_colon(const String& in, uint8_t out[6]) {
  String s = in;
  s.trim();
  if (s.length() == 0) return false;
  s.toUpperCase();
  s.replace('-', ':');
  int part = 0;
  unsigned val = 0;
  int nhex = 0;
  for (unsigned i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s[i] : ':';
    if (c == ':') {
      if (nhex < 1 || nhex > 2 || part >= 6) return false;
      out[part++] = (uint8_t)val;
      val = 0;
      nhex = 0;
      continue;
    }
    int d = -1;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'A' && c <= 'F')
      d = 10 + (c - 'A');
    else
      return false;
    val = (val << 4) | (unsigned)d;
    nhex++;
    if (nhex > 2) return false;
  }
  return part == 6;
}

static String www_normalize_mac_input(const String& in) {
  uint8_t m[6];
  if (!www_parse_mac_colon(in, m)) return "";
  return www_format_mac_bytes(m);
}

static String www_normalizePath(const String& in) {
  String p = in;
  p.trim();
  if (p.length() == 0) return "/";
  if (!p.startsWith("/")) p = "/" + p;
  if (p.indexOf("..") >= 0) return "";
  while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  return p;
}

static void www_sendRedirect(AsyncWebServerRequest* request, const String& url) {
  AsyncWebServerResponse* resp = request->beginResponse(302);
  resp->addHeader("Location", url);
  request->send(resp);
}

static String www_entryBaseName(const String& nameIn) {
  String name = nameIn;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  return name;
}

static String www_childPath(const String& parentDir, const String& nameFromEntry) {
  String base = www_entryBaseName(String(nameFromEntry.c_str()));
  String full = parentDir;
  if (!full.endsWith("/")) full += "/";
  full += base;
  return full;
}

static bool www_removeRecursive(const String& path) {
  if (path == "/" || path.length() == 0) return false;
  String vfs = sd_path(path);
  File root = SD.open(vfs);
  if (!root) return false;
  if (!root.isDirectory()) {
    root.close();
    return SD.remove(vfs.c_str());
  }
  File file = root.openNextFile();
  while (file) {
    String fp = www_childPath(path, String(file.name()));
    bool isDir = file.isDirectory();
    file.close();
    if (isDir) {
      if (!www_removeRecursive(fp)) {
        root.close();
        return false;
      }
    } else {
      if (!SD.remove(sd_path(fp).c_str())) {
        root.close();
        return false;
      }
    }
    file = root.openNextFile();
  }
  root.close();
  return SD.rmdir(vfs.c_str());
}

static String www_parentPath(const String& path) {
  if (path.length() <= 1) return "/";
  int sl = path.lastIndexOf('/');
  if (sl <= 0) return "/";
  return path.substring(0, sl);
}

// --- Layout (sivý, responzívny) ---

static void www_emit_head(String& html, const char* title) {
  html += F("<!DOCTYPE html><html lang=\"sk\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  html += String(title);
  html += F("</title><style>");
  html += F(":root{--bg:#dfe1e4;--surface:#ebedf0;--card:#fff;--line:#b4b8bf;--txt:#232428;--muted:#5e6168;--shadow:0 2px 10px rgba(0,0,0,.07)}");
  html += F("*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.55 system-ui,-apple-system,\"Segoe UI\",Roboto,\"Noto Sans\",\"Liberation Sans\",Arial,sans-serif;min-height:100vh}");
  html += F(".wrap{max-width:920px;margin:0 auto;padding:clamp(.55rem,2vw,1.2rem)}");
  html += F(".site-h{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:1rem 1.15rem;margin-bottom:1rem;box-shadow:var(--shadow)}");
  html += F(".site-t{margin:0;font-size:clamp(1.02rem,2.3vw,1.32rem);font-weight:650;letter-spacing:-.02em}");
  html += F(".site-n{display:flex;flex-wrap:wrap;gap:.4rem;margin-top:.8rem}");
  html += F(".site-na{padding:.4rem .8rem;border-radius:11px;border:1px solid var(--line);background:var(--surface);color:var(--txt);text-decoration:none;font-size:.87rem}");
  html += F(".site-na:hover{background:#e2e4e8}.site-na[aria-current=page]{background:var(--txt);color:var(--card);border-color:var(--txt)}");
  html += F(".site-m{margin-bottom:1rem}");
  html += F(".card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:1rem 1.15rem;margin-bottom:1rem;box-shadow:var(--shadow)}");
  html += F(".card > h2,.card h2.section{margin:0 0 .7rem;font-size:.76rem;font-weight:650;letter-spacing:.07em;color:var(--muted);text-transform:uppercase}");
  html += F(".breadcrumb{font-size:.88rem;color:var(--muted);margin:0 0 .7rem}.breadcrumb strong{color:var(--txt)}");
  html += F(".tbl-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:12px}");
  html += F("table{width:100%;border-collapse:collapse;font-size:.86rem}");
  html += F("th,td{padding:.52rem .65rem;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}");
  html += F("th{background:var(--surface);color:var(--muted);font-weight:650}");
  html += F("tr:last-child td{border-bottom:0}tbody tr:hover td{background:#f6f7f8}");
  html += F(".btnrow{display:flex;flex-wrap:wrap;gap:.32rem;align-items:center}");
  html += F(".cal-btnrow{display:flex;flex-wrap:wrap;gap:.4rem;align-items:stretch;margin-top:.45rem;width:100%}");
  html += F(".cal-btnrow button{flex:1 1 0;min-width:0;text-align:center}");
  html += F("button,input[type=submit]{font:inherit;font-size:.86rem;padding:.36rem .72rem;border-radius:11px;border:1px solid var(--line);background:var(--surface);color:var(--txt);cursor:pointer}");
  html += F("button:hover,input[type=submit]:hover{background:#dde0e5}");
  html += F("input[type=text],input[type=password],input[type=file],input[type=number]{font:inherit;padding:.45rem .65rem;border:1px solid var(--line);border-radius:11px;background:#fff;width:100%;max-width:22rem}");
  html += F(".form-row{margin-bottom:.62rem}.form-row label{display:block;font-size:.82rem;color:var(--muted);margin-bottom:.28rem;font-weight:500}");
  html += F(".alert{border:1px solid var(--line);border-radius:12px;padding:.72rem 1rem;margin-bottom:1rem;font-size:.9rem}");
  html += F(".alert.ok{background:#e6e8ec}.alert.err{background:#ebecef;color:#333}");
  html += F(".grid-2{display:grid;grid-template-columns:repeat(auto-fit,minmax(236px,1fr));gap:.85rem}");
  html += F(".tile{display:block;border:1px solid var(--line);border-radius:16px;padding:1.05rem 1.15rem;background:var(--card);text-decoration:none;color:var(--txt);box-shadow:var(--shadow)}");
  html += F(".tile:hover{border-color:#9ea4ad}.tile-t{display:block;font-weight:650;margin-bottom:.32rem}.tile-d{display:block;font-size:.86rem;color:var(--muted);line-height:1.35}");
  html += F(".model-stack{display:flex;flex-direction:column;gap:.45rem;margin-top:.35rem}");
  html += F(".model-stack a{display:block;text-align:left;padding:.65rem 1rem;border-radius:12px;border:1px solid var(--line);background:var(--surface);color:var(--txt);text-decoration:none;font-size:.95rem;font-weight:500}");
  html += F(".model-stack a:hover{background:#e2e4e8}");
  html += F("form.model-open{margin:0}form.model-open button{display:block;width:100%;text-align:left;padding:.65rem 1rem;border-radius:12px;border:1px solid var(--line);background:var(--surface);color:var(--txt);font-size:.95rem;font-weight:500;font-family:inherit;cursor:pointer}");
  html += F("form.model-open button:hover{background:#e2e4e8}");
  html += F(".site-f{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:.72rem 1rem;font-size:.79rem;color:var(--muted);display:flex;flex-wrap:wrap;gap:.45rem 1.1rem}");
  html += F(".mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.84rem}");
  html += F(".model-tabs{display:flex;flex-wrap:wrap;gap:.4rem;margin:0 0 .85rem}");
  html += F(".model-tabs .site-na{padding:.45rem .9rem;border-radius:12px;font-size:.88rem}");
  html += F("input[type=range]{width:100%;max-width:100%;accent-color:var(--txt)}");
  html += F(".axis-block{margin:.85rem 0 1rem}");
  html += F(".axis-head{font-size:.72rem;font-weight:650;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin:0 0 .35rem}");
  html += F(".travel-rail{position:relative;display:flex;align-items:center;gap:.5rem;min-height:2.6rem;padding:.4rem .55rem;background:linear-gradient(90deg,#e4e6ea 0%,#f6f7f8 50%,#e4e6ea 100%);border-radius:12px;border:1px solid var(--line)}");
  html += F(".travel-rail::before{content:'';position:absolute;left:50%;top:12%;bottom:12%;width:2px;margin-left:-1px;background:var(--txt);opacity:.28;border-radius:1px;pointer-events:none}");
  html += F(".travel-rail input[type=range]{flex:1;min-width:0}");
  html += F("input[type=range].travel-range-center{flex:1;min-width:0;height:1.65rem;--a:50%;--b:50%;accent-color:transparent;background:transparent;-webkit-appearance:none;-moz-appearance:none;appearance:none}");
  html += F("input[type=range].travel-range-center::-webkit-slider-runnable-track{height:10px;border-radius:999px;background:linear-gradient(to right,#e4e6ea 0%,#e4e6ea var(--a),#25282c var(--a),#25282c var(--b),#e4e6ea var(--b),#e4e6ea 100%)}");
  html += F("input[type=range].travel-range-center::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:16px;height:16px;margin-top:-3px;border-radius:50%;background:var(--txt);border:2px solid var(--card);box-shadow:0 0 0 1px rgba(0,0,0,.12)}");
  html += F("input[type=range].travel-range-center::-moz-range-track{height:10px;border-radius:999px;background:linear-gradient(to right,#e4e6ea 0%,#e4e6ea var(--a),#25282c var(--a),#25282c var(--b),#e4e6ea var(--b),#e4e6ea 100%)}");
  html += F("input[type=range].travel-range-center::-moz-range-thumb{width:14px;height:14px;border:none;border-radius:50%;background:var(--txt)}");
  html += F("input[type=range].travel-range-center::-moz-range-progress{background:transparent}");
  html += F(".trim-rail{background:linear-gradient(90deg,#dde4ee 0%,#f6f7f8 48%,#eee4dc 100%);border-radius:12px;border:1px solid var(--line);padding:.45rem .6rem}");
  html += F(".trim-hint{font-size:.82rem;color:var(--muted);margin:.35rem 0 0}");
  html += F(".rev-row{margin-top:.55rem;display:flex;align-items:center;gap:.5rem;font-size:.88rem}");
  html += F(".rev-row input{width:auto;max-width:none}");
  html += F(".cfg-section{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:.65rem 1rem .85rem;margin-bottom:1rem;box-shadow:var(--shadow)}");
  html += F(".cfg-sub{margin:0 0 .5rem;font-size:.72rem;font-weight:650;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}");
  html += F("@media(max-width:540px){input[type=text],input[type=password],input[type=file],input[type=number],input[type=range]{max-width:100%}.site-na{padding:.42rem .65rem}}");
  html += F("</style></head><body><div class=\"wrap\">");
}

static void www_emit_main_nav(String& html, const char* nav) {
  html += F("<header class=\"site-h\"><p class=\"site-t\">Transmitter</p><nav class=\"site-n\" aria-label=\"Hlavná navigácia\">");
  html += F("<a class=\"site-na\" href=\"/\"");
  if (nav && strcmp(nav, "models") == 0) html += F(" aria-current=\"page\"");
  html += F(">Modely</a>");
  html += F("<a class=\"site-na\" href=\"/config/wifi\"");
  if (nav && strcmp(nav, "wifi") == 0) html += F(" aria-current=\"page\"");
  html += F(">Sieť</a>");
  html += F("<a class=\"site-na\" href=\"/config/file\"");
  if (nav && strcmp(nav, "file") == 0) html += F(" aria-current=\"page\"");
  html += F(">Súbory</a>");
  html += F("<a class=\"site-na\" href=\"/config/cal\"");
  if (nav && strcmp(nav, "cal") == 0) html += F(" aria-current=\"page\"");
  html += F(">Kalibrácia</a>");
  html += F("<a class=\"site-na\" href=\"/config/firmware\"");
  if (nav && strcmp(nav, "fw") == 0) html += F(" aria-current=\"page\"");
  html += F(">Firmware</a>");
  html += F("</nav></header>");
}

static void www_emit_footer(String& html) {
  html += F("<footer class=\"site-f\"><span>AP ");
  html += WiFi.softAPIP().toString();
  html += F("</span><span>STA ");
  if (WiFi.status() == WL_CONNECTED)
    html += WiFi.localIP().toString();
  else
    html += F("—");
  html += F("</span><span class=\"mono\">Zostavené ");
  html += FIRMWARE_BUILD_DATETIME;
  html += F("</span></footer></div></body></html>");
}

static void www_page_start(String& html, const char* title, const char* nav) {
  www_emit_head(html, title);
  www_emit_main_nav(html, nav);
  html += F("<main class=\"site-m\">");
}

static void www_page_end(String& html) {
  html += F("</main>");
  www_emit_footer(html);
}

static void www_send_sd_missing(AsyncWebServerRequest* request) {
  String html;
  www_page_start(html, "SD", "file");
  html += F("<div class=\"card\"><h2>Chyba</h2><p>SD karta nie je dostupná. Skontroluj kartu a CS (LOLIN S3 Pro: GPIO&nbsp;46).</p><p><a class=\"site-na\" href=\"/config/file\">Súbory</a></p></div>");
  www_page_end(html);
  request->send(503, "text/html; charset=utf-8", html);
}

static void www_legacy_redirect_under_cfg_file(AsyncWebServerRequest* request) {
  String u = String(request->url());
  String loc = "/config/file";
  int slash2 = u.indexOf('/', 1);
  if (slash2 > 0) {
    loc += u.substring(slash2);
  } else {
    int q = u.indexOf('?');
    if (q >= 0) loc += u.substring(q);
  }
  www_sendRedirect(request, loc);
}

static bool www_require_sd(AsyncWebServerRequest* request) {
  if (sd_mounted) return true;
  www_send_sd_missing(request);
  return false;
}

/** Adresár modelu, ktorý má v model_web.json túto MAC (normalizovanú), alebo prázdne. */
static String www_find_model_dir_owning_peer_mac(const String& mac_canonical) {
  if (mac_canonical.length() == 0) return "";
  www_models_scan_sd();
  for (int i = 1; i <= www_model_count; i++) {
    String d = www_model_names[i];
    d.trim();
    if (!www_model_dir_token_valid(d)) continue;
    if (!SD.exists(sd_path(model_web_cfg_vpath(d)))) continue;
    String existing = model_web_cfg_load_peer_mac(d);
    existing.trim();
    if (existing.length() == 0) continue;
    String n = www_normalize_mac_input(existing);
    if (n.length() == 0) continue;
    if (n == mac_canonical) return d;
  }
  return "";
}

static void www_handleModelEspnowScan(AsyncWebServerRequest* request) {
  if (!tx_espnow_begin()) {
    request->send(500, "application/json", "{\"ok\":0,\"err\":\"esp_now\"}");
    return;
  }
  if (!sd_mounted) {
    request->send(503, "application/json", "{\"ok\":0,\"err\":\"no_sd\"}");
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    request->send(403, "application/json", "{\"ok\":0,\"err\":\"no_session\"}");
    return;
  }
  if (!model_web_cfg_ensure_file(dir)) {
    request->send(500, "application/json", "{\"ok\":0,\"err\":\"cfg_file\"}");
    return;
  }
  tx_espnow_scan_run();
  TxEspScanPeer rows[TXESP_SCAN_MAX];
  int n = tx_espnow_scan_copy_peers(rows, TXESP_SCAN_MAX);
  String j;
  j.reserve((unsigned)n * 120 + 32);
  j = F("{\"ok\":1,\"peers\":[");
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    String owner = www_find_model_dir_owning_peer_mac(String(rows[i].mac));
    bool blocked = owner.length() > 0 && owner != dir;
    j += F("{\"mac\":\"");
    j += rows[i].mac;
    j += F("\",\"name\":\"");
    j += www_jsonEscape(String(rows[i].name));
    j += F("\",\"blocked\":");
    j += blocked ? F("true") : F("false");
    if (owner.length() > 0) {
      String ol = model_web_cfg_list_label(owner);
      j += F(",\"used_in_dir\":\"");
      j += www_jsonEscape(owner);
      j += F("\",\"used_in_label\":\"");
      j += www_jsonEscape(ol);
      j += F("\"");
    }
    j += F(",\"batt_mv\":");
    j += String((unsigned)rows[i].batt_mv);
    j += F(",\"sig_level\":");
    j += String((unsigned)rows[i].sig_level);
    j += F("}");
  }
  j += F("]}");
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handleModelRxTelem(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    request->send(503, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_sd\"}");
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    request->send(403, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_session\"}");
    return;
  }
  String macs = model_web_cfg_load_peer_mac(dir);
  macs.trim();
  uint8_t mac[6];
  bool have_peer = (macs.length() > 0 && tx_parse_mac_colon(macs, mac));
  if (!have_peer) {
    request->send(200, "application/json; charset=utf-8", "{\"ok\":1,\"have\":false,\"have_peer\":false}");
    return;
  }
  uint16_t bm = 0;
  uint8_t sg = 0;
  uint32_t age = 0;
  bool have = false;
  txesp_telem_snapshot_for_mac(mac, &bm, &sg, &age, &have);
  String j = F("{\"ok\":1,\"have_peer\":true,\"have\":");
  j += have ? F("true") : F("false");
  if (have) {
    j += F(",\"batt_mv\":");
    j += String((unsigned)bm);
    j += F(",\"sig_level\":");
    j += String((unsigned)sg);
    j += F(",\"age_ms\":");
    j += String((unsigned long)age);
  }
  j += F("}");
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handleModelOpen(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    www_sendRedirect(request, "/");
    return;
  }
  if (!request->hasArg("model_dir")) {
    www_sendRedirect(request, "/");
    return;
  }
  String dir = request->arg("model_dir");
  dir.trim();
  if (!www_model_dir_token_valid(dir)) {
    www_sendRedirect(request, "/");
    return;
  }
  www_models_scan_sd();
  if (!www_model_is_listed(dir)) {
    www_sendRedirect(request, "/");
    return;
  }
  if (!model_web_cfg_ensure_file(dir)) {
    www_sendRedirect(request, "/");
    return;
  }
  www_model_session_dir = dir;
  tx_prefs_set_last_model_dir(dir);
  for (int i = 1; i <= www_model_count; i++) {
    String mn = www_model_names[i];
    mn.trim();
    if (mn == dir) {
      www_model_selected = i;
      Serial.printf("WWW model session: %s\n", dir.c_str());
      break;
    }
  }
  www_sendRedirect(request, "/model/cfg");
}

static void www_emit_model_subnav(String& html, const char* cur) {
  html += F("<div class=\"cfg-section\"><p class=\"cfg-sub\">Model</p><nav class=\"model-tabs\" aria-label=\"Z\u00e1lo\u017eky modelu\">");
  html += F("<a class=\"site-na\" href=\"/model/cfg\"");
  if (cur && strcmp(cur, "cfg") == 0) html += F(" aria-current=\"page\"");
  html += F(">\u00dadaje</a>");
  html += F("<a class=\"site-na\" href=\"/model/travel\"");
  if (cur && strcmp(cur, "trv") == 0) html += F(" aria-current=\"page\"");
  html += F(">Dr\u00e1ha</a>");
  html += F("<a class=\"site-na\" href=\"/model/trim\"");
  if (cur && strcmp(cur, "trim") == 0) html += F(" aria-current=\"page\"");
  html += F(">Trim</a>");
  html += F("</nav></div>");
}

static void www_emit_model_rxtelem_script(String& html) {
  html += F("<script defer>(function(){function fmtV(mv){if(mv==null||mv===\"\"||isNaN(Number(mv)))return\"\";return(Math.round(Number(mv))/1000).toFixed(2).replace(\".\",\",\");}function tick(){var el=document.getElementById(\"rxtelem\");if(!el)return;fetch(\"/model/rxtelem\",{credentials:\"same-origin\"}).then(function(r){return r.json();}).then(function(j){if(!j||!j.ok){el.textContent=\"\";return;}if(!j.have_peer){el.textContent=\" \u00b7 Prij\u00edma\u010d: zadaj MAC prij\u00edma\u010da\";return;}if(!j.have){el.textContent=\" \u00b7 Prij\u00edma\u010d: \u010dak\u00e1m na d\u00e1ta (vysielanie / sken)\";return;}var step=(j.sig_level!=null)?(Number(j.sig_level)+1):\"?\";el.textContent=\" \u00b7 \"+fmtV(j.batt_mv)+\" V \u00b7 sign\u00e1l \"+step+\"/5\";}).catch(function(){var el=document.getElementById(\"rxtelem\");if(el)el.textContent=\"\";});}setInterval(tick,900);tick();})();</script>");
}

static void www_handleModelCfgGet(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    String html;
    www_page_start(html, "Model", "models");
    html += F("<div class=\"card alert err\"><p>SD nie je k dispozícii.</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(200, "text/html; charset=utf-8", html);
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    www_sendRedirect(request, "/");
    return;
  }
  if (!model_web_cfg_ensure_file(dir)) {
    String html;
    www_page_start(html, "Model", "models");
    html += F("<div class=\"card alert err\"><p>Neviem vytvoriť <span class=\"mono\">model_web.json</span>.</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(500, "text/html; charset=utf-8", html);
    return;
  }
  String disp = model_web_cfg_load_display_name(dir);
  String cur = model_web_cfg_load_peer_mac(dir);
  String title = model_web_cfg_list_label(dir);
  String html;
  www_page_start(html, title.c_str(), "models");
  www_emit_model_subnav(html, "cfg");
  html += F("<div class=\"card\"><p class=\"breadcrumb\"><a class=\"site-na\" href=\"/\">Modely</a> — <strong>");
  html += www_htmlEscape(title);
  html += F("</strong><span id=\"rxtelem\" class=\"mono\" style=\"font-weight:500\"></span></p>");
  www_emit_model_rxtelem_script(html);
  html += F("<p class=\"breadcrumb\">Adresár na SD: <span class=\"mono\">/models/");
  html += www_htmlEscape(dir);
  html += F("</span> (iba <span class=\"mono\">A–Z a–z 0–9 _ -</span>)</p><h2>Nastavenie modelu</h2>");
  html += F("<p class=\"breadcrumb\" style=\"margin-top:0\">Konfigurácia: <span class=\"mono\">model_web.json</span> (doplníme ďalšie polia).</p>");
  html += F("<form method=\"POST\" action=\"/model/cfg/save\"><div class=\"form-row\"><label for=\"dn\">Zobrazovaný názov</label>");
  html += F("<input id=\"dn\" name=\"display_name\" type=\"text\" maxlength=\"64\" autocomplete=\"off\" value=\"");
  html += www_htmlEscape(disp);
  html += F("\"></div><div class=\"form-row\"><label for=\"pm\">MAC prijímača (ESP-NOW)</label>");
  html += F("<input id=\"pm\" name=\"peer_mac\" type=\"text\" placeholder=\"AA:BB:CC:DD:EE:FF\" autocomplete=\"off\" value=\"");
  html += www_htmlEscape(cur);
  html += F("\"></div><div class=\"btnrow\"><button type=\"submit\">Uložiť</button>");
  html += F("<button type=\"button\" id=\"scanbtn\">Skenovať</button></div></form>");
  html += F("<div id=\"scanbox\" class=\"card\" style=\"display:none;margin-top:1rem\"><h2 class=\"section\" style=\"margin-top:0\">Nájdené prijímače</h2>");
  html += F("<p id=\"scanstat\" class=\"breadcrumb\"></p><div id=\"scanlist\" class=\"model-stack\"></div></div>");
  html += F("<script>document.getElementById(\"scanbtn\").onclick=async function(){var b=document.getElementById(\"scanbox\"),st=document.getElementById(\"scanstat\"),list=document.getElementById(\"scanlist\");b.style.display=\"block\";st.textContent=\"Hľadám...\";list.innerHTML=\"\";try{var r=await fetch(\"/model/espnow/scan\");var j=await r.json();if(!j.ok){st.textContent=(j.err===\"esp_now\"?\"ESP-NOW sa nepodarilo spustiť.\":(j.err===\"no_sd\"?\"Chýba SD.\":(j.err===\"no_session\"?\"Otvor model z ponuky.\":(j.err===\"cfg_file\"?\"SD / model_web.json.\":(\"Chyba: \"+j.err)))));return;}if(!j.peers.length){st.textContent=\"Žiadna odpoveď — rovnaký WiFi kanál a firmware prijímača?\";return;}st.textContent=\"Voľné riadky uložia MAC hneď; obsadené sú len na info:\";j.peers.forEach(function(p){if(p.blocked){var dv=document.createElement(\"div\");dv.className=\"breadcrumb\";dv.style.cssText=\"padding:.45rem 0;opacity:.88;font-size:.88rem\";var who=(p.used_in_label&&String(p.used_in_label).length)?p.used_in_label:(p.used_in_dir||\"?\");dv.textContent=p.mac+\" — už priradené k modelu: \"+who;list.appendChild(dv);return;}var a=document.createElement(\"a\");a.href=\"#\";var nm=(p.name&&p.name.length)?p.name:\"(bez mena)\";var ex=\"\";if(p.batt_mv&&Number(p.batt_mv)>0)ex+=\" · \"+(Math.round(Number(p.batt_mv))/1000).toFixed(2).replace(\".\",\",\")+\" V\";if(p.sig_level!=null&&p.sig_level!==\"\")ex+=\" · sig \"+(Number(p.sig_level)+1)+\"/5\";a.textContent=nm+\" — \"+p.mac+ex;a.onclick=async function(e){e.preventDefault();var body=new URLSearchParams();body.append(\"display_name\",document.getElementById(\"dn\").value);body.append(\"peer_mac\",p.mac);b.style.display=\"none\";var rs=await fetch(\"/model/cfg/save\",{method:\"POST\",headers:{\"Content-Type\":\"application/x-www-form-urlencoded\"},body:body.toString(),credentials:\"same-origin\",redirect:\"manual\"});if(rs.status>=200&&rs.status<400){location.reload();return;}if(rs.status===0||rs.type===\"opaqueredirect\"){location.reload();return;}st.textContent=\"Uloženie zlyhalo (\"+rs.status+\").\";b.style.display=\"block\";};list.appendChild(a);});}catch(e){st.textContent=\"Chyba siete.\";}};</script>");
  html += F("</div>");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleModelCfgSave(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    www_sendRedirect(request, "/");
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    www_sendRedirect(request, "/");
    return;
  }
  if (!request->hasParam("peer_mac", true)) {
    www_sendRedirect(request, "/model/cfg");
    return;
  }
  String disp = request->hasArg("display_name") ? request->arg("display_name") : String();
  disp.trim();
  if (disp.length() > 64) disp = disp.substring(0, 64);
  String raw = request->getParam("peer_mac", true)->value();
  raw.trim();
  String toStore;
  if (raw.length() > 0) {
    toStore = www_normalize_mac_input(raw);
    if (toStore.length() == 0) {
      String html;
      www_page_start(html, "Model", "models");
      html += F("<div class=\"card alert err\"><p>Neplatná MAC adresa (tvar AA:BB:CC:DD:EE:FF).</p><p><a class=\"site-na\" href=\"/model/cfg\">Späť</a></p></div>");
      www_page_end(html);
      request->send(200, "text/html; charset=utf-8", html);
      return;
    }
    String owner = www_find_model_dir_owning_peer_mac(toStore);
    if (owner.length() > 0 && owner != dir) {
      String ol = model_web_cfg_list_label(owner);
      String html;
      www_page_start(html, "Model", "models");
      html += F("<div class=\"card alert err\"><p>Táto MAC je už priradená k modelu <strong>");
      html += www_htmlEscape(ol);
      html += F("</strong> (<span class=\"mono\">");
      html += www_htmlEscape(owner);
      html += F("</span>). Jedna MAC môže byť len v jednom modeli.</p><p><a class=\"site-na\" href=\"/model/cfg\">Späť</a></p></div>");
      www_page_end(html);
      request->send(200, "text/html; charset=utf-8", html);
      return;
    }
  }
  if (!model_web_cfg_save_fields(dir, disp, toStore)) {
    String html;
    www_page_start(html, "Model", "models");
    html += F("<div class=\"card alert err\"><p>Zápis na SD zlyhal.</p><p><a class=\"site-na\" href=\"/model/cfg\">Späť</a></p></div>");
    www_page_end(html);
    request->send(500, "text/html; charset=utf-8", html);
    return;
  }
  if (dir == tx_active_model_dir) tx_model_cfg_bump();
  if (toStore.length() > 0) {
    uint8_t rxm[6];
    if (www_parse_mac_colon(toStore, rxm)) {
      (void)tx_espnow_begin();
      tx_espnow_send_pair_bind(rxm);
    }
  }
  www_sendRedirect(request, "/model/cfg");
}

static void www_json_append_sticks_cal(String& j, const SticksCal& c) {
  j += F("\"steer_min\":");
  j += String(c.steer_min);
  j += F(",\"steer_mid\":");
  j += String(c.steer_mid);
  j += F(",\"steer_max\":");
  j += String(c.steer_max);
  j += F(",\"thr_min\":");
  j += String(c.thr_min);
  j += F(",\"thr_mid\":");
  j += String(c.thr_mid);
  j += F(",\"thr_max\":");
  j += String(c.thr_max);
}

static void www_handleModelCalLive(AsyncWebServerRequest* request) {
  int as = 0, at = 0;
  tx_radio_live_adc(&as, &at);
  String j = F("{\"ok\":1,\"steer\":");
  j += String(as);
  j += F(",\"thr\":");
  j += String(at);
  j += F("}");
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handleModelCalStep(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    request->send(503, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_sd\"}");
    return;
  }
  if (!request->hasArg("ch") || !request->hasArg("slot")) {
    request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"args\"}");
    return;
  }
  String ch = request->arg("ch");
  String slot = request->arg("slot");
  ch.trim();
  slot.trim();
  SticksCal c{};
  (void)sticks_cal_load(c);

  int live_s = 0, live_t = 0;
  tx_radio_live_adc(&live_s, &live_t);
  int v = (ch == "steer") ? live_s : live_t;

  if (ch == "steer") {
    if (slot == "min") c.steer_min = v;
    else if (slot == "mid") c.steer_mid = v;
    else if (slot == "max") c.steer_max = v;
    else {
      request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"slot\"}");
      return;
    }
  } else if (ch == "thr") {
    if (slot == "min") c.thr_min = v;
    else if (slot == "mid") c.thr_mid = v;
    else if (slot == "max") c.thr_max = v;
    else {
      request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"slot\"}");
      return;
    }
  } else {
    request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"ch\"}");
    return;
  }

  if (!sticks_cal_save(c)) {
    request->send(500, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"sd\"}");
    return;
  }
  tx_model_cfg_bump();
  String j = F("{\"ok\":1,");
  www_json_append_sticks_cal(j, c);
  j += F("}");
  request->send(200, "application/json; charset=utf-8", j);
}

static void www_handleModelCalGet(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    String html;
    www_page_start(html, "Kalibrácia", "cal");
    html += F("<div class=\"card alert err\"><p>SD nie je k dispozícii.</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(200, "text/html; charset=utf-8", html);
    return;
  }
  SticksCal c{};
  (void)sticks_cal_load(c);
  String html;
  www_page_start(html, "Kalibr\u00e1cia pal\u00edc", "cal");
  html += F("<div class=\"card\"><p class=\"breadcrumb\"><strong>Spolo\u010dn\u00e1 kalibr\u00e1cia</strong> pre v\u0161etky modely (s\u00fabor <span class=\"mono\">/config/sticks_cal.json</span>).</p>");
  html += F("<h2>Potenciometre</h2><p class=\"breadcrumb\">GPIO ");
  html += String(TX_STICK_PIN_STEERING);
  html += F(" = STEERING, GPIO ");
  html += String(TX_STICK_PIN_THROTTLE);
  html += F(" = THROTTLE.</p>");
  html += F("<p class=\"breadcrumb\"><strong>Riadenie:</strong> \u013eav\u00e1 \u2014 <strong>\u013Dav\u00e1</strong> (MIN), rovno \u2014 <strong>Rovno</strong> (STRED), prav\u00e1 \u2014 <strong>Prav\u00e1</strong> (MAX).</p>");
  html += F("<p class=\"breadcrumb\"><strong>Plyn:</strong> dozadu \u2014 <strong>Dozadu</strong> (MIN), stop \u2014 <strong>Stop</strong> (STRED), dopredu \u2014 <strong>Dopredu</strong> (MAX).</p>");
  html += F("<p class=\"breadcrumb\"><strong>Live:</strong> STEERING <span class=\"mono\" id=\"lv_s\">0</span> \u00b7 THROTTLE <span class=\"mono\" id=\"lv_t\">0</span></p></div>");

  html += F("<div class=\"card\"><h2 class=\"section\">STEERING</h2><p class=\"breadcrumb\">Ulo\u017een\u00e9: \u013eav\u00e1 <span class=\"mono\" id=\"steer_min\">");
  html += String(c.steer_min);
  html += F("</span> \u00b7 rovno <span class=\"mono\" id=\"steer_mid\">");
  html += String(c.steer_mid);
  html += F("</span> \u00b7 prav\u00e1 <span class=\"mono\" id=\"steer_max\">");
  html += String(c.steer_max);
  html += F("</span></p><div class=\"cal-btnrow\"><button type=\"button\" data-ch=\"steer\" data-slot=\"min\">\u013Dav\u00e1</button><button type=\"button\" data-ch=\"steer\" data-slot=\"mid\">Rovno</button><button type=\"button\" data-ch=\"steer\" data-slot=\"max\">Prav\u00e1</button></div></div>");

  html += F("<div class=\"card\"><h2 class=\"section\">THROTTLE</h2><p class=\"breadcrumb\">Ulo\u017een\u00e9: dozadu <span class=\"mono\" id=\"thr_min\">");
  html += String(c.thr_min);
  html += F("</span> \u00b7 stop <span class=\"mono\" id=\"thr_mid\">");
  html += String(c.thr_mid);
  html += F("</span> \u00b7 dopredu <span class=\"mono\" id=\"thr_max\">");
  html += String(c.thr_max);
  html += F("</span></p><div class=\"cal-btnrow\"><button type=\"button\" data-ch=\"thr\" data-slot=\"min\">Dozadu</button><button type=\"button\" data-ch=\"thr\" data-slot=\"mid\">Stop</button><button type=\"button\" data-ch=\"thr\" data-slot=\"max\">Dopredu</button></div></div>");

  html += F("<script>(function(){function updLive(){fetch(\"/config/cal/live\",{credentials:\"same-origin\"}).then(function(r){return r.json();}).then(function(j){if(!j||!j.ok)return;document.getElementById(\"lv_s\").textContent=j.steer;document.getElementById(\"lv_t\").textContent=j.thr;}).catch(function(){});}function updCal(j){[\"steer\",\"thr\"].forEach(function(p){[\"min\",\"mid\",\"max\"].forEach(function(s){var el=document.getElementById(p+\"_\"+s);if(el&&j[p+\"_\"+s]!==undefined)el.textContent=j[p+\"_\"+s];});});}document.querySelectorAll(\"button[data-ch]\").forEach(function(btn){btn.onclick=function(){var fd=new URLSearchParams();fd.append(\"ch\",btn.getAttribute(\"data-ch\"));fd.append(\"slot\",btn.getAttribute(\"data-slot\"));fetch(\"/config/cal/step\",{method:\"POST\",headers:{\"Content-Type\":\"application/x-www-form-urlencoded\"},body:fd.toString(),credentials:\"same-origin\"}).then(function(r){return r.json();}).then(function(j){if(j.ok)updCal(j);});};});setInterval(updLive,250);updLive();})();</script>");

  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleModelTravelGet(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    String html;
    www_page_start(html, "Model", "models");
    html += F("<div class=\"card alert err\"><p>SD nie je k dispozícii.</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(200, "text/html; charset=utf-8", html);
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    www_sendRedirect(request, "/");
    return;
  }
  if (!model_web_cfg_ensure_file(dir)) {
    request->send(500, "text/plain", "cfg");
    return;
  }
  if (tx_active_model_dir.length() == 0 || tx_active_model_dir != dir) {
    String html;
    www_page_start(html, "Dr\u00e1ha (travel)", "models");
    www_emit_model_subnav(html, "trv");
    html += F("<div class=\"card alert err\"><p>Dr\u00e1ha je dostupn\u00e1 len pre model potvrden\u00fd na vysiela\u010di (aktu\u00edvny na LCD).</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(403, "text/html; charset=utf-8", html);
    return;
  }
  ModelWebCfg c;
  model_web_cfg_load(dir, c);
  String title = model_web_cfg_list_label(dir);
  String html;
  www_page_start(html, "Dr\u00e1ha (travel)", "models");
  www_emit_model_subnav(html, "trv");
  if (request->hasParam("ok")) html += F("<div class=\"card alert ok\">Ulo\u017een\u00e9.</div>");
  html += F("<div class=\"card\"><p class=\"breadcrumb\"><a class=\"site-na\" href=\"/\">Modely</a> — <strong>");
  html += www_htmlEscape(title);
  html += F("</strong><span id=\"rxtelem\" class=\"mono\" style=\"font-weight:500\"></span></p>");
  www_emit_model_rxtelem_script(html);
  html += F("<h2>Dr\u00e1ha (travel)</h2>");
  html += F("<p class=\"breadcrumb\">Zu\u017eenie v\u00fdchylky od mechanick\u00e9ho stredu k \u013eavej/pravej (riadenie), resp. dozadu/dopredu (plyn). Trim a reverse s\u00fa na z\u00e1lo\u017eke <a class=\"site-na\" href=\"/model/trim\">Trim</a>.</p>");
  html += F("<p id=\"trvstat\" class=\"breadcrumb\" style=\"min-height:1.25em;margin-bottom:.5rem\"></p>");
  html += F("<form id=\"travelForm\" method=\"POST\" action=\"/model/travel/save\" onsubmit=\"return false;\">");
  html += F("<div class=\"cfg-section\" style=\"margin-bottom:.85rem\"><p class=\"cfg-sub\">Riadenie</p>");
  html += F("<div class=\"axis-block\"><p class=\"axis-head\">Travel (\u013eav\u00e1 / prav\u00e1 %) — \u013eav\u00fd posuvn\u00edk: stred 0 %, \u00faplne v\u013eavo 100 %</p><div class=\"travel-rail\">");
  html += F("<input type=\"hidden\" name=\"steer_travel_l\" id=\"stl_h\" value=\"");
  html += String(c.steer_travel_l);
  html += F("\"><input type=\"range\" class=\"travel-range-center\" id=\"stl\" min=\"0\" max=\"100\" value=\"50\" aria-label=\"Riadenie travel \u013eav\u00e1\"><input type=\"range\" class=\"travel-range-center\" name=\"steer_travel_r\" id=\"str\" min=\"0\" max=\"100\" value=\"");
  html += String(c.steer_travel_r);
  html += F("\"></div><p class=\"trim-hint\" id=\"st-tr-h\">\u013eav\u00e1 <span id=\"stl-v\"></span>% · prav\u00e1 <span id=\"str-v\"></span>%</p></div></div>");
  html += F("<div class=\"cfg-section\"><p class=\"cfg-sub\">Plyn</p>");
  html += F("<div class=\"axis-block\"><p class=\"axis-head\">Travel (dozadu / dopredu %) — \u013eav\u00fd posuvn\u00edk: stop 0 %, \u00faplne dozadu 100 %</p><div class=\"travel-rail\">");
  html += F("<input type=\"hidden\" name=\"thr_travel_l\" id=\"ttl_h\" value=\"");
  html += String(c.thr_travel_l);
  html += F("\"><input type=\"range\" class=\"travel-range-center\" id=\"ttl\" min=\"0\" max=\"100\" value=\"50\" aria-label=\"Plyn travel dozadu\"><input type=\"range\" class=\"travel-range-center\" name=\"thr_travel_r\" id=\"ttr\" min=\"0\" max=\"100\" value=\"");
  html += String(c.thr_travel_r);
  html += F("\"></div><p class=\"trim-hint\">dozadu <span id=\"ttl-v\"></span>% · dopredu <span id=\"ttr-v\"></span>%</p></div></div>");
  html += F("</form></div>");
  html += F("<script>(function(){function q(i){return document.getElementById(i);}function syncFill(el){var v=+el.value;el.style.setProperty('--a',Math.min(50,v)+'%');el.style.setProperty('--b',Math.max(50,v)+'%');}function leftUiToStored(v){v=+v;return Math.max(0,Math.min(100,Math.round((50-v)*2)));}function storedToLeftUi(t){t=+t;return Math.max(0,Math.min(100,Math.round(50-t/2)));}function syncLeftHids(){q(\"stl_h\").value=String(leftUiToStored(q(\"stl\").value));q(\"ttl_h\").value=String(leftUiToStored(q(\"ttl\").value));}function upd(){syncLeftHids();q(\"stl-v\").textContent=q(\"stl_h\").value;q(\"str-v\").textContent=q(\"str\").value;q(\"ttl-v\").textContent=q(\"ttl_h\").value;q(\"ttr-v\").textContent=q(\"ttr\").value;}var tmr=0;function sched(){upd();clearTimeout(tmr);tmr=setTimeout(save,420);}async function save(){syncLeftHids();var f=q(\"travelForm\"),st=q(\"trvstat\");st.textContent=\"Uklad\u00e1m\u2026\";try{var body=new URLSearchParams(new FormData(f));var r=await fetch(\"/model/travel/save\",{method:\"POST\",headers:{\"Content-Type\":\"application/x-www-form-urlencoded\",\"X-Travel-Save\":\"1\"},body:body.toString(),credentials:\"same-origin\"});var j=await r.json();if(j&&j.ok){st.textContent=\"Ulo\u017een\u00e9.\";setTimeout(function(){if(st.textContent.indexOf(\"Ulo\")===0)st.textContent=\"\";},1900);}else st.textContent=(j&&j.err)?(\"Chyba: \"+j.err):\"Chyba.\";}catch(e){st.textContent=\"Sie\u0165.\";}}[\"stl\",\"str\",\"ttl\",\"ttr\"].forEach(function(id){var e=q(id);if(e)syncFill(e);});q(\"stl\").value=storedToLeftUi(q(\"stl_h\").value);q(\"ttl\").value=storedToLeftUi(q(\"ttl_h\").value);[\"stl\",\"str\",\"ttl\",\"ttr\"].forEach(function(id){var e=q(id);if(e)syncFill(e);});upd();var f=q(\"travelForm\");f.addEventListener(\"input\",function(ev){if(ev.target&&ev.target.classList&&ev.target.classList.contains(\"travel-range-center\"))syncFill(ev.target);sched();},true);f.addEventListener(\"change\",sched,true);})();</script>");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleModelTrimGet(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    String html;
    www_page_start(html, "Model", "models");
    html += F("<div class=\"card alert err\"><p>SD nie je k dispozícii.</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(200, "text/html; charset=utf-8", html);
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    www_sendRedirect(request, "/");
    return;
  }
  if (!model_web_cfg_ensure_file(dir)) {
    request->send(500, "text/plain", "cfg");
    return;
  }
  if (tx_active_model_dir.length() == 0 || tx_active_model_dir != dir) {
    String html;
    www_page_start(html, "Trim", "models");
    www_emit_model_subnav(html, "trim");
    html += F("<div class=\"card alert err\"><p>Trim je dostupn\u00fd len pre model potvrden\u00fd na vysiela\u010di (aktu\u00edvny na LCD).</p><p><a class=\"site-na\" href=\"/\">Modely</a></p></div>");
    www_page_end(html);
    request->send(403, "text/html; charset=utf-8", html);
    return;
  }
  ModelWebCfg c;
  model_web_cfg_load(dir, c);
  String title = model_web_cfg_list_label(dir);
  String html;
  www_page_start(html, "Trim", "models");
  www_emit_model_subnav(html, "trim");
  if (request->hasParam("ok")) html += F("<div class=\"card alert ok\">Ulo\u017een\u00e9.</div>");
  html += F("<div class=\"card\"><p class=\"breadcrumb\"><a class=\"site-na\" href=\"/\">Modely</a> — <strong>");
  html += www_htmlEscape(title);
  html += F("</strong><span id=\"rxtelem\" class=\"mono\" style=\"font-weight:500\"></span></p>");
  www_emit_model_rxtelem_script(html);
  html += F("<h2>Trim a reverse</h2>");
  html += F("<p class=\"breadcrumb\">Elektrick\u00fd stred riadenia (\u00b5s) a inverzie smeru. Travel nastav\u00ed\u0161 na z\u00e1lo\u017eke <a class=\"site-na\" href=\"/model/travel\">Dr\u00e1ha</a>.</p>");
  html += F("<p id=\"trimstat\" class=\"breadcrumb\" style=\"min-height:1.25em;margin-bottom:.5rem\"></p>");
  html += F("<form id=\"trimForm\" method=\"POST\" action=\"/model/trim/save\" onsubmit=\"return false;\">");
  html += F("<div class=\"cfg-section\"><p class=\"cfg-sub\">Riadenie</p>");
  html += F("<div class=\"axis-block\"><p class=\"axis-head\">Trim stredu (\u00b5s)</p><div class=\"trim-rail\">");
  html += F("<input type=\"range\" name=\"steer_trim_us\" id=\"sttrim\" min=\"1000\" max=\"2000\" step=\"1\" value=\"");
  html += String(c.steer_trim_us);
  html += F("\"></div><p class=\"trim-hint\" id=\"sttrim-h\"></p></div>");
  html += F("<div class=\"rev-row\"><input type=\"checkbox\" name=\"steer_reverse\" id=\"strev\" value=\"1\"");
  if (c.steer_reverse) html += F(" checked");
  html += F("><label for=\"strev\">Reverse riadenia (okolo trimu)</label></div></div>");
  html += F("<div class=\"cfg-section\"><p class=\"cfg-sub\">Plyn</p>");
  html += F("<div class=\"rev-row\"><input type=\"checkbox\" name=\"thr_reverse\" id=\"threv\" value=\"1\"");
  if (c.thr_reverse) html += F(" checked");
  html += F("><label for=\"threv\">Reverse plynu (okolo 1500 \u00b5s)</label></div></div>");
  html += F("</form></div>");
  html += F("<script>(function(){function q(i){return document.getElementById(i);}function upd(){q(\"sttrim-h\").textContent=q(\"sttrim\").value+\" \u00b5s\";}var tmr=0;function sched(){upd();clearTimeout(tmr);tmr=setTimeout(save,420);}async function save(){var f=q(\"trimForm\"),st=q(\"trimstat\");upd();st.textContent=\"Uklad\u00e1m\u2026\";try{var body=new URLSearchParams(new FormData(f));var r=await fetch(\"/model/trim/save\",{method:\"POST\",headers:{\"Content-Type\":\"application/x-www-form-urlencoded\",\"X-Trim-Save\":\"1\"},body:body.toString(),credentials:\"same-origin\"});var j=await r.json();if(j&&j.ok){st.textContent=\"Ulo\u017een\u00e9.\";setTimeout(function(){if(st.textContent.indexOf(\"Ulo\")===0)st.textContent=\"\";},1900);}else st.textContent=(j&&j.err)?(\"Chyba: \"+j.err):\"Chyba\";}catch(e){st.textContent=\"Sie\u0165.\";}}upd();var f=q(\"trimForm\");f.addEventListener(\"input\",sched,true);f.addEventListener(\"change\",sched,true);})();</script>");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleModelTravelSave(AsyncWebServerRequest* request) {
  const bool ajax = request->hasHeader("X-Travel-Save");
  if (!sd_mounted) {
    if (ajax)
      request->send(503, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_sd\"}");
    else
      www_sendRedirect(request, "/");
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    if (ajax)
      request->send(403, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_session\"}");
    else
      www_sendRedirect(request, "/");
    return;
  }
  if (tx_active_model_dir.length() == 0 || tx_active_model_dir != dir) {
    if (ajax)
      request->send(403, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"not_active_tx\"}");
    else
      www_sendRedirect(request, "/model/travel");
    return;
  }
  if (!request->hasParam("steer_travel_l", true) || !request->hasParam("steer_travel_r", true) || !request->hasParam("thr_travel_l", true) ||
      !request->hasParam("thr_travel_r", true)) {
    if (ajax)
      request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"fields\"}");
    else
      www_sendRedirect(request, "/model/travel");
    return;
  }
  ModelWebCfg c;
  model_web_cfg_load(dir, c);
  c.steer_travel_l = model_web_cfg_clamp_travel(request->getParam("steer_travel_l", true)->value().toInt());
  c.steer_travel_r = model_web_cfg_clamp_travel(request->getParam("steer_travel_r", true)->value().toInt());
  c.thr_travel_l = model_web_cfg_clamp_travel(request->getParam("thr_travel_l", true)->value().toInt());
  c.thr_travel_r = model_web_cfg_clamp_travel(request->getParam("thr_travel_r", true)->value().toInt());
  if (!model_web_cfg_save(dir, c)) {
    if (ajax)
      request->send(500, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"sd\"}");
    else {
      String html;
      www_page_start(html, "Model", "models");
      html += F("<div class=\"card alert err\"><p>Z\u00e1pis na SD zlyhal.</p><p><a class=\"site-na\" href=\"/model/travel\">Sp\u00e4\u0165</a></p></div>");
      www_page_end(html);
      request->send(500, "text/html; charset=utf-8", html);
    }
    return;
  }
  tx_model_cfg_bump();
  if (ajax)
    request->send(200, "application/json; charset=utf-8", "{\"ok\":1}");
  else
    www_sendRedirect(request, "/model/travel?ok=1");
}

static void www_handleModelTrimSave(AsyncWebServerRequest* request) {
  const bool ajax = request->hasHeader("X-Trim-Save");
  if (!sd_mounted) {
    if (ajax)
      request->send(503, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_sd\"}");
    else
      www_sendRedirect(request, "/");
    return;
  }
  www_models_scan_sd();
  String dir = www_model_session_dir;
  dir.trim();
  if (!www_model_dir_token_valid(dir) || !www_model_is_listed(dir)) {
    www_model_session_dir = "";
    if (ajax)
      request->send(403, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"no_session\"}");
    else
      www_sendRedirect(request, "/");
    return;
  }
  if (tx_active_model_dir.length() == 0 || tx_active_model_dir != dir) {
    if (ajax)
      request->send(403, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"not_active_tx\"}");
    else
      www_sendRedirect(request, "/model/trim");
    return;
  }
  if (!request->hasParam("steer_trim_us", true)) {
    if (ajax)
      request->send(400, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"fields\"}");
    else
      www_sendRedirect(request, "/model/trim");
    return;
  }
  ModelWebCfg c;
  model_web_cfg_load(dir, c);
  c.steer_trim_us = model_web_cfg_clamp_trim_us(request->getParam("steer_trim_us", true)->value().toInt());
  if (request->hasParam("steer_reverse", true)) {
    String v = request->getParam("steer_reverse", true)->value();
    c.steer_reverse = (v == "1" || v == "on");
  } else
    c.steer_reverse = false;
  if (request->hasParam("thr_reverse", true)) {
    String v = request->getParam("thr_reverse", true)->value();
    c.thr_reverse = (v == "1" || v == "on");
  } else
    c.thr_reverse = false;
  if (!model_web_cfg_save(dir, c)) {
    if (ajax)
      request->send(500, "application/json; charset=utf-8", "{\"ok\":0,\"err\":\"sd\"}");
    else {
      String html;
      www_page_start(html, "Model", "models");
      html += F("<div class=\"card alert err\"><p>Z\u00e1pis na SD zlyhal.</p><p><a class=\"site-na\" href=\"/model/trim\">Sp\u00e4\u0165</a></p></div>");
      www_page_end(html);
      request->send(500, "text/html; charset=utf-8", html);
    }
    return;
  }
  tx_model_cfg_bump();
  if (ajax)
    request->send(200, "application/json; charset=utf-8", "{\"ok\":1}");
  else
    www_sendRedirect(request, "/model/trim?ok=1");
}

// --- Handlery ---

/** Opustenie web session modelu (úpravy cez www) — neovplyvní vysielanie; to riadi len výber na LCD. */
static void www_leave_model_web_session() {
  if (www_model_session_dir.length() == 0) return;
  www_model_session_dir = "";
}

static void www_handleModels(AsyncWebServerRequest* request) {
  www_leave_model_web_session();
  String html;
  www_page_start(html, "Modely", "models");
  if (!sd_mounted) {
    html += F("<div class=\"card alert err\"><p>SD nie je k dispozícii — zoznam modelov neviem načítať.</p>");
    html += F("<p><a class=\"site-na\" href=\"/config/file\">Systém a SD</a> — WiFi a firmware môžu fungovať aj bez SD (zápis do <span class=\"mono\">config.json</span> potrebuje kartu).</p></div>");
    html += F("<div class=\"card\"><h2>Vysielač</h2>");
    if (tx_active_model_dir.length() > 0) {
      html += F("<p class=\"breadcrumb\">Aktívny model (LCD) v pamäti: <strong>");
      html += www_htmlEscape(tx_active_model_dir);
      html += F("</strong></p>");
    } else {
      html += F("<p class=\"breadcrumb\"><strong>Aktívny model:</strong> žiadny — vyber enkóderom po pripojení SD.</p>");
    }
    {
      bool slo = tx_prefs_get_serial_log_enabled();
      html += F("<h3 class=\"section\">Serial</h3><p class=\"breadcrumb\">Podrobné výpisy do sériovej linky (vypnuté šetrí výkon).</p>");
      html += F("<form method=\"POST\" action=\"/config/prefs/serial\"><div class=\"form-row\"><label for=\"sl2\">Režim</label>");
      html += F("<select id=\"sl2\" name=\"serlog\"><option value=\"0\"");
      if (!slo) html += F(" selected");
      html += F(">Vypnuté</option><option value=\"1\"");
      if (slo) html += F(" selected");
      html += F(">Zapnuté</option></select></div><button type=\"submit\">Uložiť</button></form>");
    }
    html += F("</div>");
    www_page_end(html);
    request->send(200, "text/html; charset=utf-8", html);
    return;
  }
  www_models_scan_sd();
  html += F("<div class=\"card\"><h2>Modely</h2>");
  if (www_model_count <= 0) {
    html += F("<p>Žiadne modely. Pridaj adresár modelu cez ");
    html += F("<a class=\"site-na\" href=\"/config/file\">správu súborov</a>.</p>");
  } else {
    html += F("<p class=\"breadcrumb\">Adresár v <span class=\"mono\">/models/</span> musí byť pomenovaný len <span class=\"mono\">A–Z a–z 0–9 _ -</span> (bez medzier). Zobrazený názov nastavíš po otvorení modelu.</p>");
    html += F("<div class=\"model-stack\">");
    for (int i = 1; i <= www_model_count; i++) {
      String d = www_model_names[i];
      d.trim();
      String lbl = model_web_cfg_list_label(d);
      html += F("<form class=\"model-open\" method=\"POST\" action=\"/model/open\"><input type=\"hidden\" name=\"model_dir\" value=\"");
      html += www_htmlEscape(d);
      html += F("\"><button type=\"submit\">");
      html += www_htmlEscape(lbl);
      html += F(" <span class=\"mono\" style=\"font-size:.78rem;color:var(--muted)\">(");
      html += www_htmlEscape(d);
      html += F(")</span></button></form>");
    }
    html += F("</div>");
  }
  html += F("</div>");

  html += F("<div class=\"card\"><h2>Vysielač</h2>");
  if (tx_active_model_dir.length() > 0) {
    html += F("<p class=\"breadcrumb\">Aktívny model (LCD): <strong>");
    if (sd_mounted)
      html += www_htmlEscape(model_web_cfg_list_label(tx_active_model_dir));
    else
      html += www_htmlEscape(tx_active_model_dir);
    html += F("</strong>");
    if (sd_mounted) {
      html += F(" <span class=\"mono\">(");
      html += www_htmlEscape(tx_active_model_dir);
      html += F(")</span>");
    }
    html += F("</p>");
  } else {
    html += F("<p class=\"breadcrumb\"><strong>Aktívny model:</strong> žiadny — vyber enkóderom na displeji.</p>");
  }
  {
    bool slo = tx_prefs_get_serial_log_enabled();
    html += F("<h3 class=\"section\">Serial</h3><p class=\"breadcrumb\">Podrobné výpisy do sériovej linky (vypnuté šetrí výkon).</p>");
    html += F("<form method=\"POST\" action=\"/config/prefs/serial\"><div class=\"form-row\"><label for=\"sl\">Režim</label>");
    html += F("<select id=\"sl\" name=\"serlog\"><option value=\"0\"");
    if (!slo) html += F(" selected");
    html += F(">Vypnuté</option><option value=\"1\"");
    if (slo) html += F(" selected");
    html += F(">Zapnuté</option></select></div><button type=\"submit\">Uložiť</button></form>");
  }
  html += F("</div>");

  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleSerialLogSave(AsyncWebServerRequest* request) {
  bool on = false;
  if (request->hasArg("serlog")) {
    String v = request->arg("serlog");
    v.trim();
    on = (v == "1" || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("true"));
  }
  tx_prefs_set_serial_log_enabled(on);
  www_sendRedirect(request, "/");
}

static void www_handleConfigHub(AsyncWebServerRequest* request) {
  www_sendRedirect(request, "/config/file");
}

static void www_handleFile(AsyncWebServerRequest* request) {
  www_leave_model_web_session();
  if (!www_require_sd(request)) return;
  String path = www_normalizePath(request->hasParam("path") ? request->getParam("path")->value() : "/");
  if (path.length() == 0) path = "/";

  File dir = SD.open(sd_path(path));
  if (!dir || !dir.isDirectory()) {
    String html;
    www_page_start(html, "Adresár", "file");
    html += F("<div class=\"card\"><h2>Neexistuje</h2><p>Požadovaný adresár sa na SD nenašiel.</p>");
    html += F("<p><a class=\"site-na\" href=\"/config/file\">Späť na koreň</a></p></div>");
    www_page_end(html);
    request->send(404, "text/html; charset=utf-8", html);
    return;
  }

  String up = www_parentPath(path);
  String html;
  html.reserve(8192);
  www_page_start(html, "Súbory", "file");

  html += F("<div class=\"card\"><p class=\"breadcrumb\">Cesta: <strong>");
  html += www_htmlEscape(path);
  html += F("</strong> · <a href=\"/config/file?path=");
  html += www_uriEncodePathQuery(up);
  html += F("\">Nadradený</a> · <a href=\"/config/file/api/list?path=");
  html += www_uriEncodePathQuery(path);
  html += F("\">JSON</a></p>");
  html += F("<h2 class=\"section\">Nový adresár</h2><form method=\"POST\" action=\"/config/file/mkdir\">");
  html += F("<input type=\"hidden\" name=\"parent\" value=\"");
  html += www_htmlEscape(path);
  html += F("\"><div class=\"form-row\"><label for=\"dn\">Názov</label><input id=\"dn\" type=\"text\" name=\"name\" placeholder=\"podadresár\" required pattern=\"[^/\\\\]+\"></div>");
  html += F("<button type=\"submit\">Vytvoriť</button></form>");
  html += F("<h2 class=\"section\" style=\"margin-top:1rem\">Nahrať súbor</h2><form method=\"POST\" action=\"/config/file/upload?path=");
  html += www_uriEncodePathQuery(path);
  html += F("\" enctype=\"multipart/form-data\"><div class=\"form-row\"><label for=\"up\">Súbor</label><input id=\"up\" type=\"file\" name=\"data\" required></div>");
  html += F("<button type=\"submit\">Nahrať</button></form></div>");

  html += F("<div class=\"card\"><h2>Obsah</h2><div class=\"tbl-wrap\"><table><thead><tr><th>Názov</th><th>Typ</th><th>Veľkosť</th><th>Akcie</th></tr></thead><tbody>");

  File entry = dir.openNextFile();
  while (entry) {
    String fullPath = www_childPath(path, String(entry.name()));
    String showName = www_entryBaseName(fullPath);
    bool isDir = entry.isDirectory();
    size_t sz = isDir ? 0 : entry.size();
    entry.close();

    html += F("<tr><td>");
    if (isDir) {
      html += F("<a href=\"/config/file?path=");
      html += www_uriEncodePathQuery(fullPath);
      html += F("\">");
      html += www_htmlEscape(showName);
      html += F("</a>");
    } else {
      html += F("<a href=\"/config/file/download?path=");
      html += www_uriEncodePathQuery(fullPath);
      html += F("\">");
      html += www_htmlEscape(showName);
      html += F("</a>");
    }
    html += F("</td><td>");
    html += isDir ? F("Adresár") : F("Súbor");
    html += F("</td><td>");
    if (!isDir) html += String(sz);
    html += F("</td><td><div class=\"btnrow\">");
    html += F("<form class=\"btnrow\" method=\"POST\" action=\"/config/file/delete\" onsubmit=\"return confirm('Zmazať táto položka?');\">");
    html += F("<input type=\"hidden\" name=\"path\" value=\"");
    html += www_htmlEscape(fullPath);
    html += F("\"><button type=\"submit\">Zmazať</button></form>");
    if (!isDir) {
      html += F("<form class=\"btnrow\" method=\"POST\" action=\"/config/file/rename\">");
      html += F("<input type=\"hidden\" name=\"from\" value=\"");
      html += www_htmlEscape(fullPath);
      html += F("\"><input type=\"text\" name=\"to\" placeholder=\"nový názov\" required pattern=\"[^/\\\\]+\" style=\"max-width:9rem\">");
      html += F("<button type=\"submit\">Premenovať</button></form>");
    }
    html += F("</div></td></tr>");

    entry = dir.openNextFile();
  }
  dir.close();

  html += F("</tbody></table></div></div>");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleDownload(AsyncWebServerRequest* request) {
  if (!www_require_sd(request)) return;
  if (!request->hasParam("path")) {
    request->send(400, "text/plain", "Missing path");
    return;
  }
  String path = www_normalizePath(request->getParam("path")->value());
  if (path.length() == 0) {
    request->send(400, "text/plain", "Bad path");
    return;
  }
  if (!SD.exists(sd_path(path))) {
    request->send(404, "text/plain", "Not found");
    return;
  }
  File f = SD.open(sd_path(path), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    request->send(400, "text/plain", "Not a file");
    return;
  }
  f.close();
  AsyncWebServerResponse* response = request->beginResponse(SD, sd_path(path), "application/octet-stream", true);
  response->addHeader("Content-Disposition", "attachment");
  request->send(response);
}

static void www_handleApiList(AsyncWebServerRequest* request) {
  if (!www_require_sd(request)) return;
  String path = www_normalizePath(request->hasParam("path") ? request->getParam("path")->value() : "/");
  if (path.length() == 0) path = "/";
  File dir = SD.open(sd_path(path));
  if (!dir || !dir.isDirectory()) {
    request->send(404, "application/json", "[]");
    return;
  }
  String json = "[";
  bool first = true;
  File e = dir.openNextFile();
  while (e) {
    String fullPath = www_childPath(path, String(e.name()));
    String showName = www_entryBaseName(fullPath);
    bool isDir = e.isDirectory();
    size_t sz = isDir ? 0 : e.size();
    e.close();
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"";
    json += www_jsonEscape(showName);
    json += "\",\"path\":\"";
    json += www_jsonEscape(fullPath);
    json += "\",\"dir\":";
    json += isDir ? "true" : "false";
    json += ",\"size\":";
    json += String(sz);
    json += "}";
    e = dir.openNextFile();
  }
  dir.close();
  json += "]";
  request->send(200, "application/json; charset=utf-8", json);
}

static void www_handleDelete(AsyncWebServerRequest* request) {
  if (!www_require_sd(request)) return;
  if (!request->hasParam("path", true)) {
    request->send(400, "text/plain", "Missing path");
    return;
  }
  String path = www_normalizePath(request->getParam("path", true)->value());
  if (path.length() == 0 || path == "/") {
    request->send(403, "text/plain", "Refused");
    return;
  }
  if (!www_removeRecursive(path)) {
    String html_500;
    www_page_start(html_500, "Chyba", "file");
    html_500 += F("<div class=\"card alert err\">Zmazanie zlyhalo.</div><p><a class=\"site-na\" href=\"/config/file\">Na prehliadač</a></p>");
    www_page_end(html_500);
    request->send(500, "text/html; charset=utf-8", html_500);
    return;
  }
  www_sendRedirect(request, "/config/file?path=" + www_uriEncodePathQuery(www_parentPath(path)));
}

static void www_handleMkdir(AsyncWebServerRequest* request) {
  if (!www_require_sd(request)) return;
  if (!request->hasParam("parent", true) || !request->hasParam("name", true)) {
    request->send(400, "text/plain", "Missing fields");
    return;
  }
  String parent = www_normalizePath(request->getParam("parent", true)->value());
  if (parent.length() == 0) parent = "/";
  String name = request->getParam("name", true)->value();
  name.trim();
  if (name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
    request->send(400, "text/plain", "Bad name");
    return;
  }
  String full = parent;
  if (!full.endsWith("/")) full += "/";
  full += name;
  if (!SD.mkdir(sd_path(full).c_str())) {
    String html_500;
    www_page_start(html_500, "Chyba", "file");
    html_500 += F("<div class=\"card alert err\">Nepodarilo sa vytvoriť adresár.</div>");
    www_page_end(html_500);
    request->send(500, "text/html; charset=utf-8", html_500);
    return;
  }
  www_sendRedirect(request, "/config/file?path=" + www_uriEncodePathQuery(parent));
}

static void www_handleRename(AsyncWebServerRequest* request) {
  if (!www_require_sd(request)) return;
  if (!request->hasParam("from", true) || !request->hasParam("to", true)) {
    request->send(400, "text/plain", "Missing fields");
    return;
  }
  String from = www_normalizePath(request->getParam("from", true)->value());
  String toName = request->getParam("to", true)->value();
  toName.trim();
  if (from.length() == 0 || toName.length() == 0 || toName.indexOf('/') >= 0 || toName.indexOf('\\') >= 0) {
    request->send(400, "text/plain", "Bad path/name");
    return;
  }
  String parent = www_parentPath(from);
  String to = parent;
  if (!to.endsWith("/")) to += "/";
  to += toName;
  if (!SD.rename(sd_path(from).c_str(), sd_path(to).c_str())) {
    String html_500;
    www_page_start(html_500, "Chyba", "file");
    html_500 += F("<div class=\"card alert err\">Premenovanie zlyhalo.</div>");
    www_page_end(html_500);
    request->send(500, "text/html; charset=utf-8", html_500);
    return;
  }
  www_sendRedirect(request, "/config/file?path=" + www_uriEncodePathQuery(parent));
}

static String www_upload_dir;
static File www_upload_file;

static void www_handleUploadDone(AsyncWebServerRequest* request) {
  if (!sd_mounted) {
    www_send_sd_missing(request);
    return;
  }
  String dir = "/";
  if (request->hasParam("path", false)) {
    String q = www_normalizePath(request->getParam("path", false)->value());
    if (q.length()) dir = q;
  }
  if (www_upload_dir.length() > 0) dir = www_upload_dir;
  www_sendRedirect(request, "/config/file?path=" + www_uriEncodePathQuery(dir));
}

static void www_handleUploadFile(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (!sd_mounted) return;
  if (index == 0) {
    if (request->hasParam("path", false))
      www_upload_dir = www_normalizePath(request->getParam("path", false)->value());
    else
      www_upload_dir = "/";
    if (www_upload_dir.length() == 0) www_upload_dir = "/";
    String fn = filename;
    if (fn.length() == 0) return;
    fn = www_entryBaseName(fn);
    if (fn.indexOf('/') >= 0 || fn.indexOf('\\') >= 0) return;
    String full = www_upload_dir;
    if (!full.endsWith("/")) full += "/";
    full += fn;
    www_upload_file = SD.open(sd_path(full), FILE_WRITE);
    if (!www_upload_file) Serial.println("Upload: nepodarilo sa vytvorit subor");
  }
  if (len && www_upload_file) www_upload_file.write(data, len);
  if (final) {
    if (www_upload_file) www_upload_file.close();
  }
}

static void www_handleWifiGet(AsyncWebServerRequest* request) {
  www_leave_model_web_session();
  String html;
  www_page_start(html, "Sieť (WiFi)", "wifi");

  if (request->hasParam("saved"))
    html += F("<div class=\"card alert ok\">Uložené na SD. Po zmene AP sa pripoj k novej sieti; odporúčaný je reštart zariadenia.</div>");
  if (request->hasParam("err"))
    html += F("<div class=\"card alert err\">Uloženie zlyhalo — skontroluj SD, kartu (FAT32) a či ide zapisovať. Cieľový súbor na karte: <span class=\"mono\">/config/config.json</span> (nie URL v prehliadači).</div>");
  if (!sd_mounted)
    html += F("<div class=\"card alert err\">SD nie je pripojená — údaje sú len v pamäti; zápis do <span class=\"mono\">config.json</span> nepôjde.</div>");

  html += F("<div class=\"card\"><h2>Siete</h2><form method=\"POST\" action=\"/config/wifi/save\">");
  html += F("<div class=\"form-row\"><label for=\"ap_ssid\">AP SSID (vysielač ESP)</label>");
  html += F("<input id=\"ap_ssid\" type=\"text\" name=\"ap_ssid\" maxlength=\"32\" required value=\"");
  html += www_htmlEscape(wifi_cfg.ap_ssid);
  html += F("\"></div>");
  html += F("<div class=\"form-row\"><label for=\"ap_pw\">AP heslo (min. 8 znakov pre WPA2)</label>");
  html += F("<input id=\"ap_pw\" type=\"password\" name=\"ap_password\" maxlength=\"64\" value=\"");
  html += www_htmlEscape(wifi_cfg.ap_password);
  html += F("\"></div>");
  html += F("<div class=\"form-row\"><label for=\"sta_ssid\">STA SSID (domáca WiFi, voliteľné)</label>");
  html += F("<input id=\"sta_ssid\" type=\"text\" name=\"sta_ssid\" maxlength=\"32\" value=\"");
  html += www_htmlEscape(wifi_cfg.sta_ssid);
  html += F("\"></div>");
  html += F("<div class=\"form-row\"><label for=\"sta_pw\">STA heslo</label>");
  html += F("<input id=\"sta_pw\" type=\"password\" name=\"sta_password\" maxlength=\"64\" value=\"");
  html += www_htmlEscape(wifi_cfg.sta_password);
  html += F("\"></div>");
  html += F("<button type=\"submit\">Uložiť do <span class=\"mono\">/config/config.json</span></button></form>");
  html += F("<p class=\"breadcrumb\" style=\"margin-top:1rem;margin-bottom:0\">Prázdne STA SSID = klient sa nespustí. Predvolený AP po prvom štarte: <span class=\"mono\">transmitter</span> / <span class=\"mono\">12345678</span>.</p>");
  html += F("</div>");

  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleWifiSave(AsyncWebServerRequest* request) {
  // hasArg / arg: spoľahlivejšie ako hasParam(...,true); voliteľné STA polia niektoré prehliadače v POST vynechajú.
  if (!request->hasArg("ap_ssid")) {
    www_sendRedirect(request, "/config/wifi?err=1");
    return;
  }
  wifi_cfg.ap_ssid = request->arg("ap_ssid");
  wifi_cfg.ap_password = request->hasArg("ap_password") ? request->arg("ap_password") : String();
  wifi_cfg.sta_ssid = request->hasArg("sta_ssid") ? request->arg("sta_ssid") : String();
  wifi_cfg.sta_password = request->hasArg("sta_password") ? request->arg("sta_password") : String();
  wifi_cfg.ap_ssid.trim();
  wifi_cfg.sta_ssid.trim();

  if (wifi_cfg.ap_ssid.length() == 0) {
    www_sendRedirect(request, "/config/wifi?err=1");
    return;
  }

  if (!sd_mounted || !wifi_cfg_ensure_dirs() || !wifi_cfg_save()) {
    Serial.println("WiFi: ulozenie konfig zlyhalo (SD / mkdir / zapis).");
    www_sendRedirect(request, "/config/wifi?err=1");
    return;
  }
  wifi_cfg_loaded_from_sd = true;
  www_sendRedirect(request, "/config/wifi?saved=1");
}

static void www_handleFirmwareGet(AsyncWebServerRequest* request) {
  www_leave_model_web_session();
  String html;
  www_page_start(html, "Firmware", "fw");
  html += F("<div class=\"card\"><h2>Aktualizácia z GitHubu</h2>");
  html += F("<p class=\"breadcrumb\">Potrebné je <strong>STA WiFi</strong> s internetom (v <a class=\"site-na\" href=\"/config/wifi\">Sieť</a>). Release asset musí mať názov <span class=\"mono\">firmware-&lt;40×hex&gt;.bin</span> (z CI).</p>");
  html += F("<p class=\"breadcrumb\">Tento build · Git <span class=\"mono\">");
  html += String(FIRMWARE_GIT_SHA_FULL).substring(0, 7);
  html += F("</span> · ");
  html += GITHUB_RELEASE_OWNER;
  html += F("/");
  html += GITHUB_RELEASE_REPO;
  html += F("</p>");
  html += F("<div class=\"btnrow\" style=\"margin:.5rem 0\"><button type=\"button\" id=\"gh_chk\">Skontrolovať (online)</button>");
  html += F("<button type=\"button\" id=\"gh_inst\" disabled>Nainštalovať z GitHubu</button></div>");
  html += F("<p id=\"gh_msg\" class=\"breadcrumb\" style=\"min-height:1.25em\"></p>");
  html += F("<script>(function(){var msg=document.getElementById(\"gh_msg\"),inst=document.getElementById(\"gh_inst\");function poll(){fetch(\"/config/firmware/github/status\",{credentials:\"same-origin\"}).then(function(r){return r.json();}).then(function(s){if(s&&s.busy&&s.phase===1)msg.textContent=\"Sťahujem… \"+(s.written||0)+\" / \"+(s.total||\"?\")+\" B\";}).catch(function(){});}document.getElementById(\"gh_chk\").onclick=async function(){msg.textContent=\"Kontrolujem…\";inst.disabled=true;try{var r=await fetch(\"/config/firmware/github/check\",{credentials:\"same-origin\"});var j=await r.json();if(!j.ok){msg.textContent=j.hint||(j.err||\"Chyba\")+(j.code?\" (\"+j.code+\")\":\"\");return;}msg.textContent=j.update_available?(\"Novší firmware: \"+j.remote_sha.substring(0,7)+\" (aktuálny \"+j.current_sha.substring(0,7)+\")\"):\"Si na najnovšom commite (\"+j.current_sha.substring(0,7)+\").\";inst.disabled=!j.update_available;}catch(e){msg.textContent=\"Sieť alebo odpoveď.\";}};inst.onclick=async function(){if(!confirm(\"Naozaj nahrať firmvér z GitHubu? Zariadenie sa reštartuje.\"))return;msg.textContent=\"Spúšťam OTA…\";try{var r=await fetch(\"/config/firmware/github/install\",{method:\"POST\",credentials:\"same-origin\"});var j=await r.json();if(j.ok){msg.textContent=\"Sťahujem a zapisujem flash…\";var iv=setInterval(function(){poll();},400);poll();setTimeout(function(){clearInterval(iv);},120000);}else msg.textContent=j.err||\"Chyba\";}catch(e){msg.textContent=\"Sieť.\";}};})();</script></div>");

  html += F("<div class=\"card\"><h2>Nahrávanie .bin z počítača</h2>");
  html += F("<p class=\"breadcrumb\">Vyber súbor <span class=\"mono\">.bin</span> z <strong>Sketch → Export kompilovaného binárneho súboru</strong> (rovnaká doska / partície ako pri bežnom nahrávaní cez USB).</p>");
  html += F("<div class=\"card alert err\" style=\"margin-bottom:1rem\">Nesprávny súbor môže zariadenie znefunkčniť — over si model (ESP32-S3) a veľkosť flash.</div>");
  html += F("<form method=\"POST\" action=\"/config/firmware/update\" enctype=\"multipart/form-data\">");
  html += F("<div class=\"form-row\"><label for=\"fw\">Súbor firmvéru (.bin)</label>");
  html += F("<input id=\"fw\" type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\" required></div>");
  html += F("<button type=\"submit\">Nahrať a flashnúť</button></form></div>");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
}

static void www_handleFirmwareDone(AsyncWebServerRequest* request) {
  if (Update.hasError()) {
    String html;
    www_page_start(html, "Firmware", "fw");
    html += F("<div class=\"card alert err\"><strong>Chyba:</strong> zápis alebo veľkosť firmvéru. Skús iný .bin.</div>");
    html += F("<p><a class=\"site-na\" href=\"/config/firmware\">Späť</a></p>");
    www_page_end(html);
    request->send(500, "text/html; charset=utf-8", html);
    Update.printError(Serial);
    return;
  }
  String html;
  www_page_start(html, "Firmware", "fw");
  www_page_end(html);
  request->send(200, "text/html; charset=utf-8", html);
  delay(1000);
  ESP.restart();
}

static void www_handleFirmwareChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  (void)request;
  if (index == 0) {
    if (Update.isRunning()) Update.abort();
    Serial.printf("OTA: subor '%s', zapis flash...\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      return;
    }
  }
  if (Update.hasError()) return;
  if (len) {
    size_t w = Update.write(data, len);
    if (w != len) {
      Serial.printf("OTA write: ocakavane %u, zapisane %u\n", (unsigned)len, (unsigned)w);
    }
  }
  if (final) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    }
  }
}

inline void www_begin() {
  www_server.on("/model/open", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelOpen(r); });
  www_server.on("/model/cfg", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelCfgGet(r); });
  www_server.on("/model/cfg/save", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelCfgSave(r); });
  www_server.on("/model/espnow/scan", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelEspnowScan(r); });
  www_server.on("/model/rxtelem", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelRxTelem(r); });
  www_server.on("/model/cal/live", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelCalLive(r); });
  www_server.on("/model/cal/step", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelCalStep(r); });
  www_server.on("/model/cal", HTTP_GET, [](AsyncWebServerRequest* r) { www_sendRedirect(r, "/config/cal"); });
  www_server.on("/model/travel/save", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelTravelSave(r); });
  www_server.on("/model/travel", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelTravelGet(r); });
  www_server.on("/model/trim/save", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelTrimSave(r); });
  www_server.on("/model/trim", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelTrimGet(r); });
  www_server.on("/model", HTTP_GET, [](AsyncWebServerRequest* r) { www_sendRedirect(r, "/"); });
  www_server.on("/", HTTP_GET, www_handleModels);

  // ESPAsyncWebServer: prvý zhodný handler vyhrá. Presná cesta "/config"
  // musí byť AŽ za "/config/...", inak by sa všetky podstránky chytali na hub.
  www_server.on("/config/file", HTTP_GET, www_handleFile);
  www_server.on("/config/file/download", HTTP_GET, www_handleDownload);
  www_server.on("/config/file/api/list", HTTP_GET, www_handleApiList);
  www_server.on("/config/file/delete", HTTP_POST, www_handleDelete);
  www_server.on("/config/file/mkdir", HTTP_POST, www_handleMkdir);
  www_server.on("/config/file/rename", HTTP_POST, www_handleRename);
  www_server.on("/config/file/upload", HTTP_POST, www_handleUploadDone, www_handleUploadFile);

  www_server.on("/config/cal", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelCalGet(r); });
  www_server.on("/config/cal/live", HTTP_GET, [](AsyncWebServerRequest* r) { www_handleModelCalLive(r); });
  www_server.on("/config/cal/step", HTTP_POST, [](AsyncWebServerRequest* r) { www_handleModelCalStep(r); });
  www_server.on("/config/prefs/serial", HTTP_POST, www_handleSerialLogSave);

  www_server.on("/config/wifi", HTTP_GET, www_handleWifiGet);
  www_server.on("/config/wifi/save", HTTP_POST, www_handleWifiSave);
  www_server.on("/config/save", HTTP_POST, www_handleWifiSave);

  www_server.on("/config/firmware", HTTP_GET, www_handleFirmwareGet);
  www_server.on("/config/firmware/update", HTTP_POST, www_handleFirmwareDone, www_handleFirmwareChunk);
  www_github_ota_begin();

  www_server.on("/config", HTTP_GET, www_handleConfigHub);

  www_server.on("/file", HTTP_GET, www_legacy_redirect_under_cfg_file);
  www_server.on("/file/download", HTTP_GET, www_legacy_redirect_under_cfg_file);
  www_server.on("/file/api/list", HTTP_GET, www_legacy_redirect_under_cfg_file);
  www_server.on("/file/delete", HTTP_POST, www_handleDelete);
  www_server.on("/file/mkdir", HTTP_POST, www_handleMkdir);
  www_server.on("/file/rename", HTTP_POST, www_handleRename);
  www_server.on("/file/upload", HTTP_POST, www_handleUploadDone, www_handleUploadFile);

  www_server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest* r) { www_sendRedirect(r, "/config/firmware"); });
  www_server.on("/firmware/update", HTTP_POST, www_handleFirmwareDone, www_handleFirmwareChunk);

  www_server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  www_server.begin();
}

#endif
