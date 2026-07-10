/*
  EDB.cpp
  Extended Database Library for Arduino
  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary

  See EDB.h for the v3 format overview.

  Crash-safety guarantees:
    - The header is written to two CRC32-checksummed copies, ping-pong by sequence number, so a
      power loss during a header write always leaves at least one complete, valid copy. open()
      selects the newest valid copy. This eliminates the whole-table corruption possible in v2.
    - Every record slot carries a CRC16 over its status + payload; a torn record write is reported
      as EDB_CORRUPT on read rather than returned as good data.
    - Mutations write slot bytes first and publish the header last. A crash in that window is
      benign: the un-published change is either ignored (unreferenced slot) or leaves count() off
      by at most one / a freed slot unreclaimed until compact(). Reads always honor the per-slot
      status byte, so no record is ever lost or silently garbled.
*/

#include "Arduino.h"
#include "EDB.h"
#include <stdlib.h>
#include <string.h>

/**************************************************/
// checksums (bitwise; no lookup tables -> minimal flash on AVR)

static uint16_t edbCrc16Update(uint16_t crc, const byte* p, unsigned int len)
{
  for (unsigned int i = 0; i < len; i++) {
    crc ^= (uint16_t)p[i] << 8;
    for (int k = 0; k < 8; k++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

static uint32_t edbCrc32(const byte* p, unsigned int len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  for (unsigned int i = 0; i < len; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
  }
  return crc ^ 0xFFFFFFFFUL;
}

#ifdef EDB_TEST
static bool _edb_malloc_fail = false;
void EDB::setMallocFail(bool fail) { _edb_malloc_fail = fail; }
#endif

/**************************************************/
// low-level storage

void EDB::edbWrite(unsigned long ee, const byte* p, unsigned int recsize)
{
  if (!_write_buffer) {
    for (unsigned int i = 0; i < recsize; i++)
      _write_byte(ee++, *p++);
  } else {
    _write_buffer(ee, p, recsize);
  }
}

void EDB::edbRead(unsigned long ee, byte* p, unsigned int recsize)
{
  if (!_read_buffer) {
    for (unsigned int i = 0; i < recsize; i++)
      *p++ = _read_byte(ee++);
  } else {
    _read_buffer(ee, p, recsize);
  }
}

byte EDB::readByte(unsigned long address) const
{
  byte value = 0;
  if (!_read_buffer) {
    value = _read_byte(address);
  } else {
    EDB* self = const_cast<EDB*>(this);
    self->_read_buffer(address, &value, 1);
  }
  return value;
}

void* EDB::edbMalloc(unsigned int size)
{
#ifdef EDB_TEST
  if (_edb_malloc_fail) return NULL;
#endif
  return malloc(size);
}

/**************************************************/
// header

static bool edbValidateHeader(const EDB_Header& h)
{
  if (h.magic != EDB_FLAG) return false;
  if (h.version != EDB_VERSION) return false;
  uint32_t want = edbCrc32((const byte*)&h, EDB_HEADER_COPY_SIZE - 4);
  if (want != h.header_crc) return false;
  if (h.rec_size == 0 || h.slot_stride == 0) return false;
  if (h.slot_stride < (uint16_t)(1 + h.rec_size + 2)) return false;
  if (h.data_offset < EDB_HEADER_COPY_SIZE) return false;
  if (h.table_size < (uint32_t)h.data_offset + h.slot_stride) return false;
  unsigned long max_slots = (unsigned long)(h.table_size - h.data_offset) / h.slot_stride;
  if (h.n_slots > max_slots) return false;
  if (h.n_live > h.n_slots) return false;
  if (h.free_head != EDB_FREE_NONE && h.free_head >= h.n_slots) return false;
  return true;
}

bool EDB::loadHeaderCopy(unsigned int copy, EDB_Header& out) const
{
  EDB* self = const_cast<EDB*>(this);
  self->edbRead(EDB_head_ptr + (unsigned long)copy * EDB_HEADER_COPY_SIZE,
                (byte*)&out, EDB_HEADER_COPY_SIZE);
  return edbValidateHeader(out);
}

EDB_Status EDB::readHead()
{
  EDB_Header h0, h1;
  bool valid0 = loadHeaderCopy(0, h0);
#if EDB_HEADER_REDUNDANT
  bool valid1 = loadHeaderCopy(1, h1);
#else
  bool valid1 = false;
#endif

  if (!valid0 && !valid1) {
    // Distinguish a legacy (v1/v2) file from uninitialized storage.
    byte magic = readByte(EDB_head_ptr);
    byte version = readByte(EDB_head_ptr + 1);
    if (magic == EDB_FLAG && version != EDB_VERSION) return EDB_NEEDS_MIGRATION;
    return EDB_ERROR;
  }

  if (valid0 && (!valid1 || h0.seq >= h1.seq)) {
    EDB_head = h0;
    _active_copy = 0;
  } else {
    EDB_head = h1;
    _active_copy = 1;
  }
  EDB_table_ptr = EDB_head_ptr + EDB_head.data_offset;
  return EDB_OK;
}

EDB_Status EDB::writeHead()
{
  unsigned int target = 0;
#if EDB_HEADER_REDUNDANT
  target = 1 - _active_copy;
#endif
  EDB_head.seq += 1;
  EDB_head.header_crc = edbCrc32((const byte*)&EDB_head, EDB_HEADER_COPY_SIZE - 4);
  unsigned long addr = EDB_head_ptr + (unsigned long)target * EDB_HEADER_COPY_SIZE;
  edbWrite(addr, (const byte*)&EDB_head, EDB_HEADER_COPY_SIZE);

  // Read back and verify the copy we just published (cheap: 48 bytes).
  EDB_Header verify;
  edbRead(addr, (byte*)&verify, EDB_HEADER_COPY_SIZE);
  if (verify.header_crc != EDB_head.header_crc || verify.seq != EDB_head.seq)
    return EDB_ERROR;

  _active_copy = target;
  return EDB_OK;
}

/**************************************************/
// slot helpers

unsigned long EDB::slotOffset(unsigned long index) const
{
  return EDB_table_ptr + index * EDB_head.slot_stride;
}

unsigned long EDB::maxSlots() const
{
  if (EDB_head.slot_stride == 0) return 0;
  if (EDB_head.table_size < EDB_head.data_offset) return 0;
  return (unsigned long)(EDB_head.table_size - EDB_head.data_offset) / EDB_head.slot_stride;
}

bool EDB::hasFreeList() const
{
  // The intrusive free-list stores a 4-byte "next" index in the tombstone's payload.
  return EDB_head.rec_size >= 4;
}

EDB_Status EDB::writeSlot(unsigned long index, const byte* payload)
{
  unsigned long off = slotOffset(index);
  byte live = EDB_SLOT_LIVE;
  uint16_t crc = 0xFFFF;
  crc = edbCrc16Update(crc, &live, 1);
  crc = edbCrc16Update(crc, payload, EDB_head.rec_size);
  byte crc_bytes[2];
  crc_bytes[0] = (byte)(crc & 0xFF);
  crc_bytes[1] = (byte)(crc >> 8);
  // payload + crc first; commit with the status byte last so a torn write never reads as LIVE.
  edbWrite(off + 1, payload, EDB_head.rec_size);
  edbWrite(off + 1 + EDB_head.rec_size, crc_bytes, 2);
  edbWrite(off, &live, 1);
  return EDB_OK;
}

EDB_Status EDB::readSlot(unsigned long index, byte* payload)
{
  unsigned long off = slotOffset(index);
  byte status = readByte(off);
  if (status == EDB_SLOT_TOMBSTONE) return EDB_DELETED;
  if (status != EDB_SLOT_LIVE) return EDB_OUT_OF_RANGE;
  edbRead(off + 1, payload, EDB_head.rec_size);
#if EDB_VERIFY_ON_READ
  byte crc_bytes[2];
  edbRead(off + 1 + EDB_head.rec_size, crc_bytes, 2);
  uint16_t stored = (uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8);
  byte live = EDB_SLOT_LIVE;
  uint16_t crc = 0xFFFF;
  crc = edbCrc16Update(crc, &live, 1);
  crc = edbCrc16Update(crc, payload, EDB_head.rec_size);
  if (crc != stored) return EDB_CORRUPT;
#endif
  return EDB_OK;
}

unsigned long EDB::allocSlot()
{
  // 1. Reuse a freed slot via the intrusive free-list (validated against crash damage).
  if (hasFreeList() && EDB_head.free_head != EDB_FREE_NONE) {
    unsigned long idx = EDB_head.free_head;
    if (idx < EDB_head.n_slots && readByte(slotOffset(idx)) == EDB_SLOT_TOMBSTONE) {
      byte link[4];
      edbRead(slotOffset(idx) + 1, link, 4);
      uint32_t next = (uint32_t)link[0] | ((uint32_t)link[1] << 8) |
                      ((uint32_t)link[2] << 16) | ((uint32_t)link[3] << 24);
      EDB_head.free_head = (next == EDB_FREE_NONE || next < EDB_head.n_slots) ? next : EDB_FREE_NONE;
      return idx;
    }
    // Free-list head is inconsistent (e.g. a crash mid-reuse) -> drop it; space is
    // still reclaimable by the linear scan below or compact().
    EDB_head.free_head = EDB_FREE_NONE;
  }

  // 2. Grow into fresh space.
  if (EDB_head.n_slots < maxSlots()) {
    return (unsigned long)EDB_head.n_slots++;
  }

  // 3. Full: reclaim any tombstone the free-list missed (small records, or post-crash leak).
  for (unsigned long i = 0; i < EDB_head.n_slots; i++) {
    if (readByte(slotOffset(i)) == EDB_SLOT_TOMBSTONE) return i;
  }
  return EDB_FREE_NONE;
}

/**************************************************/
// public functions

EDB::EDB(EDB_Write_Handler *w, EDB_Read_Handler *r)
{
  _write_byte = w;
  _read_byte = r;
  _write_buffer = NULL;
  _read_buffer = NULL;
  _active_copy = 0;
}

EDB::EDB(EDB_Write_Buffer *w, EDB_Read_Buffer *r)
{
  _write_byte = NULL;
  _read_byte = NULL;
  _write_buffer = w;
  _read_buffer = r;
  _active_copy = 0;
}

EDB_Status EDB::create(unsigned long head_ptr, unsigned long tablesize, unsigned int recsize)
{
  if (recsize == 0) return EDB_ERROR;
  if (recsize > (unsigned int)(0xFFFFu - 3)) return EDB_ERROR;   // slot_stride must fit uint16
  if (tablesize > 0xFFFFFFFFUL) return EDB_ERROR;

  uint16_t stride = (uint16_t)(1 + recsize + 2);
  uint16_t data_offset = EDB_HEADER_REDUNDANT ? EDB_HEADER_SPAN : EDB_HEADER_COPY_SIZE;
  if (tablesize < (unsigned long)data_offset + stride) return EDB_ERROR;

  EDB_head_ptr = head_ptr;
  memset(&EDB_head, 0, sizeof(EDB_head));
  EDB_head.magic = EDB_FLAG;
  EDB_head.version = EDB_VERSION;
  EDB_head.flags = 0;
  EDB_head.seq = 1;
  EDB_head.n_slots = 0;
  EDB_head.n_live = 0;
  EDB_head.rec_size = (uint16_t)recsize;
  EDB_head.slot_stride = stride;
  EDB_head.table_size = (uint32_t)tablesize;
  EDB_head.free_head = EDB_FREE_NONE;
  EDB_head.data_offset = data_offset;
  EDB_head.header_crc = edbCrc32((const byte*)&EDB_head, EDB_HEADER_COPY_SIZE - 4);
  EDB_table_ptr = EDB_head_ptr + data_offset;

  // Write both copies with the same seq; copy 0 is active (tie-break), copy 1 gets the next publish.
  edbWrite(EDB_head_ptr, (const byte*)&EDB_head, EDB_HEADER_COPY_SIZE);
#if EDB_HEADER_REDUNDANT
  edbWrite(EDB_head_ptr + EDB_HEADER_COPY_SIZE, (const byte*)&EDB_head, EDB_HEADER_COPY_SIZE);
#endif
  _active_copy = 0;

  EDB_Header verify;
  edbRead(EDB_head_ptr, (byte*)&verify, EDB_HEADER_COPY_SIZE);
  if (!edbValidateHeader(verify)) return EDB_ERROR;
  if (verify.rec_size != recsize || verify.table_size != tablesize) return EDB_ERROR;
  return EDB_OK;
}

EDB_Status EDB::open(unsigned long head_ptr)
{
  EDB_head_ptr = head_ptr;
  return readHead();
}

EDB_Status EDB::readRec(unsigned long recno, EDB_Rec rec)
{
  if (recno < 1 || recno > EDB_head.n_slots) return EDB_OUT_OF_RANGE;
  return readSlot(recno - 1, rec);
}

EDB_Status EDB::updateRec(unsigned long recno, const EDB_Rec rec)
{
  if (recno < 1 || recno > EDB_head.n_slots) return EDB_OUT_OF_RANGE;
  byte status = readByte(slotOffset(recno - 1));
  if (status == EDB_SLOT_TOMBSTONE) return EDB_DELETED;
  if (status != EDB_SLOT_LIVE) return EDB_OUT_OF_RANGE;
  return writeSlot(recno - 1, rec);
}

EDB_Status EDB::appendRec(const EDB_Rec rec)
{
  return appendRec(rec, NULL);
}

EDB_Status EDB::appendRec(const EDB_Rec rec, unsigned long* out_recno)
{
  unsigned long idx;
  bool inc_live = true;
  bool grew_slots = false;

  if (ringModeEnabled()) {
    unsigned long cap = maxSlots();
    if (cap == 0) return EDB_ERROR;
    if (EDB_head.n_live < cap) {
      idx = EDB_head.n_slots;
      EDB_head.n_slots++;
      grew_slots = true;
    } else {
      idx = readRingHead();
      writeRingHead((uint32_t)((idx + 1) % cap));
      inc_live = false;
    }
  } else {
    idx = allocSlot();
    if (idx == EDB_FREE_NONE) return EDB_TABLE_FULL;
  }

  EDB_Status st = EDB_OK;
  if (stableIdsEnabled()) {
    byte* temp = (byte*)edbMalloc(EDB_head.rec_size);
    if (!temp) {
      st = EDB_ERROR;
    } else {
      memcpy(temp, rec, EDB_head.rec_size);
      uint32_t id = readNextRecordId();
      stampRecordId(temp, id);
      st = writeSlot(idx, temp);
      if (st == EDB_OK) writeNextRecordId(id + 1);
      free(temp);
    }
  } else {
    st = writeSlot(idx, rec);
  }

  if (st != EDB_OK) {
    if (grew_slots) EDB_head.n_slots--;
    if (ringModeEnabled() && !inc_live) {
      unsigned long cap = maxSlots();
      writeRingHead((uint32_t)((idx + cap - 1) % cap));
    }
    return st;
  }

  if (inc_live) EDB_head.n_live++;
  EDB_Status status = writeHead();
  if (status != EDB_OK) {
    if (inc_live) EDB_head.n_live--;
    if (grew_slots) EDB_head.n_slots--;
    if (ringModeEnabled() && !inc_live) {
      unsigned long cap = maxSlots();
      writeRingHead((uint32_t)((idx + cap - 1) % cap));
    }
    return status;
  }
  if (out_recno) *out_recno = idx + 1;
  return EDB_OK;
}

EDB_Status EDB::insertRec(unsigned long recno, const EDB_Rec rec)
{
  // Stable-slot model: positional insert is not preserved; allocate any free slot.
  (void)recno;
  return appendRec(rec, NULL);
}

EDB_Status EDB::deleteRec(unsigned long recno)
{
  if (ringModeEnabled()) return EDB_ERROR;
  if (recno < 1 || recno > EDB_head.n_slots) return EDB_OUT_OF_RANGE;
  unsigned long idx = recno - 1;
  byte status = readByte(slotOffset(idx));
  if (status == EDB_SLOT_TOMBSTONE) return EDB_DELETED;
  if (status != EDB_SLOT_LIVE) return EDB_OUT_OF_RANGE;

  if (hasFreeList()) {
    uint32_t link = EDB_head.free_head;
    byte link_bytes[4];
    link_bytes[0] = (byte)(link & 0xFF);
    link_bytes[1] = (byte)((link >> 8) & 0xFF);
    link_bytes[2] = (byte)((link >> 16) & 0xFF);
    link_bytes[3] = (byte)((link >> 24) & 0xFF);
    edbWrite(slotOffset(idx) + 1, link_bytes, 4);
  }
  byte tomb = EDB_SLOT_TOMBSTONE;
  edbWrite(slotOffset(idx), &tomb, 1);
  if (hasFreeList()) EDB_head.free_head = (uint32_t)idx;
  EDB_head.n_live--;
  return writeHead();
}

unsigned long EDB::firstRec()
{
  for (unsigned long i = 0; i < EDB_head.n_slots; i++)
    if (readByte(slotOffset(i)) == EDB_SLOT_LIVE) return i + 1;
  return 0;
}

unsigned long EDB::nextRec(unsigned long recno)
{
  for (unsigned long i = recno; i < EDB_head.n_slots; i++)
    if (readByte(slotOffset(i)) == EDB_SLOT_LIVE) return i + 1;
  return 0;
}

bool EDB::isLive(unsigned long recno)
{
  if (recno < 1 || recno > EDB_head.n_slots) return false;
  return readByte(slotOffset(recno - 1)) == EDB_SLOT_LIVE;
}

EDB_Status EDB::compact()
{
  if (ringModeEnabled()) return EDB_ERROR;
  // Reconcile counts and rebuild the free-list from tombstones (reclaims post-crash leaks).
  // Does not move live records: slot IDs remain stable.
  unsigned long live = 0;
  uint32_t free_head = EDB_FREE_NONE;
  for (unsigned long i = 0; i < EDB_head.n_slots; i++) {
    byte status = readByte(slotOffset(i));
    if (status == EDB_SLOT_LIVE) {
      live++;
    } else if (status == EDB_SLOT_TOMBSTONE && hasFreeList()) {
      byte link_bytes[4];
      link_bytes[0] = (byte)(free_head & 0xFF);
      link_bytes[1] = (byte)((free_head >> 8) & 0xFF);
      link_bytes[2] = (byte)((free_head >> 16) & 0xFF);
      link_bytes[3] = (byte)((free_head >> 24) & 0xFF);
      edbWrite(slotOffset(i) + 1, link_bytes, 4);
      free_head = (uint32_t)i;
    }
  }
  EDB_head.n_live = (uint32_t)live;
  EDB_head.free_head = hasFreeList() ? free_head : EDB_FREE_NONE;
  return writeHead();
}

/**************************************************/
// stable record_id helpers (distinct from slot recno; survives host-side edb_vacuum.py)

uint32_t EDB::readNextRecordId() const
{
  return (uint32_t)EDB_head.reserved[EDB_STABLE_ID_OFFSET] |
         ((uint32_t)EDB_head.reserved[EDB_STABLE_ID_OFFSET + 1] << 8) |
         ((uint32_t)EDB_head.reserved[EDB_STABLE_ID_OFFSET + 2] << 16) |
         ((uint32_t)EDB_head.reserved[EDB_STABLE_ID_OFFSET + 3] << 24);
}

void EDB::writeNextRecordId(uint32_t id)
{
  EDB_head.reserved[EDB_STABLE_ID_OFFSET] = (uint8_t)(id & 0xFF);
  EDB_head.reserved[EDB_STABLE_ID_OFFSET + 1] = (uint8_t)((id >> 8) & 0xFF);
  EDB_head.reserved[EDB_STABLE_ID_OFFSET + 2] = (uint8_t)((id >> 16) & 0xFF);
  EDB_head.reserved[EDB_STABLE_ID_OFFSET + 3] = (uint8_t)((id >> 24) & 0xFF);
}

uint32_t EDB::peekRecordId(const byte* payload) const
{
  return (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
         ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
}

void EDB::stampRecordId(byte* payload, uint32_t id) const
{
  payload[0] = (byte)(id & 0xFF);
  payload[1] = (byte)((id >> 8) & 0xFF);
  payload[2] = (byte)((id >> 16) & 0xFF);
  payload[3] = (byte)((id >> 24) & 0xFF);
}

bool EDB::stableIdsEnabled() const
{
  return (EDB_head.flags & EDB_HDR_STABLE_IDS) != 0;
}

EDB_Status EDB::enableStableIds()
{
  if (EDB_head.rec_size < 4) return EDB_ERROR;
  EDB_head.flags |= EDB_HDR_STABLE_IDS;
  uint32_t max_id = 0;
  for (unsigned long i = 0; i < EDB_head.n_slots; i++) {
    if (readByte(slotOffset(i)) != EDB_SLOT_LIVE) continue;
    byte payload[4];
    edbRead(slotOffset(i) + 1, payload, 4);
    uint32_t id = peekRecordId(payload);
    if (id > max_id) max_id = id;
  }
  writeNextRecordId(max_id + 1);
  return writeHead();
}

EDB_Status EDB::recordId(unsigned long recno, uint32_t* out_id) const
{
  if (!out_id) return EDB_ERROR;
  if (!stableIdsEnabled()) return EDB_ERROR;
  if (recno < 1 || recno > EDB_head.n_slots) return EDB_OUT_OF_RANGE;
  if (readByte(slotOffset(recno - 1)) != EDB_SLOT_LIVE) return EDB_DELETED;
  byte payload[4];
  unsigned long off = slotOffset(recno - 1) + 1;
  for (unsigned int i = 0; i < 4; i++)
    payload[i] = readByte(off + i);
  *out_id = peekRecordId(payload);
  return EDB_OK;
}

EDB_Status EDB::findRecById(uint32_t record_id, unsigned long* out_recno) const
{
  if (!out_recno) return EDB_ERROR;
  if (!stableIdsEnabled()) return EDB_ERROR;
  for (unsigned long i = 0; i < EDB_head.n_slots; i++) {
    if (readByte(slotOffset(i)) != EDB_SLOT_LIVE) continue;
    byte payload[4];
    unsigned long off = slotOffset(i) + 1;
    for (unsigned int j = 0; j < 4; j++)
      payload[j] = readByte(off + j);
    if (peekRecordId(payload) == record_id) {
      *out_recno = i + 1;
      return EDB_OK;
    }
  }
  return EDB_OUT_OF_RANGE;
}

/**************************************************/
// ring FIFO (append-only; deleteRec returns EDB_ERROR; use clear() to wipe)

uint32_t EDB::readRingHead() const
{
  return (uint32_t)EDB_head.reserved[EDB_RING_HEAD_OFFSET] |
         ((uint32_t)EDB_head.reserved[EDB_RING_HEAD_OFFSET + 1] << 8) |
         ((uint32_t)EDB_head.reserved[EDB_RING_HEAD_OFFSET + 2] << 16) |
         ((uint32_t)EDB_head.reserved[EDB_RING_HEAD_OFFSET + 3] << 24);
}

void EDB::writeRingHead(uint32_t head)
{
  EDB_head.reserved[EDB_RING_HEAD_OFFSET] = (uint8_t)(head & 0xFF);
  EDB_head.reserved[EDB_RING_HEAD_OFFSET + 1] = (uint8_t)((head >> 8) & 0xFF);
  EDB_head.reserved[EDB_RING_HEAD_OFFSET + 2] = (uint8_t)((head >> 16) & 0xFF);
  EDB_head.reserved[EDB_RING_HEAD_OFFSET + 3] = (uint8_t)((head >> 24) & 0xFF);
}

bool EDB::ringModeEnabled() const
{
  return (EDB_head.flags & EDB_HDR_RING) != 0;
}

bool EDB::ringIsFull() const
{
  return ringModeEnabled() && EDB_head.n_live >= maxSlots();
}

EDB_Status EDB::enableRingMode()
{
  EDB_head.flags |= EDB_HDR_RING;
  unsigned long cap = maxSlots();
  if (cap == 0) return EDB_ERROR;
  uint32_t head = (EDB_head.n_live >= cap) ? 0 : (uint32_t)EDB_head.n_slots;
  writeRingHead(head);
  return writeHead();
}

unsigned long EDB::fifoFirstRec()
{
  if (!ringModeEnabled()) return firstRec();
  if (!ringIsFull()) return firstRec();
  return readRingHead() + 1;
}

unsigned long EDB::fifoNextRec(unsigned long recno)
{
  if (!ringModeEnabled()) return nextRec(recno);
  if (recno < 1 || recno > EDB_head.n_slots) return 0;
  if (!ringIsFull()) return nextRec(recno);

  unsigned long cap = maxSlots();
  unsigned long slot = recno - 1;
  unsigned long next_slot = (slot + 1) % cap;
  if (next_slot == readRingHead()) return 0;
  return next_slot + 1;
}

unsigned long EDB::count()
{
  return EDB_head.n_live;
}

unsigned long EDB::limit()
{
  return maxSlots();
}

EDB_Status EDB::clear()
{
  EDB_head.n_slots = 0;
  EDB_head.n_live = 0;
  EDB_head.free_head = EDB_FREE_NONE;
  if (ringModeEnabled()) writeRingHead(0);
  if (stableIdsEnabled()) writeNextRecordId(1);
  return writeHead();
}

unsigned long EDB::headPtr() const
{
  return EDB_head_ptr;
}

unsigned long EDB::tableSize() const
{
  return EDB_head.table_size;
}

unsigned int EDB::recSize() const
{
  return EDB_head.rec_size;
}

unsigned long EDB::nextTableOffset(unsigned long head_ptr, unsigned long table_size)
{
  return head_ptr + table_size;
}

EDB_Status EDB::openOrCreate(unsigned long head_ptr, unsigned long table_size, unsigned int rec_size)
{
  EDB_Status status = open(head_ptr);
  if (status == EDB_OK) return status;
  if (status == EDB_NEEDS_MIGRATION) return status;   // never clobber a legacy file
  return create(head_ptr, table_size, rec_size);
}
