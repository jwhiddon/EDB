/*
  EDB_SerialBridge — NDJSON serial protocol for EDB Gateway
  ESP32 + SD (Tier 1). See docs/GATEWAY.md
*/
#include "Arduino.h"
#include <EDB.h>
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <stdarg.h>

// Transport session crypto is active only when both the bridge flag AND EDB_ENABLE_CRYPTO are set
// (EDB_ENABLE_CRYPTO must be a global build flag so EDB_Crypto.cpp is compiled). Without it the
// bridge runs in plaintext.
#if EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO && defined(EDB_ENABLE_CRYPTO)
#define EDB_BRIDGE_CRYPTO 1
#include "bridge_session.h"
#else
#define EDB_BRIDGE_CRYPTO 0
#endif

#if EDB_BRIDGE_ENABLE_AT_REST_CRYPTO && defined(EDB_ENABLE_CRYPTO)
#define EDB_BRIDGE_AT_REST 1
#include <EDB_Crypto.h>
#else
#define EDB_BRIDGE_AT_REST 0
#endif

static const char DB_PATH[] = "/edb_bridge.db";
File dbFile;

// Largest stored record this sketch will read into a stack buffer. Tables whose on-disk
// record size exceeds this are refused rather than risk a buffer overflow.
#define REC_BUF_SIZE 128

struct TableConfig {
  unsigned long head_ptr;
  unsigned long table_size;
  unsigned int rec_size;
  const char *label;
};

static TableConfig tables[] = {
  {0, TABLE_SIZE, DEFAULT_REC_SIZE, "main"},
};
static const size_t NUM_TABLES = sizeof(tables) / sizeof(tables[0]);

static unsigned long active_head = 0;
static bool sd_ok = false;

#if EDB_BRIDGE_CRYPTO
static bool session_active = false;
static uint8_t session_key[32];
static uint32_t tx_counter = 0;   // device -> host
static int64_t rx_counter = -1;   // last accepted host -> device counter
#endif

static char line_buf[EDB_BRIDGE_MAX_LINE];
static size_t line_len = 0;

#if EDB_BRIDGE_CRYPTO || EDB_BRIDGE_AT_REST
static void bridgeFillRandom(uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i += 4) {
#if defined(ESP32)
    uint32_t r = esp_random();
#else
    uint32_t r = ((uint32_t)random(65536) << 16) ^ (uint32_t)micros();
#endif
    size_t take = (n - i) >= 4 ? 4 : (n - i);
    memcpy(p + i, &r, take);
  }
}
#endif

void writer(unsigned long address, const byte *data, unsigned int recsize) {
  dbFile.seek(address, SeekSet);
  dbFile.write(data, recsize);
}

void reader(unsigned long address, byte *data, unsigned int recsize) {
  dbFile.seek(address, SeekSet);
  dbFile.read(data, recsize);
}

static EDB db(writer, reader);

static const char *statusStr(EDB_Status s) {
  switch (s) {
    case EDB_OK: return "EDB_OK";
    case EDB_ERROR: return "EDB_ERROR";
    case EDB_OUT_OF_RANGE: return "EDB_OUT_OF_RANGE";
    case EDB_TABLE_FULL: return "EDB_TABLE_FULL";
    case EDB_DELETED: return "EDB_DELETED";
    case EDB_CORRUPT: return "EDB_CORRUPT";
    case EDB_NEEDS_MIGRATION: return "EDB_NEEDS_MIGRATION";
    default: return "EDB_ERROR";
  }
}

static int b64idx(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static size_t b64decode(const char *in, byte *out, size_t max_out) {
  size_t o = 0;
  int val = 0, valb = -8;
  for (; *in; in++) {
    if (*in == '=') break;
    int d = b64idx(*in);
    if (d < 0) continue;
    val = (val << 6) + d;
    valb += 6;
    if (valb >= 0) {
      if (o < max_out) out[o++] = (byte)((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return o;
}

static void b64encode(const byte *in, size_t len, char *out, size_t out_max) {
  static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)in[i]) << 16;
    if (i + 1 < len) n |= ((uint32_t)in[i + 1]) << 8;
    if (i + 2 < len) n |= in[i + 2];
    if (o + 4 >= out_max) break;
    out[o++] = tbl[(n >> 18) & 63];
    out[o++] = tbl[(n >> 12) & 63];
    out[o++] = (i + 1 < len) ? tbl[(n >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? tbl[n & 63] : '=';
  }
  out[o] = 0;
}

static long jsonLong(const char *json, const char *key) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) return -1;
  p += strlen(pat);
  while (*p == ' ') p++;
  return strtol(p, nullptr, 10);
}

static bool jsonStr(const char *json, const char *key, char *out, size_t out_len) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < out_len) out[i++] = *p++;
  out[i] = 0;
  return true;
}

