/*
  EDB storage benchmark.

  Counts *storage byte I/O* -- the metric that dominates on EEPROM/flash, where a byte write costs
  ~3.3 ms and a read ~1 us -- for the same logical workload across library versions/configurations.

    writes : calls to the byte write handler
    eff_w  : writes whose value actually differs from what is stored (real EEPROM cell writes,
             i.e. what an EEPROM.update() handler would physically commit)
    reads  : calls to the byte read handler

  A realistic sensor-log dataset is generated up front from a fixed seed, so every version under
  test writes byte-for-byte identical records. The dataset's FNV-1a hash is printed to prove it.

  Build/run with test/bench/run.sh. See docs/BENCHMARK.md.
*/
#include "Arduino.h"
#include "EDB.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* ---------------------------------------------------------------- record + dataset */

struct __attribute__((packed)) SensorRec {
  uint32_t id;        // logical record id (monotonic)
  uint32_t ts;        // unix seconds, mostly increasing
  int16_t  temp_c100; // centi-degrees C
  uint16_t humidity;  // 0..10000
  uint8_t  status;    // flag bits
  uint8_t  channel;   // 0..7
  char     tag[8];    // fixed-width name, not necessarily NUL-terminated
};

#define REC_SIZE ((unsigned int)sizeof(SensorRec))

static uint32_t rng_state = 0x1234abcdu;
static uint32_t rnd() {
  uint32_t x = rng_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return rng_state = x;
}

static const char *TAGS[8] = {"kitchen", "garage", "attic", "cellar", "porch", "shed", "office", "lab"};

// Fresh readings: id/tag/channel are stable, ts/temp/humidity/status vary (a realistic sensor log).
static void genDataset(std::vector<SensorRec> &out, unsigned long n, uint32_t seed) {
  rng_state = seed;
  out.resize(n);
  for (unsigned long i = 0; i < n; i++) {
    SensorRec r;
    memset(&r, 0, sizeof(r));
    r.id = (uint32_t)(1000 + i);
    r.ts = 1700000000u + (uint32_t)(i * 10) + (rnd() % 7);
    r.temp_c100 = (int16_t)(500 + (int)(rnd() % 2000));
    r.humidity = (uint16_t)(3000 + (rnd() % 5000));
    r.status = (uint8_t)(rnd() & 0x0F);
    r.channel = (uint8_t)(i % 8);
    size_t tl = strlen(TAGS[i % 8]);
    if (tl > sizeof(r.tag)) tl = sizeof(r.tag);
    memcpy(r.tag, TAGS[i % 8], tl);
    out[i] = r;
  }
}

// A later reading from the same sensor: same id/tag/channel, new ts/temp/humidity/status.
// This is what an updateRec() really looks like, and only a few bytes change.
static void genUpdates(const std::vector<SensorRec> &base, std::vector<SensorRec> &out, uint32_t seed) {
  rng_state = seed;
  out = base;
  for (size_t i = 0; i < out.size(); i++) {
    out[i].ts += 3600 + (rnd() % 60);
    out[i].temp_c100 = (int16_t)(500 + (int)(rnd() % 2000));
    out[i].humidity = (uint16_t)(3000 + (rnd() % 5000));
    out[i].status = (uint8_t)(rnd() & 0x0F);
  }
}

static uint64_t fnv1a(const void *data, size_t len) {
  const unsigned char *p = (const unsigned char *)data;
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
  return h;
}

/* ---------------------------------------------------------------- instrumented storage */

static std::vector<unsigned char> store;
static unsigned long g_w = 0, g_r = 0, g_eff = 0;

static void oob(unsigned long a) { fprintf(stderr, "FATAL: storage access out of range at %lu\n", a); abort(); }
static void wb(unsigned long a, const uint8_t v) { if (a >= store.size()) oob(a); if (store[a] != v) g_eff++; store[a] = v; g_w++; }
static uint8_t rb(unsigned long a) { if (a >= store.size()) oob(a); g_r++; return store[a]; }

EDB db(&wb, &rb);

struct M { unsigned long w, eff, r; };
static void start() { g_w = 0; g_eff = 0; g_r = 0; }
static M stop() { return M{g_w, g_eff, g_r}; }

static M g_total = {0, 0, 0};
static void row(const char *name, M m, unsigned long ops) {
  g_total.w += m.w; g_total.eff += m.eff; g_total.r += m.r;
  printf("%-22s %12lu %12lu %12lu", name, m.w, m.eff, m.r);
  if (ops > 0) printf("   %10.1f %10.1f", (double)m.w / ops, (double)m.eff / ops);
  printf("\n");
}

/* ---------------------------------------------------------------- workload */

static std::vector<SensorRec> g_data, g_upd;

static unsigned long appendIdx(unsigned long i) {
#ifdef EDB_VERSION
  unsigned long recno = 0;
  db.appendRec((EDB_Rec)&g_data[i], &recno);
  return recno;
#else
  db.appendRec((EDB_Rec)&g_data[i]);
  return db.count();   // shift-based: the newest record is at the end
#endif
}

