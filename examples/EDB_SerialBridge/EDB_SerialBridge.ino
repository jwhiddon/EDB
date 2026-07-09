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

static const char DB_PATH[] = "/edb_bridge.db";
File dbFile;

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

#if EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO
static bool session_active = false;
static char session_token[33] = {0};
#endif

static char line_buf[EDB_BRIDGE_MAX_LINE];
static size_t line_len = 0;

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

static void replyOk(long id, const char *extra) {
  if (extra && extra[0])
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{%s}}\n", id, extra);
  else
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\"}\n", id);
}

static void replyErr(long id, EDB_Status st) {
  Serial.printf("{\"id\":%ld,\"status\":\"%s\"}\n", id, statusStr(st));
}

static void handleCommand(const char *json) {
  long id = jsonLong(json, "id");
  char cmd[24] = {0};
  if (!jsonStr(json, "cmd", cmd, sizeof(cmd))) {
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id >= 0 ? id : 0);
    return;
  }

  if (strcmp(cmd, "ping") == 0) {
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"version\":\"%s\"}}\n", id, EDB_BRIDGE_VERSION);
    return;
  }

#if EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO
  if (strcmp(cmd, "pair") == 0) {
    char token[64] = {0};
    jsonStr(json, "token", token, sizeof(token));
    if (strlen(token) >= 8) {
      strncpy(session_token, token, sizeof(session_token) - 1);
      session_active = true;
      Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"paired\":true}}\n", id);
    } else {
      Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
    }
    return;
  }
#endif

  if (strcmp(cmd, "info") == 0) {
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"tables\":[", id);
    for (size_t i = 0; i < NUM_TABLES; i++) {
      if (i) Serial.print(',');
      Serial.printf("{\"head_ptr\":%lu,\"table_size\":%lu,\"rec_size\":%u,\"label\":\"%s\"}",
        tables[i].head_ptr, tables[i].table_size, tables[i].rec_size, tables[i].label);
    }
    Serial.println("]}}");
    return;
  }

  if (!sd_ok) {
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
    return;
  }

  long head_ptr = jsonLong(json, "head_ptr");
  if (head_ptr < 0) head_ptr = (long)active_head;
  TableConfig *tc = findTable((unsigned long)head_ptr);
  if (!tc) {
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
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
    active_head = tc->head_ptr;
    EDB_Status st = db.create(tc->head_ptr, (unsigned long)ts, (unsigned int)rs);
    replyErr(id, st);
    return;
  }

  if (strcmp(cmd, "count") == 0) {
    db.open(tc->head_ptr);
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"count\":%lu}}\n", id, db.count());
    return;
  }

  if (strcmp(cmd, "limit") == 0) {
    db.open(tc->head_ptr);
    Serial.printf("{\"id\":%ld,\"status\":\"EDB_OK\",\"data\":{\"limit\":%lu}}\n", id, db.limit());
    return;
  }

  if (strcmp(cmd, "clear") == 0) {
    db.open(tc->head_ptr);
    replyErr(id, db.clear());
    return;
  }

  byte rec_buf[128];
  char payload_b64[256];
  char extra[320];

  if (strcmp(cmd, "readRec") == 0) {
    long recno = jsonLong(json, "recno");
    unsigned int rs = tc->rec_size;
    db.open(tc->head_ptr);
    EDB_Status st = db.readRec((unsigned long)recno, rec_buf);
    if (st != EDB_OK) { replyErr(id, st); return; }
    char b64[200];
    b64encode(rec_buf, rs, b64, sizeof(b64));
    snprintf(extra, sizeof(extra), "\"recno\":%ld,\"payload_b64\":\"%s\",\"enc_version\":0", recno, b64);
    replyOk(id, extra);
    return;
  }

  if (strcmp(cmd, "appendRec") == 0 || strcmp(cmd, "updateRec") == 0 || strcmp(cmd, "insertRec") == 0) {
    if (!jsonStr(json, "payload_b64", payload_b64, sizeof(payload_b64))) {
      Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
      return;
    }
    size_t n = b64decode(payload_b64, rec_buf, sizeof(rec_buf));
    if (n == 0 || n > tc->rec_size) {
      Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
      return;
    }
    while (n < tc->rec_size) rec_buf[n++] = 0;
    db.open(tc->head_ptr);
    EDB_Status st = EDB_ERROR;
    if (strcmp(cmd, "appendRec") == 0) st = db.appendRec(rec_buf);
    else if (strcmp(cmd, "updateRec") == 0) {
      long recno = jsonLong(json, "recno");
      st = db.updateRec((unsigned long)recno, rec_buf);
    } else {
      long recno = jsonLong(json, "recno");
      st = db.insertRec((unsigned long)recno, rec_buf);
    }
    replyErr(id, st);
    return;
  }

  if (strcmp(cmd, "deleteRec") == 0) {
    long recno = jsonLong(json, "recno");
    db.open(tc->head_ptr);
    replyErr(id, db.deleteRec((unsigned long)recno));
    return;
  }

  Serial.printf("{\"id\":%ld,\"status\":\"EDB_ERROR\"}\n", id);
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
