/*
  EDB storage benchmark.

  Counts *storage byte I/O* -- the metric that dominates on EEPROM/flash, where a byte write costs
  ~3.3 ms and a read ~1 us -- for the same logical workload across library versions/configurations.

    writes : calls to the byte write handler
    eff_w  : writes whose value actually differs from what is stored (real EEPROM cell writes)
    reads  : calls to the byte read handler

  Build/run with test/bench/run.sh. See docs/BENCHMARK.md.
*/
#include "Arduino.h"
#include "EDB.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static std::vector<unsigned char> store;
static unsigned long g_w = 0, g_r = 0, g_eff = 0;

static void ensure(unsigned long n) { if (store.size() < n) store.resize(n, 0xFF); }
static void wb(unsigned long a, const uint8_t v) { ensure(a + 1); if (store[a] != v) g_eff++; store[a] = v; g_w++; }
static uint8_t rb(unsigned long a) { ensure(a + 1); g_r++; return store[a]; }

EDB db(&wb, &rb);

#define REC_SIZE 8
struct Rec { unsigned char b[REC_SIZE]; };

struct M { unsigned long w, eff, r; };
static void start() { g_w = 0; g_eff = 0; g_r = 0; }
static M stop() { return M{g_w, g_eff, g_r}; }

static void row(const char *name, M m, long ops) {
  printf("%-22s %10lu %10lu %10lu", name, m.w, m.eff, m.r);
  if (ops > 0) printf("   %9.1f %9.1f", (double)m.w / ops, (double)m.eff / ops);
  printf("\n");
}

static Rec g_rec;
static unsigned long g_seq = 0;

// Records must carry DISTINCT bytes: otherwise EDB_WRITE_IF_DIFFERENT legitimately skips rewriting
// a slot that already holds identical data, and the measurement is meaningless.
static void freshRec() {
  g_seq++;
  for (int i = 0; i < REC_SIZE; i++)
    g_rec.b[i] = (unsigned char)((g_seq >> (8 * (i & 3))) ^ (unsigned)(i * 31));
}

static unsigned long appendOne() {
  freshRec();
#ifdef EDB_VERSION
  unsigned long id = 0;
  db.appendRec((EDB_Rec)&g_rec, &id);
  return id;
#else
  db.appendRec((EDB_Rec)&g_rec);
  return db.count();   // shift-based: the newest record is at the end
#endif
}

int main(int argc, char **argv) {
  long N = (argc > 1) ? atol(argv[1]) : 1024;
  long K = (argc > 2) ? atol(argv[2]) : 16;
  const unsigned long TABLE = 1048576UL;

  store.assign(TABLE, 0xFF);

  start(); db.create(0, TABLE, REC_SIZE); M mCreate = stop();
  start(); for (long i = 0; i < N; i++) appendOne(); M mApp = stop();

#ifdef EDB_VERSION
  db.clear();
  start();
  db.beginBatch();
  for (long i = 0; i < N; i++) appendOne();
  db.endBatch();
  M mBatch = stop();
  db.clear();
  for (long i = 0; i < N; i++) appendOne();   // restore state for the phases below
#endif

  start(); for (long i = 1; i <= N; i++) { Rec o; db.readRec(i, (EDB_Rec)&o); } M mRead = stop();
  start(); for (long i = 1; i <= N; i++) { freshRec(); db.updateRec(i, (EDB_Rec)&g_rec); } M mUpd = stop();

  // "Remove the first (oldest) live record, K times."
  //   shift-based: deleteRec(1) each time -- every call shifts the whole tail down.
  //   v3:          firstRec() then tombstone the slot.
  start();
#ifdef EDB_VERSION
  for (long i = 0; i < K; i++) { unsigned long r = db.firstRec(); db.deleteRec(r); }
#else
  for (long i = 0; i < K; i++) db.deleteRec(1);
#endif
  M mDelFront = stop();

  std::vector<unsigned long> batch;
  for (long i = 0; i < K; i++) batch.push_back(appendOne());

  // "Remove the K most recently appended records" (the cheap case for a shift-based store).
  start();
#ifdef EDB_VERSION
  for (long i = 0; i < K; i++) { db.deleteRec(batch.back()); batch.pop_back(); }
#else
  for (long i = 0; i < K; i++) db.deleteRec(db.count());
#endif
  M mDelEnd = stop();

  for (long i = 0; i < K; i++) appendOne();

  // NOTE: v3's insertRec() is not positional (it allocates a free slot), so this compares the cost
  // of the call, not identical semantics.
  start(); for (long i = 0; i < K; i++) { freshRec(); db.insertRec(1, (EDB_Rec)&g_rec); } M mIns = stop();

  start();
  long seen = 0;
#ifdef EDB_VERSION
  for (unsigned long rc = db.firstRec(); rc != 0; rc = db.nextRec(rc)) { Rec o; db.readRec(rc, (EDB_Rec)&o); seen++; }
#else
  for (unsigned long rc = 1; rc <= db.count(); rc++) { Rec o; db.readRec(rc, (EDB_Rec)&o); seen++; }
#endif
  M mScan = stop();

  printf("\n=== %s   N=%ld K=%ld rec_size=%d ===\n", VERSION_NAME, N, K, REC_SIZE);
  printf("%-22s %10s %10s %10s   %9s %9s\n", "phase", "writes", "eff_w", "reads", "w/op", "effw/op");
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
  printf("final count=%lu  scanned=%ld\n", (unsigned long)db.count(), seen);
  return 0;
}
