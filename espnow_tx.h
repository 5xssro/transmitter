#ifndef ESPNOW_TX_H
#define ESPNOW_TX_H

/*
  ESP-NOW: DISCOVER → ANNOUNCE (+ napätie batérie, 5 úrovní signálu).
  PAIR_BIND, CONTROL; prijímač odpovedá na CONTROL paketom TELEM (batéria + signál).
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <string.h>

inline void txesp_mac_to_str(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const uint8_t TXESP_MAGIC[4] = {'T', 'X', 'E', 'N'};
static const uint8_t TXESP_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum {
  TXESP_DISCOVER = 1,
  TXESP_ANNOUNCE = 2,
  TXESP_PAIR_BIND = 4,
  TXESP_CONTROL = 8,
  TXESP_TELEM = 16,
};

struct __attribute__((packed)) TxEspPacket {
  uint8_t magic[4];
  uint8_t type;
  uint8_t pad[3];
};

/** ANNOUNCE: 45 B vrátane batt_mv a sig_level (0–4). */
struct __attribute__((packed)) TxEspAnnouncePkt {
  uint8_t magic[4];
  uint8_t type;
  uint8_t pad[3];
  uint8_t ap_name_len;
  char ap_name[32];
  uint16_t batt_mv;
  uint8_t sig_level;
  uint8_t _pad_tail;
};

struct __attribute__((packed)) TxEspControlPkt {
  uint8_t magic[4];
  uint8_t type;
  uint8_t pad[3];
  uint16_t steering_us;
  uint16_t throttle_us;
};

struct __attribute__((packed)) TxEspTelemPkt {
  uint8_t magic[4];
  uint8_t type;
  uint8_t pad[3];
  uint16_t batt_mv;
  uint8_t sig_level;
  uint8_t _pad_tail;
};

#define TXESP_SCAN_MAX 16
#define TXESP_APNAME_MAX 32
#define TXESP_ANNOUNCE_FULL_LEN ((int)sizeof(TxEspAnnouncePkt))

static bool txesp_inited = false;
static portMUX_TYPE txesp_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool txesp_scan_armed = false;
static uint8_t txesp_found_mac[TXESP_SCAN_MAX][6];
static char txesp_found_name[TXESP_SCAN_MAX][TXESP_APNAME_MAX + 1];
static uint16_t txesp_found_batt[TXESP_SCAN_MAX];
static uint8_t txesp_found_sig[TXESP_SCAN_MAX];
static int txesp_found_count = 0;

static portMUX_TYPE txesp_telem_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t txesp_telem_mac[6];
static uint16_t txesp_telem_batt_mv = 0;
static uint8_t txesp_telem_sig_level = 0;
static uint32_t txesp_telem_ms = 0;

inline void txesp_store_telem(const uint8_t mac[6], uint16_t batt_mv, uint8_t sig_level) {
  if (!mac) return;
  if (sig_level > 4) sig_level = 4;
  portENTER_CRITICAL(&txesp_telem_mux);
  memcpy(txesp_telem_mac, mac, 6);
  txesp_telem_batt_mv = batt_mv;
  txesp_telem_sig_level = sig_level;
  txesp_telem_ms = millis();
  portEXIT_CRITICAL(&txesp_telem_mux);
}

/** Zruší cache (iný model / MAC po skene — inak snapshot pre aktuálnu MAC zlyháva). */
inline void txesp_telem_clear() {
  portENTER_CRITICAL(&txesp_telem_mux);
  txesp_telem_ms = 0;
  memset(txesp_telem_mac, 0, 6);
  txesp_telem_batt_mv = 0;
  txesp_telem_sig_level = 0;
  portEXIT_CRITICAL(&txesp_telem_mux);
}

