/**
 * Mynah ESP-NOW mesh audio wire format — shared by astrolabe (watch) and atom repos.
 * Copy or submodule this header; keep magic/version in sync across firmware.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYNAH_MESH_MAGIC 0x4D4Eu /** 'NM' little-endian */
#define MYNAH_MESH_PROTO_VERSION 1u

#define MYNAH_MESH_SAMPLE_HZ 16000u
#define MYNAH_MESH_FRAME_MS 30u
#define MYNAH_MESH_MAX_PCM_BYTES 960u /** 30 ms mono int16 @ 16 kHz */
#define MYNAH_MESH_FRAG_PAYLOAD_MAX 230u

typedef enum {
  MYNAH_MESH_ROLE_WATCH = 1,
  MYNAH_MESH_ROLE_ATOM = 2,
} mynah_mesh_role_t;

typedef enum {
  MYNAH_MESH_PKT_BEACON = 1,
  MYNAH_MESH_PKT_PCM_FRAG = 2,
} mynah_mesh_pkt_type_t;

struct __attribute__((packed)) mynah_mesh_hdr {
  uint16_t magic;
  uint8_t type;
  uint8_t ver_flags; /** low nibble = MYNAH_MESH_PROTO_VERSION */
  uint16_t seq;
};

struct __attribute__((packed)) mynah_mesh_beacon {
  mynah_mesh_hdr hdr;
  uint16_t device_id;
  uint8_t role;
  uint8_t channel;
  uint16_t sample_hz;
};

struct __attribute__((packed)) mynah_mesh_pcm_frag {
  mynah_mesh_hdr hdr;
  uint8_t frag_idx;
  uint8_t frag_count;
  uint16_t frame_seq;
  uint16_t byte_off;
  uint16_t total_bytes;
};

static inline uint8_t mynah_mesh_hdr_version(const struct mynah_mesh_hdr *h) {
  return h ? (h->ver_flags & 0x0Fu) : 0u;
}

static inline void mynah_mesh_hdr_init(struct mynah_mesh_hdr *h, uint8_t type, uint16_t seq) {
  h->magic = MYNAH_MESH_MAGIC;
  h->type = type;
  h->ver_flags = MYNAH_MESH_PROTO_VERSION & 0x0Fu;
  h->seq = seq;
}

#ifdef __cplusplus
}
#endif
