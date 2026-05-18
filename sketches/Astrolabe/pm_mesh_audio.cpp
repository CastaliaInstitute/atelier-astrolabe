#include "pm_mesh_audio.h"

#include <string.h>

#include "esp_log.h"
#include "pm_config.h"
#include "pm_mic.h"
#include "pm_speaker.h"
#include "pm_wifi_ntp.h"

#ifndef ASTROLABE_QEMU
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

static const char *TAG = "pm_mesh";

#ifndef MYNAH_MESH_DEVICE_ROLE
#define MYNAH_MESH_DEVICE_ROLE MYNAH_MESH_ROLE_WATCH
#endif

static bool s_ready = false;
static bool s_rx_on = true;
static uint8_t s_channel = MYNAH_MESH_WIFI_CHANNEL;
static uint16_t s_device_id = 0;
static uint16_t s_pkt_seq = 0;
static uint16_t s_frame_seq = 0;

static PmMeshPeer s_peers[PM_MESH_MAX_PEERS];
static int s_peer_count = 0;

static uint8_t s_reasm[MYNAH_MESH_MAX_PCM_BYTES];
static uint16_t s_reasm_frame = 0xFFFF;
static uint8_t s_reasm_mask = 0;
static uint8_t s_reasm_count = 0;
static uint16_t s_reasm_total = 0;

static int s_tx_peer = -1;
static int16_t s_tx_frame[480];
static uint32_t s_last_pcm_rx_ms = 0;

static uint16_t mesh_device_id_from_mac(const uint8_t mac[6]) {
  return static_cast<uint16_t>((mac[4] << 8) | mac[5]);
}

static void mesh_label_from_mac(const uint8_t mac[6], char *out, size_t out_len) {
  snprintf(out, out_len, "%02x%02x", mac[4], mac[5]);
}

static bool mesh_mac_equal(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

static const char *mesh_role_name(uint8_t role) {
  if (role == MYNAH_MESH_ROLE_ATOM) {
    return "atom";
  }
  if (role == MYNAH_MESH_ROLE_WATCH) {
    return "watch";
  }
  return "?";
}

#ifndef ASTROLABE_QEMU

static int mesh_peer_find(const uint8_t mac[6]) {
  for (int i = 0; i < s_peer_count; ++i) {
    if (mesh_mac_equal(s_peers[i].mac, mac)) {
      return i;
    }
  }
  return -1;
}

static void mesh_peer_upsert(const uint8_t mac[6], uint8_t channel, uint8_t role, uint16_t device_id,
                             uint32_t now_ms) {
  int idx = mesh_peer_find(mac);
  if (idx < 0) {
    if (s_peer_count >= PM_MESH_MAX_PEERS) {
      return;
    }
    idx = s_peer_count++;
    memcpy(s_peers[idx].mac, mac, 6);
  }
  s_peers[idx].channel = channel;
  s_peers[idx].role = role;
  s_peers[idx].device_id = device_id;
  s_peers[idx].last_seen_ms = now_ms;
  mesh_label_from_mac(mac, s_peers[idx].label, sizeof(s_peers[idx].label));
}

static uint8_t mesh_current_channel(void) { return MYNAH_MESH_WIFI_CHANNEL; }

static void mesh_apply_channel(void) {
  s_channel = MYNAH_MESH_WIFI_CHANNEL;
  esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
}

static bool mesh_ensure_peer_entry(const uint8_t mac[6], uint8_t channel) {
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  return esp_now_add_peer(&peer) == ESP_OK;
}

static void mesh_on_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGW(TAG, "esp_now send fail %d", static_cast<int>(status));
  }
}

static void mesh_reset_reasm(void) {
  s_reasm_frame = 0xFFFF;
  s_reasm_mask = 0;
  s_reasm_count = 0;
  s_reasm_total = 0;
}

static void mesh_play_frame(const uint8_t *pcm, size_t nbytes) {
  if (nbytes < 2 || (nbytes & 1u) != 0) {
    return;
  }
  const size_t samples = nbytes / 2;
  const int16_t *s16 = reinterpret_cast<const int16_t *>(pcm);
  if (!pm_speaker_stream_active()) {
    if (!pm_speaker_stream_begin(static_cast<int>(MYNAH_MESH_SAMPLE_HZ))) {
      ESP_LOGW(TAG, "speaker stream begin failed");
      return;
    }
  }
  (void)pm_speaker_stream_write(s16, samples);
}

static void mesh_handle_beacon(const uint8_t mac[6], const mynah_mesh_beacon *b, uint32_t now_ms) {
  uint8_t local[6];
  WiFi.macAddress(local);
  if (mesh_mac_equal(mac, local)) {
    return;
  }
  if (mynah_mesh_hdr_version(&b->hdr) != MYNAH_MESH_PROTO_VERSION) {
    return;
  }
  mesh_peer_upsert(mac, b->channel, b->role, b->device_id, now_ms);
}