static TableConfig *findTable(unsigned long head_ptr) {
  for (size_t i = 0; i < NUM_TABLES; i++)
    if (tables[i].head_ptr == head_ptr) return &tables[i];
  return nullptr;
}

static char g_out[EDB_BRIDGE_MAX_LINE];

// Emit one response line: ChaCha20-Poly1305 framed when a session is active, else plaintext.
static void emitLine(const char *body) {
#if EDB_BRIDGE_CRYPTO
  if (session_active) {
    size_t blen = strlen(body);
    if (blen > EDB_BRIDGE_MAX_LINE) blen = EDB_BRIDGE_MAX_LINE;
    static uint8_t frame[12 + EDB_BRIDGE_MAX_LINE + EDB_CRYPTO_TAG_SIZE];
    size_t flen = bridge_session_seal(session_key, EDB_BRIDGE_DIR_DEVICE, tx_counter++,
                                      (const uint8_t *)body, blen, frame);
    static char b64[((12 + EDB_BRIDGE_MAX_LINE + EDB_CRYPTO_TAG_SIZE) * 4) / 3 + 8];
    b64encode(frame, flen, b64, sizeof(b64));
    Serial.print("{\"enc\":\"");
    Serial.print(b64);
    Serial.println("\"}");
    return;
  }
#endif
  Serial.println(body);
}

static void replyFmt(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_out, sizeof(g_out), fmt, ap);
  va_end(ap);
  emitLine(g_out);
}

static void replyOk(long id, const char *extra) {
  if (extra && extra[0])
    snprintf(g_out, sizeof(g_out), "{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{%s}}", id, extra);
  else
    snprintf(g_out, sizeof(g_out), "{\"id\":%ld,\"status\":\"EDB_OK\"}", id);
  emitLine(g_out);
}

static void replyErr(long id, EDB_Status st) {
  snprintf(g_out, sizeof(g_out), "{\"id\":%ld,\"status\":\"%s\"}", id, statusStr(st));
  emitLine(g_out);
}

static void handleCommand(const char *raw) {
  const char *json = raw;

#if EDB_BRIDGE_CRYPTO
  static char decbuf[EDB_BRIDGE_MAX_LINE];
  if (session_active) {
    // Once paired, every command must arrive as an authenticated {"enc":"..."} frame.
    static char enc_b64[EDB_BRIDGE_MAX_LINE];
    if (!jsonStr(raw, "enc", enc_b64, sizeof(enc_b64))) {
      replyFmt("{\"id\":0,\"status\":\"EDB_ERROR\",\"data\":{\"error\":\"encrypted_required\"}}");
      return;
    }
    static uint8_t frame[EDB_BRIDGE_MAX_LINE];
    size_t flen = b64decode(enc_b64, frame, sizeof(frame));
    uint32_t counter = 0;
    int n = bridge_session_open(session_key, EDB_BRIDGE_DIR_HOST, frame, flen, (uint8_t *)decbuf, &counter);
    if (n < 0 || (int64_t)counter <= rx_counter) {   // auth failure or replay
      replyFmt("{\"id\":0,\"status\":\"EDB_ERROR\",\"data\":{\"error\":\"decrypt\"}}");
      return;
    }
    rx_counter = (int64_t)counter;
    decbuf[n] = 0;
    json = decbuf;
  }
#endif

  long id = jsonLong(json, "id");
  char cmd[24] = {0};
  if (!jsonStr(json, "cmd", cmd, sizeof(cmd))) {
    replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id >= 0 ? id : 0);
    return;
  }

  if (strcmp(cmd, "ping") == 0) {
    replyFmt("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"version\":\"%s\"}}", id, EDB_BRIDGE_VERSION);
    return;
  }

