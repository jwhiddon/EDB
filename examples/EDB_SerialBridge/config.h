#ifndef EDB_BRIDGE_CONFIG_H
#define EDB_BRIDGE_CONFIG_H

#include <stdint.h>

#ifndef EDB_BRIDGE_MAX_LINE
#define EDB_BRIDGE_MAX_LINE 512
#endif

#ifndef EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO
#if defined(ESP32)
#define EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO 1
#else
#define EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO 0
#endif
#endif

#ifndef EDB_BRIDGE_ENABLE_BASE64
#define EDB_BRIDGE_ENABLE_BASE64 1
#endif

#define EDB_BRIDGE_VERSION "2.0.0"
#define SD_PIN 5
#define TABLE_SIZE 8192
#define DEFAULT_REC_SIZE 8

// Transport pre-shared key (32 bytes), only used when transport crypto is compiled in
// (EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO and the -DEDB_ENABLE_CRYPTO build flag).
// CHANGE THIS before deploying: it must match the gateway's EDB_GATEWAY_PSK and is NOT a secret
// as shipped.
#if EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO && defined(EDB_ENABLE_CRYPTO)
static const uint8_t EDB_BRIDGE_PSK[32] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};
#endif

// Autonomous at-rest encryption: when enabled, the device seals records on write and opens them on
// read with EDB_BRIDGE_AT_REST_KEY, so plaintext never touches storage. Tables must be created with
// rec_size = plaintext_size + 28. Opt-in and requires the -DEDB_ENABLE_CRYPTO build flag.
#ifndef EDB_BRIDGE_ENABLE_AT_REST_CRYPTO
#define EDB_BRIDGE_ENABLE_AT_REST_CRYPTO 0
#endif

#if EDB_BRIDGE_ENABLE_AT_REST_CRYPTO && defined(EDB_ENABLE_CRYPTO)
// CHANGE THIS: 32-byte device key. In production, load it from secure storage (ESP32 NVS), not a
// compile-time constant.
static const uint8_t EDB_BRIDGE_AT_REST_KEY[32] = {
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf
};
#endif

#endif