/** Posledná telemetria od prijímača s danou MAC (po spárovaní). */
inline void txesp_telem_snapshot_for_mac(const uint8_t mac[6], uint16_t* batt_mv, uint8_t* sig_level, uint32_t* age_ms,
                                         bool* have_data) {
  if (batt_mv) *batt_mv = 0;
  if (sig_level) *sig_level = 0;
  if (age_ms) *age_ms = 999999;
  if (have_data) *have_data = false;
  if (!mac) return;
  portENTER_CRITICAL(&txesp_telem_mux);
  if (txesp_telem_ms == 0 || memcmp(txesp_telem_mac, mac, 6) != 0) {
    portEXIT_CRITICAL(&txesp_telem_mux);
    return;
  }
  if (batt_mv) *batt_mv = txesp_telem_batt_mv;
  if (sig_level) *sig_level = txesp_telem_sig_level;
  if (age_ms) *age_ms = millis() - txesp_telem_ms;
  if (have_data) *have_data = true;
  portEXIT_CRITICAL(&txesp_telem_mux);
}

static void txesp_pull_announce_name(const uint8_t* data, int len, char out_name[TXESP_APNAME_MAX + 1]) {
  out_name[0] = 0;
  if (len < (int)sizeof(TxEspPacket)) return;
  if (len == (int)sizeof(TxEspPacket)) return;
  uint8_t n = data[8];
  if (n > TXESP_APNAME_MAX) n = TXESP_APNAME_MAX;
  if (len < 9 + n) n = (len > 9) ? (uint8_t)(len - 9) : 0;
  if (n == 0) return;
  memcpy(out_name, data + 9, n);
  out_name[n] = 0;
}

static bool txesp_pull_announce_extras(const uint8_t* data, int len, uint16_t* batt_mv, uint8_t* sig_level) {
  if (!batt_mv || !sig_level) return false;
  *batt_mv = 0;
  *sig_level = 0;
  if (len < TXESP_ANNOUNCE_FULL_LEN) return false;
  uint16_t bm = (uint16_t)data[41] | ((uint16_t)data[42] << 8);
  uint8_t sg = data[43];
  if (sg > 4) sg = 4;
  *batt_mv = bm;
  *sig_level = sg;
  return true;
}

static void txesp_on_recv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!mac || !data || len < (int)sizeof(TxEspPacket)) return;
  TxEspPacket p;
  memcpy(&p, data, sizeof(p));
  if (memcmp(p.magic, TXESP_MAGIC, 4) != 0) return;

  if (p.type == TXESP_TELEM) {
    if (len < (int)sizeof(TxEspTelemPkt)) return;
    TxEspTelemPkt t;
    memcpy(&t, data, sizeof(t));
    txesp_store_telem(mac, t.batt_mv, t.sig_level);
    return;
  }

  if (p.type != TXESP_ANNOUNCE) return;

  char name_buf[TXESP_APNAME_MAX + 1];
  txesp_pull_announce_name(data, len, name_buf);
  uint16_t bm = 0;
  uint8_t sg = 0;
  bool has_ex = txesp_pull_announce_extras(data, len, &bm, &sg);

  portENTER_CRITICAL(&txesp_mux);
  if (!txesp_scan_armed) {
    portEXIT_CRITICAL(&txesp_mux);
    if (has_ex) txesp_store_telem(mac, bm, sg);
    return;
  }

  for (int i = 0; i < txesp_found_count; i++) {
    if (memcmp(txesp_found_mac[i], mac, 6) == 0) {
      if (strlen(name_buf) > strlen(txesp_found_name[i])) strcpy(txesp_found_name[i], name_buf);
      if (has_ex) {
        txesp_found_batt[i] = bm;
        txesp_found_sig[i] = sg;
      }
      portEXIT_CRITICAL(&txesp_mux);
      if (has_ex) txesp_store_telem(mac, bm, sg);
      return;
    }
  }

  if (txesp_found_count < TXESP_SCAN_MAX) {
    memcpy(txesp_found_mac[txesp_found_count], mac, 6);
    strcpy(txesp_found_name[txesp_found_count], name_buf);
    txesp_found_batt[txesp_found_count] = has_ex ? bm : 0;
    txesp_found_sig[txesp_found_count] = has_ex ? sg : 0;
    txesp_found_count++;
  }
  portEXIT_CRITICAL(&txesp_mux);
  if (has_ex) txesp_store_telem(mac, bm, sg);
}

