#include "unity.h"
#include "CryptoTestHelpers.h"
#include "FakeStorage.h"
#include "TestHelpers.h"
#include "Arduino.h"
#include "EDB.h"
#include <cstring>

// Needs both the crypto reference impl and the v3 core API (appendRec out-param, EDB_DELETED).
#if defined(EDB_ENABLE_CRYPTO) && defined(EDB_VERSION)

static FakeStorage g_store;
static void wbuf(unsigned long a, const byte *p, unsigned int n) { g_store.writeBufferInstance(a, p, n); }
static void rbuf(unsigned long a, byte *p, unsigned int n) { g_store.readBufferInstance(a, p, n); }

// v3: the core stores sealed records as opaque bytes. Slot IDs are stable and records are never
// shifted, so a delete never disturbs another record's ciphertext or its (table_id, record_id)
// binding. The caller seals with a fresh per-record nonce.
void test_encrypted_records_opaque_roundtrip() {
  uint8_t key[EDB_CRYPTO_KEY_SIZE];
  cryptoTestKey(key);
  const unsigned int plain_size = 4;
  const unsigned int stored_size = plain_size + EDB_CRYPTO_RECORD_OVERHEAD;

  g_store.reset();
  EDB db(wbuf, rbuf);
  TEST_ASSERT_EQUAL(EDB_OK, db.create(0, 1024, stored_size));

  for (unsigned int i = 1; i <= 3; i++) {
    uint8_t plain[4] = {(uint8_t)i, 0, 0, 0};
    uint8_t nonce[EDB_CRYPTO_NONCE_SIZE] = {0};
    nonce[0] = (uint8_t)i;                 // unique per record (test uses a counter)
    uint8_t blob[64];
    size_t blob_len = 0;
    TEST_ASSERT_EQUAL_INT(0, edb_crypto_seal_record(key, 0, i, nonce, plain, plain_size, blob, &blob_len));
    TEST_ASSERT_EQUAL_UINT32(stored_size, (unsigned)blob_len);
    unsigned long id = 0;
    TEST_ASSERT_EQUAL(EDB_OK, db.appendRec(blob, &id));
    TEST_ASSERT_EQUAL_UINT32(i, id);
  }

  TEST_ASSERT_EQUAL(EDB_OK, db.deleteRec(2));
  TEST_ASSERT_EQUAL_UINT32(2, db.count());

  uint8_t blob[64];
  uint8_t plain[4];
  size_t out_len = 0;
  TEST_ASSERT_EQUAL(EDB_OK, db.readRec(1, blob));
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_open_record(key, 0, 1, blob, stored_size, plain, &out_len));
  TEST_ASSERT_EQUAL_UINT8(1, plain[0]);

  TEST_ASSERT_EQUAL(EDB_DELETED, db.readRec(2, blob));

  TEST_ASSERT_EQUAL(EDB_OK, db.readRec(3, blob));
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_open_record(key, 0, 3, blob, stored_size, plain, &out_len));
  TEST_ASSERT_EQUAL_UINT8(3, plain[0]);
  // opening record 3's ciphertext under the wrong identity fails (swap protection)
  TEST_ASSERT_EQUAL_INT(-2, edb_crypto_open_record(key, 0, 1, blob, stored_size, plain, &out_len));
}

int run_crypto_integrity_tests() {
  UNITY_BEGIN();
  RUN_TEST(test_encrypted_records_opaque_roundtrip);
  return UNITY_END();
}
#else
int run_crypto_integrity_tests() { return 0; }
#endif
