/*
  EDB.h
  Extended Database Library for Arduino
  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary

  v3 storage format (EDB 3.x):
    - Redundant, CRC32-checksummed header written ping-pong for atomic, crash-safe updates.
    - Stable slot IDs: recno never renumbers. deleteRec tombstones a slot (O(1)); appendRec
      reuses a freed slot via an intrusive free-list (O(1)) or grows the table.
    - Per-record CRC16 detects torn writes / bit-rot; verified on read by default.
    - Records are opaque to the library; encryption (if any) is a caller concern.
  Legacy v1/v2 files are detected on open() and reported as EDB_NEEDS_MIGRATION; convert them
  offline with tools/edb_migrate.py. The library never upgrades a file in place.
*/

#ifndef EDB_H
#define EDB_H

#ifndef EDB_PROM
#define EDB_PROM
#endif

#include <stdint.h>

#define EDB_FLAG 0xDB
#define EDB_VERSION 3

/* One header copy is 48 bytes; two copies are stored back-to-back for redundancy. */
#define EDB_HEADER_COPY_SIZE 48
#define EDB_HEADER_SPAN (2 * EDB_HEADER_COPY_SIZE)   /* default data_offset = 96 */

/* Slot status byte values (anything else -> treated as empty/never-used). */
#define EDB_SLOT_LIVE 0xA5
#define EDB_SLOT_TOMBSTONE 0x5A

/* Sentinel for "no free slot". */
#define EDB_FREE_NONE 0xFFFFFFFFUL

/* Verify each record's CRC on read (1) or trust it (0, minimum read CPU). */
#ifndef EDB_VERIFY_ON_READ
#define EDB_VERIFY_ON_READ 1
#endif

/* Store the header twice for atomic, crash-safe updates (1) or once (0, discouraged). */
#ifndef EDB_HEADER_REDUNDANT
#define EDB_HEADER_REDUNDANT 1
#endif

#if defined(__GNUC__)
#define EDB_PACKED __attribute__((packed))
#else
#define EDB_PACKED
#endif

/* On-disk header copy. All multi-byte fields little-endian (matches AVR/ESP32/x86 hosts). */
struct EDB_PACKED EDB_Header
{
  uint8_t  magic;         /* 0  : EDB_FLAG (0xDB) */
  uint8_t  version;       /* 1  : EDB_VERSION (3) */
  uint16_t flags;         /* 2  : bit0 = encrypted (informational) */
  uint32_t seq;           /* 4  : monotonic; higher copy wins on open */
  uint32_t n_slots;       /* 8  : high-water mark of allocated slots */
  uint32_t n_live;        /* 12 : live record count == count() */
  uint16_t rec_size;      /* 16 : stored bytes per record (opaque payload) */
  uint16_t slot_stride;   /* 18 : on-disk bytes per slot */
  uint32_t table_size;    /* 20 : total bytes reserved for this table */
  uint32_t free_head;     /* 24 : free-list head slot index, or EDB_FREE_NONE */
  uint16_t data_offset;   /* 28 : record region start relative to head_ptr */
  uint8_t  reserved[14];  /* 30 : zeroed; room for future fields without a rewrite */
  uint32_t header_crc;    /* 44 : CRC32 over bytes 0..43 */
};

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(EDB_Header) == EDB_HEADER_COPY_SIZE, "EDB_Header must be 48 bytes");
#endif

enum EDB_Status {
  EDB_OK,
  EDB_ERROR,
  EDB_OUT_OF_RANGE,
  EDB_TABLE_FULL,
  EDB_DELETED,          /* recno refers to a tombstoned (deleted) slot */
  EDB_CORRUPT,          /* record CRC (or header CRC) failed verification */
  EDB_NEEDS_MIGRATION   /* a legacy v1/v2 file: migrate offline with tools/edb_migrate.py */
};

typedef byte* EDB_Rec;

#define EDB_REC (byte*)(void*)&

class EDB {
  public:
    typedef void EDB_Write_Handler(unsigned long, const uint8_t);
    typedef uint8_t EDB_Read_Handler(unsigned long);
    typedef void EDB_Write_Buffer(unsigned long, const byte*, unsigned int);
    typedef void EDB_Read_Buffer(unsigned long, byte*, unsigned int);
    EDB(EDB_Write_Handler *, EDB_Read_Handler *);
    EDB(EDB_Write_Buffer *, EDB_Read_Buffer *);
    EDB_Status create(unsigned long, unsigned long, unsigned int);
    EDB_Status open(unsigned long);
    EDB_Status readRec(unsigned long, EDB_Rec);
    EDB_Status deleteRec(unsigned long);
    EDB_Status insertRec(unsigned long, const EDB_Rec);
    EDB_Status updateRec(unsigned long, const EDB_Rec);
    EDB_Status appendRec(const EDB_Rec rec);
    EDB_Status appendRec(const EDB_Rec rec, unsigned long* out_recno);
    // Iterate live records (slots may be sparse after deletes). Return 0 when exhausted.
    unsigned long firstRec();
    unsigned long nextRec(unsigned long recno);
    bool isLive(unsigned long recno);
    EDB_Status compact();
    unsigned long limit();
    unsigned long count();
    EDB_Status clear();
    unsigned long headPtr() const;
    unsigned long tableSize() const;
    static unsigned long nextTableOffset(unsigned long head_ptr, unsigned long table_size);
    EDB_Status openOrCreate(unsigned long head_ptr, unsigned long table_size, unsigned int rec_size);
#ifdef EDB_TEST
    static void setMallocFail(bool fail);
#endif
  private:
    unsigned long EDB_head_ptr;
    unsigned long EDB_table_ptr;      /* == EDB_head_ptr + data_offset */
    unsigned int _active_copy;        /* 0 or 1: which header copy is current */
    EDB_Write_Handler *_write_byte;
    EDB_Read_Handler *_read_byte;
    EDB_Write_Buffer *_write_buffer;
    EDB_Read_Buffer *_read_buffer;
    EDB_Header EDB_head;

    void edbWrite(unsigned long ee, const byte* p, unsigned int);
    void edbRead(unsigned long ee, byte* p, unsigned int);
    byte readByte(unsigned long address) const;

    EDB_Status writeHead();                    /* publish header to the inactive copy (ping-pong) */
    EDB_Status readHead();                     /* load newest valid copy; detect legacy */
    bool loadHeaderCopy(unsigned int copy, EDB_Header& out) const;

    unsigned long slotOffset(unsigned long index) const;   /* index is 0-based */
    unsigned long maxSlots() const;
    bool hasFreeList() const;                  /* rec_size large enough for intrusive free-list */
    unsigned long allocSlot();                 /* returns slot index, or EDB_FREE_NONE if full */
    EDB_Status writeSlot(unsigned long index, const byte* payload);
    EDB_Status readSlot(unsigned long index, byte* payload);  /* verifies status + crc */

    void* edbMalloc(unsigned int size);
};

#ifndef EDB_NO_GLOBAL
// Legacy 1.0.x header declared a global instance; sketches that use `EDB edb(...)`
// must still provide the definition. Multiple tables use separate EDB objects instead.
extern EDB edb;
#endif

#endif
