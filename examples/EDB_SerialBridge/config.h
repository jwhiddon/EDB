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

#endif