#if EDB_BRIDGE_CRYPTO
  if (strcmp(cmd, "pair") == 0) {
    char hnonce_b64[24] = {0};
    uint8_t host_nonce[EDB_CRYPTO_NONCE_SIZE];
    if (!jsonStr(json, "hnonce", hnonce_b64, sizeof(hnonce_b64)) ||
        b64decode(hnonce_b64, host_nonce, sizeof(host_nonce)) != EDB_CRYPTO_NONCE_SIZE) {
      replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
      return;
    }
    uint8_t dev_nonce[EDB_CRYPTO_NONCE_SIZE];
    bridgeFillRandom(dev_nonce, sizeof(dev_nonce));
    edb_crypto_session_key(EDB_BRIDGE_PSK, host_nonce, dev_nonce, session_key);
    uint8_t confirm[EDB_CRYPTO_TAG_SIZE];
    edb_crypto_session_confirm(session_key, confirm);
    char dn_b64[20], cf_b64[28];
    b64encode(dev_nonce, EDB_CRYPTO_NONCE_SIZE, dn_b64, sizeof(dn_b64));
    b64encode(confirm, EDB_CRYPTO_TAG_SIZE, cf_b64, sizeof(cf_b64));
    tx_counter = 0;
    rx_counter = -1;
    // The pairing reply is cleartext (the host has not confirmed the key yet); activate after.
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"dnonce\":\"%s\",\"confirm\":\"%s\"}}\n",
                  id, dn_b64, cf_b64);
    session_active = true;
    return;
  }
#endif

  if (strcmp(cmd, "info") == 0) {
    int off = snprintf(g_out, sizeof(g_out), "{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"tables\":[", id);
    for (size_t i = 0; i < NUM_TABLES && off > 0 && (size_t)off < sizeof(g_out); i++) {
      off += snprintf(g_out + off, sizeof(g_out) - off,
                      "%s{\"head_ptr\":%lu,\"table_size\":%lu,\"rec_size\":%u,\"label\":\"%s\"}",
                      i ? "," : "", tables[i].head_ptr, tables[i].table_size, tables[i].rec_size, tables[i].label);
    }
    if (off > 0 && (size_t)off < sizeof(g_out))
      snprintf(g_out + off, sizeof(g_out) - off, "]}}");
    emitLine(g_out);
    return;
  }

#if EDB_BRIDGE_CRYPTO
  // With crypto built in, database commands require an active session.
  if (!session_active) {
    replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\",\"data\":{\"error\":\"not_paired\"}}", id);
    return;
  }