static void mesh_handle_frag(const uint8_t *data, int len) {
  if (len < static_cast<int>(sizeof(mynah_mesh_pcm_frag))) {
    return;
  }
  const mynah_mesh_pcm_frag *fh = reinterpret_cast<const mynah_mesh_pcm_frag *>(data);
  if (fh->hdr.magic != MYNAH_MESH_MAGIC ||
      mynah_mesh_hdr_version(&fh->hdr) != MYNAH_MESH_PROTO_VERSION) {
    return;
  }
  if (fh->total_bytes == 0 || fh->total_bytes > MYNAH_MESH_MAX_PCM_BYTES || fh->frag_count == 0 ||
      fh->frag_count > 16 || fh->frag_idx >= fh->frag_count) {
    return;
  }
  const int payload_len = len - static_cast<int>(sizeof(mynah_mesh_pcm_frag));
  if (payload_len <= 0) {
    return;
  }
  const size_t off = fh->byte_off;
  if (off + static_cast<size_t>(payload_len) > fh->total_bytes) {
    return;
  }

  if (fh->frame_seq != s_reasm_frame) {
    mesh_reset_reasm();
    s_reasm_frame = fh->frame_seq;
    s_reasm_total = fh->total_bytes;
    s_reasm_count = fh->frag_count;
  } else if (fh->total_bytes != s_reasm_total || fh->frag_count != s_reasm_count) {
    return;
  }

  memcpy(s_reasm + off, data + sizeof(mynah_mesh_pcm_frag), static_cast<size_t>(payload_len));
  s_reasm_mask |= static_cast<uint8_t>(1u << fh->frag_idx);

  const uint8_t need = static_cast<uint8_t>((1u << s_reasm_count) - 1u);
  if ((s_reasm_mask & need) == need) {
    s_last_pcm_rx_ms = millis();
    mesh_play_frame(s_reasm, s_reasm_total);
    mesh_reset_reasm();
  }
}

static void mesh_on_recv(const uint8_t *mac_addr, const uint8_t *data, int len) {
  if (!s_rx_on || !mac_addr || !data || len < static_cast<int>(sizeof(mynah_mesh_hdr))) {
    return;
  }
  const mynah_mesh_hdr *hdr = reinterpret_cast<const mynah_mesh_hdr *>(data);
  if (hdr->magic != MYNAH_MESH_MAGIC) {
    return;
  }
  const uint32_t now_ms = millis();
  if (hdr->type == MYNAH_MESH_PKT_BEACON && len >= static_cast<int>(sizeof(mynah_mesh_beacon))) {
    mesh_handle_beacon(mac_addr, reinterpret_cast<const mynah_mesh_beacon *>(data), now_ms);
    return;
  }
  if (hdr->type == MYNAH_MESH_PKT_PCM_FRAG) {
    mesh_handle_frag(data, len);
  }
}

#endif /** !ASTROLABE_QEMU */

bool pm_mesh_begin(void) {
#ifdef ASTROLABE_QEMU
  return false;
#else
  if (s_ready) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  s_device_id = mesh_device_id_from_mac(mac);
  mesh_apply_channel();

  if (esp_now_init() != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_init failed");
    return false;
  }
  esp_now_register_send_cb(mesh_on_send);
  esp_now_register_recv_cb(mesh_on_recv);

  static const uint8_t bcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  (void)mesh_ensure_peer_entry(bcast, s_channel);

  s_ready = true;
  ESP_LOGI(TAG, "mesh ready role=%s id=%04x ch=%u", mesh_role_name(MYNAH_MESH_DEVICE_ROLE), s_device_id,
           s_channel);
  return true;
#endif
}

void pm_mesh_end(void) {
#ifndef ASTROLABE_QEMU
  if (!s_ready) {
    return;
  }
  pm_mesh_tx_end();
  esp_now_deinit();
  s_ready = false;
#endif
}

bool pm_mesh_ready(void) {
#ifdef ASTROLABE_QEMU
  return false;
#else
  return s_ready;
#endif
}

uint8_t pm_mesh_channel(void) {
#ifndef ASTROLABE_QEMU
  return mesh_current_channel();
#else
  return 0;
#endif
}

void pm_mesh_discovery_tick(uint32_t now_ms) {
#ifndef ASTROLABE_QEMU
  static uint32_t last = 0;
  if (!s_ready || now_ms - last < 1000u) {
    return;
  }
  last = now_ms;
  mesh_apply_channel();

  mynah_mesh_beacon pkt = {};
  mynah_mesh_hdr_init(&pkt.hdr, MYNAH_MESH_PKT_BEACON, ++s_pkt_seq);
  pkt.device_id = s_device_id;
  pkt.role = MYNAH_MESH_DEVICE_ROLE;
  pkt.channel = s_channel;
  pkt.sample_hz = MYNAH_MESH_SAMPLE_HZ;

  static const uint8_t bcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  (void)mesh_ensure_peer_entry(bcast, s_channel);
  (void)esp_now_send(bcast, reinterpret_cast<uint8_t *>(&pkt), sizeof(pkt));
#else
  (void)now_ms;
#endif
}