int main(int argc, char **argv) {
  unsigned long N = (argc > 1) ? strtoul(argv[1], 0, 10) : 10000;
  unsigned long K = (argc > 2) ? strtoul(argv[2], 0, 10) : 16;

  genDataset(g_data, N + K + 64, 0x1234abcdu);
  genUpdates(g_data, g_upd, 0x77aa55ffu);

  const unsigned long stride = REC_SIZE + 3;                 // v3 slot framing; roomy for 1.0.x
  const unsigned long TABLE = 256 + (N + K + 64) * stride;
  store.assign(TABLE, 0xFF);

  printf("\n=== %s ===\n", VERSION_NAME);
  printf("N=%lu K=%lu rec_size=%u table=%lu bytes  dataset_fnv1a=%016llx\n",
         N, K, REC_SIZE, TABLE,
         (unsigned long long)fnv1a(&g_data[0], g_data.size() * sizeof(SensorRec)));

  // Each write phase starts from ERASED storage (0xFF): the realistic first-write case, where
  // eff_w equals the real number of EEPROM cells written. Re-appending the same bytes into the
  // same physical slots would make eff_w meaninglessly small.
  auto freshTable = [&]() { store.assign(TABLE, 0xFF); db.create(0, TABLE, REC_SIZE); };

  store.assign(TABLE, 0xFF);
  start(); db.create(0, TABLE, REC_SIZE); M mCreate = stop();

  freshTable();
  start(); for (unsigned long i = 0; i < N; i++) appendIdx(i); M mApp = stop();

#ifdef EDB_VERSION
  freshTable();
  start();
  db.beginBatch();
  for (unsigned long i = 0; i < N; i++) appendIdx(i);
  db.endBatch();
  M mBatch = stop();
#endif

  // Populate the table with g_data for the read / update / delete phases below.
  freshTable();
  for (unsigned long i = 0; i < N; i++) appendIdx(i);

  start(); for (unsigned long i = 1; i <= N; i++) { SensorRec o; db.readRec(i, (EDB_Rec)&o); } M mRead = stop();
  start(); for (unsigned long i = 1; i <= N; i++) db.updateRec(i, (EDB_Rec)&g_upd[i - 1]); M mUpd = stop();

  // "Remove the first (oldest) live record, K times."
  //   shift-based: deleteRec(1) -- every call shifts the whole tail down.
  //   v3:          firstRec() then tombstone the slot.
  start();
#ifdef EDB_VERSION
  for (unsigned long i = 0; i < K; i++) { unsigned long r = db.firstRec(); db.deleteRec(r); }
#else
  for (unsigned long i = 0; i < K; i++) db.deleteRec(1);
#endif
  M mDelFront = stop();

  std::vector<unsigned long> batch;
  for (unsigned long i = 0; i < K; i++) batch.push_back(appendIdx(N + i));

  // "Remove the K most recently appended records" (the cheap case for a shift-based store).
  start();
#ifdef EDB_VERSION
  for (unsigned long i = 0; i < K; i++) { db.deleteRec(batch.back()); batch.pop_back(); }
#else
  for (unsigned long i = 0; i < K; i++) db.deleteRec(db.count());
#endif
  M mDelEnd = stop();

  for (unsigned long i = 0; i < K; i++) appendIdx(N + i);

  // NOTE: v3's insertRec() is not positional (it allocates a free slot), so this compares the cost
  // of the call, not identical semantics.
  start(); for (unsigned long i = 0; i < K; i++) db.insertRec(1, (EDB_Rec)&g_data[N + K + i]); M mIns = stop();

  start();
  unsigned long seen = 0;
#ifdef EDB_VERSION
  for (unsigned long rc = db.firstRec(); rc != 0; rc = db.nextRec(rc)) { SensorRec o; db.readRec(rc, (EDB_Rec)&o); seen++; }
#else
  for (unsigned long rc = 1; rc <= db.count(); rc++) { SensorRec o; db.readRec(rc, (EDB_Rec)&o); seen++; }
#endif
  M mScan = stop();

  printf("%-22s %12s %12s %12s   %10s %10s\n", "phase", "writes", "eff_w", "reads", "w/op", "effw/op");
  row("create", mCreate, 0);
  row("append xN", mApp, N);
#ifdef EDB_VERSION
  row("batch append xN", mBatch, N);
#endif
  row("read xN", mRead, N);
  row("update xN", mUpd, N);
  row("del first xK", mDelFront, K);
  row("del newest xK", mDelEnd, K);
  row("insertRec(1) xK", mIns, K);
  row("scan all", mScan, seen);

  printf("%-22s %12lu %12lu %12lu\n", "TOTAL", g_total.w, g_total.eff, g_total.r);
  printf("EEPROM time @3.3ms/cell-write: raw=%.1f s   with EEPROM.update()=%.1f s\n",
         g_total.w * 0.0033, g_total.eff * 0.0033);
  printf("final count=%lu  scanned=%lu\n", (unsigned long)db.count(), seen);
  return 0;
}
