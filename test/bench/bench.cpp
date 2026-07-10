/*
  EDB storage benchmark.

  Counts *storage byte I/O* -- the metric that dominates on EEPROM/flash, where a byte write costs
  ~3.3 ms and a read ~1 us -- for the same logical workload across library versions/configurations.

    writes : calls to the byte write handler
    eff_w  : writes whose value actually differs from what is stored (real EEPROM cell writes,
             i.e. what an EEPROM.update() handler would physically commit)
    reads  : calls to the byte read handler

  The dataset is NOT generated here: it is loaded from test/data/sensorlog.bin (see
  tools/gen_datasets.py), so every version under test writes byte-for-byte identical records and the
  data is independently verifiable. The FNV-1a hash of the loaded records is printed.

  Build/run with test/bench/run.sh. See docs/BENCHMARK.md.
*/
#include "Arduino.h"
#include "EDB.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Packed 22-byte record; must match tools/gen_datasets.py (REC_FMT "<IIhHBB8s"). */
struct __attribute__((packed)) SensorRec {
  uint32_t id;
  uint32_t ts;
  int16_t  temp_c100;
  uint16_t humidity;
  uint8_t  status;
  uint8_t  channel;
  char     tag[8];
};
#define REC_SIZE ((unsigned int)sizeof(SensorRec))

static uint64_t fnv1a(const void *data, size_t len) {   // matches tools/gen_datasets.py fnv1a64
  const unsigned char *p = (const unsigned char *)data;
  uint64_t h = 0xCBF29CE484222325ULL;
  for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x100000001B3ULL; }
  return h;
}

static std::vector<SensorRec> loadRecords(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) { fprintf(stderr, "FATAL: cannot open %s\n", path.c_str()); exit(2); }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0 || (n % (long)REC_SIZE) != 0) { fprintf(stderr, "FATAL: %s not a multiple of %u\n", path.c_str(), REC_SIZE); exit(2); }
  std::vector<SensorRec> v((size_t)n / REC_SIZE);
  if (n && fread(v.data(), 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "FATAL: short read %s\n", path.c_str()); exit(2); }
  fclose(f);
  return v;
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
  std::string data_dir = (argc > 1) ? argv[1] : "test/data";
  unsigned long K = (argc > 2) ? strtoul(argv[2], 0, 10) : 16;

  g_data = loadRecords(data_dir + "/sensorlog.bin");
  g_upd = loadRecords(data_dir + "/sensorlog_updates.bin");

  const unsigned long RESERVE = K + 64;                      // tail records for the mutation probes
  if (g_data.size() <= RESERVE) { fprintf(stderr, "FATAL: dataset too small\n"); return 2; }
  const unsigned long N = (unsigned long)g_data.size() - RESERVE;

  const unsigned long stride = REC_SIZE + 3;
  const unsigned long TABLE = 256 + (unsigned long)g_data.size() * stride;
  store.assign(TABLE, 0xFF);

  printf("\n=== %s ===\n", VERSION_NAME);
  printf("dataset=%s/sensorlog.bin  loaded=%zu recs  N=%lu K=%lu rec_size=%u  fnv1a=%016llx\n",
         data_dir.c_str(), g_data.size(), N, K, REC_SIZE,
         (unsigned long long)fnv1a(g_data.data(), g_data.size() * sizeof(SensorRec)));

  // Each write phase starts from ERASED storage (0xFF): the realistic first-write case, where
  // eff_w equals the real number of EEPROM cells written.
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