#endif

  if (!sd_ok) {
    replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
    return;
  }

  long head_ptr = jsonLong(json, "head_ptr");
  if (head_ptr < 0) head_ptr = (long)active_head;
  TableConfig *tc = findTable((unsigned long)head_ptr);
  if (!tc) {
    replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
    return;
  }

  if (strcmp(cmd, "open") == 0) {
    active_head = tc->head_ptr;
    EDB_Status st = db.open(tc->head_ptr);
    replyErr(id, st);
    return;
  }

  if (strcmp(cmd, "create") == 0) {
    long ts = jsonLong(json, "table_size");
    long rs = jsonLong(json, "rec_size");
    if (ts < 0) ts = (long)tc->table_size;
    if (rs < 0) rs = (long)tc->rec_size;
    // Refuse record sizes that would overflow rec_buf on a later readRec/appendRec.
    if (rs <= 0 || rs > (long)REC_BUF_SIZE) { replyErr(id, EDB_ERROR); return; }
    active_head = tc->head_ptr;
    EDB_Status st = db.create(tc->head_ptr, (unsigned long)ts, (unsigned int)rs);
    if (st == EDB_OK) { tc->rec_size = (unsigned int)rs; dbFile.flush(); }
    replyErr(id, st);
    return;
  }

  if (strcmp(cmd, "count") == 0) {
    if (db.open(tc->head_ptr) != EDB_OK) { replyErr(id, EDB_ERROR); return; }
    replyFmt("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"count\":%lu}}", id, db.count());
    return;
  }

  if (strcmp(cmd, "limit") == 0) {
    if (db.open(tc->head_ptr) != EDB_OK) { replyErr(id, EDB_ERROR); return; }
    replyFmt("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"limit\":%lu}}", id, db.limit());
    return;
  }

  if (strcmp(cmd, "clear") == 0) {
    if (db.open(tc->head_ptr) != EDB_OK) { replyErr(id, EDB_ERROR); return; }
    EDB_Status st = db.clear();
    if (st == EDB_OK) dbFile.flush();
    replyErr(id, st);
    return;
  }

  byte rec_buf[REC_BUF_SIZE];
  char payload_b64[256];
  char extra[320];

  // The on-disk record size is authoritative; refuse anything that does not fit rec_buf.
  if (db.open(tc->head_ptr) != EDB_OK) { replyErr(id, EDB_ERROR); return; }
  unsigned int rs = db.recSize();
  if (rs == 0 || rs > sizeof(rec_buf)) { replyErr(id, EDB_ERROR); return; }
  tc->rec_size = rs;

  if (strcmp(cmd, "readRec") == 0) {
    long recno = jsonLong(json, "recno");
    if (recno < 1) { replyErr(id, EDB_OUT_OF_RANGE); return; }
    EDB_Status st = db.readRec((unsigned long)recno, rec_buf);
    if (st != EDB_OK) { replyErr(id, st); return; }
    const byte *payload_ptr = rec_buf;
    unsigned int payload_len = rs;
#if EDB_BRIDGE_AT_REST
    static byte plain_buf[REC_BUF_SIZE];
    size_t plain_len = 0;
    if (edb_crypto_open_record(EDB_BRIDGE_AT_REST_KEY, (uint32_t)tc->head_ptr, 0,
                               rec_buf, rs, plain_buf, &plain_len) != 0) {
      replyErr(id, EDB_CORRUPT);
      return;
    }
    payload_ptr = plain_buf;
    payload_len = (unsigned int)plain_len;
#endif
    char b64[4 * ((REC_BUF_SIZE + 2) / 3) + 1];
    b64encode(payload_ptr, payload_len, b64, sizeof(b64));
    snprintf(extra, sizeof(extra), "\"recno\":%ld,\"payload_b64\":\"%s\",\"enc_version\":%d",
             recno, b64, EDB_BRIDGE_AT_REST);
    replyOk(id, extra);
    return;
  }

  if (strcmp(cmd, "appendRec") == 0 || strcmp(cmd, "updateRec") == 0 || strcmp(cmd, "insertRec") == 0) {
    if (!jsonStr(json, "payload_b64", payload_b64, sizeof(payload_b64))) {
      replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
      return;
    }
    size_t n = b64decode(payload_b64, rec_buf, sizeof(rec_buf));
    byte *write_ptr = rec_buf;
#if EDB_BRIDGE_AT_REST
    // The client payload is plaintext; the device seals it (fresh random nonce) before storage.
    if (rs <= EDB_CRYPTO_RECORD_OVERHEAD) { replyErr(id, EDB_ERROR); return; }
    unsigned int plain_max = rs - EDB_CRYPTO_RECORD_OVERHEAD;
    if (n == 0 || n > plain_max) {
      replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
      return;
    }
    while (n < plain_max) rec_buf[n++] = 0;
    static byte sealed_buf[REC_BUF_SIZE];
    uint8_t at_rest_nonce[EDB_CRYPTO_NONCE_SIZE];
    bridgeFillRandom(at_rest_nonce, sizeof(at_rest_nonce));
    size_t sealed_len = 0;
    edb_crypto_seal_record(EDB_BRIDGE_AT_REST_KEY, (uint32_t)tc->head_ptr, 0,
                           at_rest_nonce, rec_buf, plain_max, sealed_buf, &sealed_len);
    write_ptr = sealed_buf;
#else
    if (n == 0 || n > rs) {
      replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
      return;
    }
    while (n < rs) rec_buf[n++] = 0;
#endif
    EDB_Status st = EDB_ERROR;
    if (strcmp(cmd, "appendRec") == 0) {
      st = db.appendRec(write_ptr);
    } else {
      long recno = jsonLong(json, "recno");
      if (recno < 1) { replyErr(id, EDB_OUT_OF_RANGE); return; }
      if (strcmp(cmd, "updateRec") == 0) st = db.updateRec((unsigned long)recno, write_ptr);
      else st = db.insertRec((unsigned long)recno, write_ptr);
    }
    if (st == EDB_OK) dbFile.flush();
    replyErr(id, st);
    return;
  }

  if (strcmp(cmd, "deleteRec") == 0) {
    long recno = jsonLong(json, "recno");
    if (recno < 1) { replyErr(id, EDB_OUT_OF_RANGE); return; }
    EDB_Status st = db.deleteRec((unsigned long)recno);
    if (st == EDB_OK) dbFile.flush();
    replyErr(id, st);
    return;
  }

  replyFmt("{\"id\":%ld,\"status\":\"EDB_ERROR\"}", id);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  if (!SD.begin(SD_PIN)) {
    sd_ok = false;
    return;
  }
  if (!SD.exists(DB_PATH)) {
    dbFile = SD.open(DB_PATH, "w+");
  } else {
    dbFile = SD.open(DB_PATH, "r+");
  }
  sd_ok = (bool)dbFile;
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line_len > 0) {
        line_buf[line_len] = 0;
        handleCommand(line_buf);
        line_len = 0;
      }
    } else if (line_len + 1 < EDB_BRIDGE_MAX_LINE) {
      line_buf[line_len++] = c;
    }
  }
}