int pm_mesh_scan(uint32_t timeout_ms) {
#ifndef ASTROLABE_QEMU
  if (!s_ready) {
    return 0;
  }
  const uint32_t deadline = millis() + timeout_ms;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    pm_mesh_discovery_tick(millis());
    delay(50);
  }
  return s_peer_count;
#else
  (void)timeout_ms;
  return 0;
#endif
}

int pm_mesh_peer_count(void) { return s_peer_count; }

int pm_mesh_atom_peer_count(void) {
  int n = 0;
  for (int i = 0; i < s_peer_count; ++i) {
    if (s_peers[i].role == MYNAH_MESH_ROLE_ATOM) {
      ++n;
    }
  }
  return n;
}

bool pm_mesh_peer_get(int index, PmMeshPeer *out) {
  if (!out || index < 0 || index >= s_peer_count) {
    return false;
  }
  *out = s_peers[index];
  return true;
}

bool pm_mesh_peer_register(const uint8_t mac[6]) {
#ifndef ASTROLABE_QEMU
  if (!s_ready || !mac) {
    return false;
  }
  return mesh_ensure_peer_entry(mac, mesh_current_channel());
#else
  (void)mac;
  return false;
#endif
}

bool pm_mesh_send_pcm(const uint8_t peer_mac[6], const int16_t *pcm, size_t sample_count) {
#ifndef ASTROLABE_QEMU
  if (!s_ready || !peer_mac || !pcm || sample_count == 0) {
    return false;
  }
  const size_t nbytes = sample_count * sizeof(int16_t);
  if (nbytes > MYNAH_MESH_MAX_PCM_BYTES) {
    return false;
  }
  if (!mesh_ensure_peer_entry(peer_mac, mesh_current_channel())) {
    return false;
  }

  const uint8_t frag_count =
      static_cast<uint8_t>((nbytes + MYNAH_MESH_FRAG_PAYLOAD_MAX - 1) / MYNAH_MESH_FRAG_PAYLOAD_MAX);
  const uint16_t frame = ++s_frame_seq;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(pcm);
  uint8_t buf[sizeof(mynah_mesh_pcm_frag) + MYNAH_MESH_FRAG_PAYLOAD_MAX];

  for (uint8_t fi = 0; fi < frag_count; ++fi) {
    const size_t off = static_cast<size_t>(fi) * MYNAH_MESH_FRAG_PAYLOAD_MAX;
    const size_t chunk =
        (off + MYNAH_MESH_FRAG_PAYLOAD_MAX > nbytes) ? (nbytes - off) : MYNAH_MESH_FRAG_PAYLOAD_MAX;
    mynah_mesh_pcm_frag *fh = reinterpret_cast<mynah_mesh_pcm_frag *>(buf);
    mynah_mesh_hdr_init(&fh->hdr, MYNAH_MESH_PKT_PCM_FRAG, ++s_pkt_seq);
    fh->frag_idx = fi;
    fh->frag_count = frag_count;
    fh->frame_seq = frame;
    fh->byte_off = static_cast<uint16_t>(off);
    fh->total_bytes = static_cast<uint16_t>(nbytes);
    memcpy(buf + sizeof(mynah_mesh_pcm_frag), bytes + off, chunk);
    if (esp_now_send(peer_mac, buf, sizeof(mynah_mesh_pcm_frag) + chunk) != ESP_OK) {
      return false;
    }
    delay(1);
  }
  return true;
#else
  (void)peer_mac;
  (void)pcm;
  (void)sample_count;
  return false;
#endif
}

void pm_mesh_poll(void) {
#ifndef ASTROLABE_QEMU
  if (pm_speaker_stream_active() && s_last_pcm_rx_ms > 0 && millis() - s_last_pcm_rx_ms > 500u) {
    pm_speaker_stream_end();
    s_last_pcm_rx_ms = 0;
  }
#endif
}

bool pm_mesh_rx_enabled(void) { return s_rx_on; }

void pm_mesh_set_rx_enabled(bool on) { s_rx_on = on; }

bool pm_mesh_tx_begin(int peer_index) {
  if (peer_index < 0 || peer_index >= s_peer_count) {
    return false;
  }
#ifndef ASTROLABE_QEMU
  if (!pm_mic_begin()) {
    return false;
  }
  if (!mesh_ensure_peer_entry(s_peers[peer_index].mac, mesh_current_channel())) {
    return false;
  }
#endif
  s_tx_peer = peer_index;
  return true;
}

