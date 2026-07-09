/*
  EDB_Encrypted — at-rest record encryption (EDB v3)

  The core EDB library stores records as opaque bytes; encryption is a caller concern. Here the
  sketch seals each record with ChaCha20-Poly1305 (EDB_Crypto) before storing it, so plaintext
  never touches storage, and opens it on read.

  Build with the crypto library enabled:
      arduino-cli compile --library <path> --fqbn <board> \
        --build-property "compiler.cpp.extra_flags=-DEDB_ENABLE_CRYPTO" examples/EDB_Encrypted

  Uses a RAM array as storage so it runs on any board. The key here is a hardcoded demo value —
  in production load it from secure storage (e.g. ESP32 NVS), never from source.
*/
#include "Arduino.h"
#include <EDB.h>
#include <EDB_Crypto.h>

#define PLAINTEXT_SIZE 4
// A sealed record is nonce(12) || ciphertext || tag(16), so the stored record is larger.
#define STORED_SIZE (PLAINTEXT_SIZE + EDB_CRYPTO_RECORD_OVERHEAD)
#define TABLE_SIZE 512

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

#if defined(EDB_ENABLE_CRYPTO)
static const uint8_t KEY[EDB_CRYPTO_KEY_SIZE] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};
static uint32_t nonce_counter = 0;

// Each write needs a unique nonce. A random nonce is best (esp_random on ESP32); a counter is used
// here for a deterministic demo. The nonce is stored with the record, so open() can recover it.
static void make_nonce(uint8_t nonce[EDB_CRYPTO_NONCE_SIZE]) {
  memset(nonce, 0, EDB_CRYPTO_NONCE_SIZE);
  memcpy(nonce, &nonce_counter, sizeof(nonce_counter));
  nonce_counter++;
}
#endif

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

#if !defined(EDB_ENABLE_CRYPTO)
  Serial.println("Compile with -DEDB_ENABLE_CRYPTO to run the encryption demo.");
  return;
#else
  Serial.println("EDB at-rest encryption demo (ChaCha20-Poly1305)\n");
  if (db.create(0, TABLE_SIZE, STORED_SIZE) != EDB_OK) {
    Serial.println("create failed");
    return;
  }

  const int32_t secret = 12345;

  // Seal the plaintext, then store only the ciphertext.
  uint8_t nonce[EDB_CRYPTO_NONCE_SIZE];
  make_nonce(nonce);
  uint8_t sealed[STORED_SIZE];
  size_t sealed_len = 0;
  edb_crypto_seal_record(KEY, 0 /*table_id*/, 0 /*record_id*/, nonce,
                         (const uint8_t *)&secret, PLAINTEXT_SIZE, sealed, &sealed_len);
  unsigned long recno = 0;
  db.appendRec(EDB_REC sealed[0], &recno);
  Serial.print("stored secret ");
  Serial.print(secret);
  Serial.print(" as ");
  Serial.print((unsigned)sealed_len);
  Serial.println(" ciphertext bytes");

  // Show the raw storage really is ciphertext (record data begins after the 96-byte v3 header).
  Serial.print("raw record bytes: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(storage[EDB_HEADER_SPAN + 1 + i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // Read the ciphertext back and open it.
  uint8_t ciphertext[STORED_SIZE];
  db.readRec(recno, EDB_REC ciphertext[0]);
  int32_t recovered = 0;
  size_t plain_len = 0;
  int rc = edb_crypto_open_record(KEY, 0, 0, ciphertext, STORED_SIZE,
                                  (uint8_t *)&recovered, &plain_len);
  Serial.print("open_record rc=");
  Serial.print(rc);
  Serial.print("  recovered=");
  Serial.println(recovered);

  // Tamper detection: flipping a stored byte makes authentication fail.
  ciphertext[EDB_CRYPTO_NONCE_SIZE] ^= 0x01;
  rc = edb_crypto_open_record(KEY, 0, 0, ciphertext, STORED_SIZE,
                              (uint8_t *)&recovered, &plain_len);
  Serial.print("after tampering, open_record rc=");
  Serial.print(rc);
  Serial.println("  (-2 == authentication failed)");
#endif
}

void loop() {}