static bool txesp_add_broadcast_peer() {
  if (esp_now_is_peer_exist(TXESP_BROADCAST)) return true;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, TXESP_BROADCAST, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_AP;
  esp_err_t e = esp_now_add_peer(&peer);
  if (e == ESP_OK) return true;
  peer.ifidx = WIFI_IF_STA;
  e = esp_now_add_peer(&peer);
  return e == ESP_OK;
}

static bool txesp_add_unicast_peer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_AP;
  esp_err_t e = esp_now_add_peer(&peer);
  if (e == ESP_OK) return true;
  peer.ifidx = WIFI_IF_STA;
  return esp_now_add_peer(&peer) == ESP_OK;
}

inline bool tx_espnow_begin() {
  if (txesp_inited) return true;
  if (esp_now_init() != ESP_OK) {
    Serial.println(F("ESP-NOW: esp_now_init zlyhalo"));
    return false;
  }
  esp_now_register_recv_cb(txesp_on_recv);
  txesp_inited = true;
  Serial.println(F("ESP-NOW: init OK (discover / pair / telem)"));
  return true;
}

inline bool tx_espnow_send_discover() {
  if (!txesp_inited) return false;
  if (!txesp_add_broadcast_peer()) {
    Serial.println(F("ESP-NOW: broadcast peer"));
    return false;
  }
  TxEspPacket pkt{};
  memcpy(pkt.magic, TXESP_MAGIC, 4);
  pkt.type = TXESP_DISCOVER;
  return esp_now_send(TXESP_BROADCAST, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt)) == ESP_OK;
}

inline bool tx_espnow_send_pair_bind(const uint8_t rx_mac[6]) {
  if (!txesp_inited || !tx_espnow_begin()) return false;
  if (!txesp_add_unicast_peer(rx_mac)) {
    Serial.println(F("ESP-NOW: pair peer"));
    return false;
  }
  TxEspPacket pkt{};
  memcpy(pkt.magic, TXESP_MAGIC, 4);
  pkt.type = TXESP_PAIR_BIND;
  for (int i = 0; i < 4; i++) {
    esp_now_send(rx_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
    delay(25);
  }
  Serial.println(F("ESP-NOW: PAIR_BIND odoslany"));
  return true;
}

inline bool tx_espnow_send_control(const uint8_t rx_mac[6], uint16_t steering_us, uint16_t throttle_us) {
  if (!txesp_inited || !rx_mac) return false;
  if (!txesp_add_unicast_peer(rx_mac)) return false;
  TxEspControlPkt pkt{};
  memcpy(pkt.magic, TXESP_MAGIC, 4);
  pkt.type = TXESP_CONTROL;
  pkt.pad[0] = pkt.pad[1] = pkt.pad[2] = 0;
  pkt.steering_us = steering_us;
  pkt.throttle_us = throttle_us;
  return esp_now_send(rx_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt)) == ESP_OK;
}

inline void tx_espnow_scan_run() {
  portENTER_CRITICAL(&txesp_mux);
  txesp_found_count = 0;
  for (int i = 0; i < TXESP_SCAN_MAX; i++) {
    txesp_found_name[i][0] = 0;
    txesp_found_batt[i] = 0;
    txesp_found_sig[i] = 0;
  }
  portEXIT_CRITICAL(&txesp_mux);
  txesp_scan_armed = true;
  for (int i = 0; i < 14; i++) {
    tx_espnow_send_discover();
    delay(35);
  }
  delay(750);
  txesp_scan_armed = false;
  delay(30);
}

struct TxEspScanPeer {
  char mac[18];
  char name[TXESP_APNAME_MAX + 1];
  uint16_t batt_mv;
  uint8_t sig_level;
};

inline int tx_espnow_scan_copy_peers(TxEspScanPeer* out, int max_out) {
  portENTER_CRITICAL(&txesp_mux);
  int n = txesp_found_count;
  if (n > max_out) n = max_out;
  for (int i = 0; i < n; i++) {
    txesp_mac_to_str(txesp_found_mac[i], out[i].mac);
    strcpy(out[i].name, txesp_found_name[i]);
    out[i].batt_mv = txesp_found_batt[i];
    out[i].sig_level = txesp_found_sig[i];
  }
  portEXIT_CRITICAL(&txesp_mux);
  return n;
}

#endif