void pm_mesh_tx_end(void) {
  s_tx_peer = -1;
  pm_mic_stop();
}

bool pm_mesh_tx_active(void) { return s_tx_peer >= 0; }

void pm_mesh_tx_tick(void) {
#ifndef ASTROLABE_QEMU
  if (s_tx_peer < 0 || s_tx_peer >= s_peer_count) {
    return;
  }
  const size_t frame_samples = pm_mic_frame_samples();
  size_t bytes_read = 0;
  if (!pm_mic_read_frame(s_tx_frame, frame_samples, &bytes_read)) {
    return;
  }
  const size_t samples = bytes_read / sizeof(int16_t);
  (void)pm_mesh_send_pcm(s_peers[s_tx_peer].mac, s_tx_frame, samples);
#endif
}

static bool parse_mac(const char *s, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    if (v[i] > 255) {
      return false;
    }
    out[i] = static_cast<uint8_t>(v[i]);
  }
  return true;
}

bool pm_mesh_serial_command(const char *args) {
  if (!args) {
    return false;
  }
  while (*args == ' ') {
    ++args;
  }
  if (strcmp(args, "status") == 0) {
    Serial.printf("mesh: ready=%d role=%s peers=%d atom=%d ch=%u rx=%d tx=%d stream=%d\n",
                  pm_mesh_ready() ? 1 : 0, mesh_role_name(MYNAH_MESH_DEVICE_ROLE), s_peer_count,
                  pm_mesh_atom_peer_count(), pm_mesh_channel(), s_rx_on ? 1 : 0, s_tx_peer,
                  pm_speaker_stream_active() ? 1 : 0);
    return true;
  }
  if (strcmp(args, "scan") == 0) {
    if (!pm_mesh_ready() && !pm_mesh_begin()) {
      Serial.println("mesh: init failed");
      return true;
    }
    const int n = pm_mesh_scan(3000);
    Serial.printf("mesh: scan found %d peer(s), %d atom\n", n, pm_mesh_atom_peer_count());
    return true;
  }
  if (strcmp(args, "peers") == 0) {
    for (int i = 0; i < s_peer_count; ++i) {
      Serial.printf("mesh: [%d] %02x:%02x:%02x:%02x:%02x:%02x %s id=%04x ch=%u\n", i,
                    s_peers[i].mac[0], s_peers[i].mac[1], s_peers[i].mac[2], s_peers[i].mac[3],
                    s_peers[i].mac[4], s_peers[i].mac[5], mesh_role_name(s_peers[i].role),
                    s_peers[i].device_id, s_peers[i].channel);
    }
    if (s_peer_count == 0) {
      Serial.println("mesh: no peers (try: mesh scan)");
    }
    return true;
  }
  if (strncmp(args, "rx ", 3) == 0) {
    const char *p = args + 3;
    pm_mesh_set_rx_enabled(strcmp(p, "on") == 0 || strcmp(p, "1") == 0);
    Serial.printf("mesh: rx %s\n", pm_mesh_rx_enabled() ? "on" : "off");
    return true;
  }
  if (strncmp(args, "tx ", 3) == 0) {
    const char *p = args + 3;
    if (strcmp(p, "stop") == 0 || strcmp(p, "off") == 0) {
      pm_mesh_tx_end();
      Serial.println("mesh: tx stopped");
      return true;
    }
    int idx = -1;
    if (sscanf(p, "%d", &idx) == 1) {
      if (!pm_mesh_ready() && !pm_mesh_begin()) {
        Serial.println("mesh: init failed");
        return true;
      }
      if (pm_mesh_tx_begin(idx)) {
        Serial.printf("mesh: tx -> peer %d (%s)\n", idx, mesh_role_name(s_peers[idx].role));
      } else {
        Serial.println("mesh: tx begin failed");
      }
      return true;
    }
  }
  if (strncmp(args, "peer ", 5) == 0) {
    uint8_t mac[6];
    if (!parse_mac(args + 5, mac)) {
      Serial.println("mesh: usage: peer aa:bb:cc:dd:ee:ff");
      return true;
    }
    if (!pm_mesh_ready() && !pm_mesh_begin()) {
      Serial.println("mesh: init failed");
      return true;
    }
    if (pm_mesh_peer_register(mac)) {
#ifndef ASTROLABE_QEMU
      mesh_peer_upsert(mac, pm_mesh_channel(), MYNAH_MESH_ROLE_ATOM, mesh_device_id_from_mac(mac),
                       millis());
#endif
      Serial.println("mesh: peer registered");
    } else {
      Serial.println("mesh: peer register failed");
    }
    return true;
  }
  Serial.println("mesh: usage: status | scan | peers | rx on|off | tx <n>|stop | peer MAC");
  return true;
}
