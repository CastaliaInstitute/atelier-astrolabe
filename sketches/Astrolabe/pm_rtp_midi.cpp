#include "pm_rtp_midi.h"

#if defined(ASTROLABE_RTP_MIDI_ENABLED) && !defined(ASTROLABE_QEMU)

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>

#include "pm_log.h"
#include "pm_wifi_ntp.h"

static constexpr uint16_t kControlPort = 5004;
static constexpr uint16_t kDataPort = 5005;
static constexpr uint32_t kProtocolVersion = 2;
static constexpr uint8_t kRtpPayloadTypeMidi = 0x61;

static WiFiUDP s_control_udp;
static WiFiUDP s_data_udp;
static bool s_started = false;
static bool s_data_peer = false;
static IPAddress s_data_ip;
static uint16_t s_data_port = 0;
static uint32_t s_ssrc = 0;
static uint16_t s_seq = 0;
static char s_session_name[40] = "";

static uint16_t read_be16(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

static void write_be16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

static void write_be32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

static void make_session_name(void) {
  snprintf(s_session_name, sizeof(s_session_name), "Astrolabe Ocarina %s", pm_wifi_mac_suffix());
}

static void send_applemidi_ok(WiFiUDP &udp, IPAddress ip, uint16_t port, uint32_t token) {
  uint8_t pkt[64] = {};
  write_be16(pkt + 0, 0xffff);
  pkt[2] = 'O';
  pkt[3] = 'K';
  write_be32(pkt + 4, kProtocolVersion);
  write_be32(pkt + 8, token);
  write_be32(pkt + 12, s_ssrc);
  const size_t name_len = strnlen(s_session_name, sizeof(s_session_name) - 1) + 1;
  memcpy(pkt + 16, s_session_name, name_len);
  udp.beginPacket(ip, port);
  udp.write(pkt, 16 + name_len);
  udp.endPacket();
}

static void send_applemidi_bye(WiFiUDP &udp, IPAddress ip, uint16_t port) {
  uint8_t pkt[16] = {};
  write_be16(pkt + 0, 0xffff);
  pkt[2] = 'B';
  pkt[3] = 'Y';
  write_be32(pkt + 4, kProtocolVersion);
  write_be32(pkt + 8, 0);
  write_be32(pkt + 12, s_ssrc);
  udp.beginPacket(ip, port);
  udp.write(pkt, sizeof(pkt));
  udp.endPacket();
}

static void send_sync_reply(WiFiUDP &udp, IPAddress ip, uint16_t port, const uint8_t *in, int len) {
  if (len < 36) {
    return;
  }
  uint8_t pkt[36] = {};
  memcpy(pkt, in, sizeof(pkt));
  write_be32(pkt + 4, s_ssrc);
  const uint8_t count = pkt[8];
  pkt[8] = count < 2 ? static_cast<uint8_t>(count + 1) : count;
  const uint32_t now = millis();
  if (count == 0) {
    write_be32(pkt + 16, now);
  } else if (count == 1) {
    write_be32(pkt + 24, now);
  }
  udp.beginPacket(ip, port);
  udp.write(pkt, sizeof(pkt));
  udp.endPacket();
}

static void handle_applemidi_packet(WiFiUDP &udp, bool data_socket, const uint8_t *pkt, int len) {
  if (len < 4 || read_be16(pkt) != 0xffff) {
    return;
  }

  IPAddress remote_ip = udp.remoteIP();
  const uint16_t remote_port = udp.remotePort();
  const char cmd0 = static_cast<char>(pkt[2]);
  const char cmd1 = static_cast<char>(pkt[3]);

  if (cmd0 == 'I' && cmd1 == 'N' && len >= 16) {
    const uint32_t token = read_be32(pkt + 8);
    if (data_socket) {
      s_data_ip = remote_ip;
      s_data_port = remote_port;
      s_data_peer = true;
    }
    send_applemidi_ok(udp, remote_ip, remote_port, token);
    pm_log_printf(false, "rtpmidi: %s peer %s:%u", data_socket ? "data" : "control",
                  remote_ip.toString().c_str(), static_cast<unsigned>(remote_port));
    return;
  }

  if (cmd0 == 'C' && cmd1 == 'K') {
    send_sync_reply(udp, remote_ip, remote_port, pkt, len);
    return;
  }

  if (cmd0 == 'B' && cmd1 == 'Y') {
    if (data_socket) {
      s_data_peer = false;
    }
    send_applemidi_bye(udp, remote_ip, remote_port);
  }
}

static void poll_socket(WiFiUDP &udp, bool data_socket) {
  uint8_t pkt[256];
  int len = udp.parsePacket();
  while (len > 0) {
    const int n = udp.read(pkt, sizeof(pkt));
    if (n >= 4 && read_be16(pkt) == 0xffff) {
      handle_applemidi_packet(udp, data_socket, pkt, n);
    }
    len = udp.parsePacket();
  }
}

static bool send_channel_voice(uint8_t status, uint8_t data1, uint8_t data2) {
  if (!s_started || !s_data_peer || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  uint8_t pkt[16] = {};
  pkt[0] = 0x80;
  pkt[1] = kRtpPayloadTypeMidi;
  write_be16(pkt + 2, ++s_seq);
  write_be32(pkt + 4, millis());
  write_be32(pkt + 8, s_ssrc);
  pkt[12] = 0x03;
  pkt[13] = status;
  pkt[14] = data1;
  pkt[15] = data2;

  s_data_udp.beginPacket(s_data_ip, s_data_port);
  s_data_udp.write(pkt, sizeof(pkt));
  return s_data_udp.endPacket() == 1;
}

bool pm_rtp_midi_begin(void) {
  if (s_started) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  s_ssrc = esp_random();
  if (s_ssrc == 0) {
    s_ssrc = 0x41535452u;
  }
  s_seq = static_cast<uint16_t>(esp_random());
  make_session_name();

  if (!s_control_udp.begin(kControlPort) || !s_data_udp.begin(kDataPort)) {
    pm_log_printf(false, "rtpmidi: udp begin failed");
    return false;
  }
  MDNS.addService("apple-midi", "udp", kControlPort);
  MDNS.addServiceTxt("apple-midi", "udp", "name", static_cast<const char *>(s_session_name));
  MDNS.addServiceTxt("apple-midi", "udp", "model", "Astrolabe Ocarina");
  s_started = true;
  pm_log_printf(false, "rtpmidi: %s udp %u/%u", s_session_name, kControlPort, kDataPort);
  return true;
}

void pm_rtp_midi_tick(void) {
  if (!s_started || WiFi.status() != WL_CONNECTED) {
    return;
  }
  poll_socket(s_control_udp, false);
  poll_socket(s_data_udp, true);
}

bool pm_rtp_midi_enabled(void) { return true; }
bool pm_rtp_midi_note_on(uint8_t note, uint8_t velocity) { return send_channel_voice(0x90, note, velocity); }
bool pm_rtp_midi_note_off(uint8_t note) { return send_channel_voice(0x80, note, 0); }

#else

bool pm_rtp_midi_begin(void) { return false; }
void pm_rtp_midi_tick(void) {}
bool pm_rtp_midi_enabled(void) { return false; }
bool pm_rtp_midi_note_on(uint8_t note, uint8_t velocity) {
  (void)note;
  (void)velocity;
  return false;
}
bool pm_rtp_midi_note_off(uint8_t note) {
  (void)note;
  return false;
}

#endif
