/*
  EDB.cpp
  Extended Database Library for Arduino
  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary
*/

#include "Arduino.h"
#include "EDB.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool edbComputeShiftBlockSize(unsigned long record_count, unsigned int rec_size, unsigned int& block_size)
{
  if (record_count == 0 || rec_size == 0) return false;
  unsigned long bytes = record_count * (unsigned long)rec_size;
  if (bytes > (unsigned long)UINT_MAX) return false;
  block_size = (unsigned int)bytes;
  return true;
}

#ifdef EDB_TEST
static bool _edb_malloc_fail = false;
void EDB::setMallocFail(bool fail) { _edb_malloc_fail = fail; }
#endif

/**************************************************/
// private functions

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

EDB_Status EDB::ensureWritable()
{
  if (_is_v2) return EDB_OK;
  if (_header_size == EDB_HEADER_V2_SIZE) {
    _is_v2 = true;
    EDB_table_ptr = EDB_head_ptr + EDB_HEADER_V2_SIZE;
    return EDB_OK;
  }
  return EDB_ERROR;
}

void EDB::writeHead()
{
  EDB_head.flag = EDB_FLAG;
  EDB_head.version = EDB_VERSION;
  _header_size = EDB_HEADER_V2_SIZE;
  _is_v2 = true;
  EDB_table_ptr = EDB_head_ptr + _header_size;
  edbWrite(EDB_head_ptr, EDB_REC EDB_head, _header_size);
}

EDB_Status EDB::readHead()
{
  byte flag = readByte(EDB_head_ptr);
  if (flag != EDB_FLAG) return EDB_ERROR;

  byte version = readByte(EDB_head_ptr + 1);
  if (version == EDB_VERSION) {
    _is_v2 = true;
    _header_size = EDB_HEADER_V2_SIZE;
    EDB_table_ptr = EDB_head_ptr + _header_size;
    edbRead(EDB_head_ptr, EDB_REC EDB_head, _header_size);
    return validateHeader();
  }

  _is_v2 = false;
  return readV1Header();
}

// Legacy v1: detect AVR (12-byte) or ESP32 (16-byte) padded layouts.
EDB_Status EDB::readV1Header()
{
  struct V1Layout {
    unsigned int header_size;
    unsigned int n_recs_offset;
    unsigned int rec_size_offset;
    unsigned int table_size_offset;
    bool table_size_16bit;
  };

  static const V1Layout layouts[] = {
    {12, 4, 8, 10, true},
    {16, 4, 8, 12, false}
  };

  for (unsigned int i = 0; i < 2; i++) {
    const V1Layout& layout = layouts[i];
    byte header[16];
    edbRead(EDB_head_ptr, header, layout.header_size);

    EDB_Header candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.flag = header[0];
    memcpy(&candidate.n_recs, header + layout.n_recs_offset, sizeof(unsigned long));
    memcpy(&candidate.rec_size, header + layout.rec_size_offset, sizeof(unsigned int));
    if (layout.table_size_16bit) {
      uint16_t table_size = 0;
      memcpy(&table_size, header + layout.table_size_offset, sizeof(uint16_t));
      candidate.table_size = table_size;
    } else {
      memcpy(&candidate.table_size, header + layout.table_size_offset, sizeof(unsigned long));
    }

    EDB_Header saved = EDB_head;
    unsigned int saved_header_size = _header_size;
    unsigned long saved_table_ptr = EDB_table_ptr;

    EDB_head = candidate;
    _header_size = layout.header_size;
    EDB_table_ptr = EDB_head_ptr + _header_size;

    if (validateHeader() == EDB_OK) {
      return EDB_OK;
    }

    EDB_head = saved;
    _header_size = saved_header_size;
    EDB_table_ptr = saved_table_ptr;
  }

  return EDB_ERROR;
}

EDB_Status EDB::validateHeader() const
{
  if (EDB_head.flag != EDB_FLAG) return EDB_ERROR;
  if (EDB_head.rec_size == 0) return EDB_ERROR;
  if (EDB_head.table_size < _header_size) return EDB_ERROR;
  if ((EDB_head.table_size - _header_size) < EDB_head.rec_size) return EDB_ERROR;

  unsigned long max_recs = (EDB_head.table_size - _header_size) / EDB_head.rec_size;
  if (EDB_head.n_recs > max_recs) return EDB_ERROR;

  return EDB_OK;
}

bool EDB::isValidRecno(unsigned long recno) const
{
  return recno >= 1 && recno <= EDB_head.n_recs;
}

unsigned long EDB::recordOffset(unsigned long recno) const
{
  return EDB_table_ptr + ((recno - 1) * EDB_head.rec_size);
}

/**************************************************/
// public functions

EDB::EDB(EDB_Write_Handler *w, EDB_Read_Handler *r)
{
  _write_byte = w;
  _read_byte = r;
  _write_buffer = NULL;
  _read_buffer = NULL;
  _header_size = EDB_HEADER_V2_SIZE;
  _is_v2 = true;
}

EDB::EDB(EDB_Write_Buffer *w, EDB_Read_Buffer *r)
{
  _write_byte = NULL;
  _read_byte = NULL;
  _write_buffer = w;
  _read_buffer = r;
  _header_size = EDB_HEADER_V2_SIZE;
  _is_v2 = true;
}

EDB_Status EDB::create(unsigned long head_ptr, unsigned long tablesize, unsigned int recsize)
{
  if (recsize == 0) return EDB_ERROR;
  if (tablesize < EDB_HEADER_V2_SIZE) return EDB_ERROR;
  if ((tablesize - EDB_HEADER_V2_SIZE) < recsize) return EDB_ERROR;

  EDB_head_ptr = head_ptr;
  _header_size = EDB_HEADER_V2_SIZE;
  _is_v2 = true;
  EDB_table_ptr = EDB_head_ptr + _header_size;
  EDB_head.flag = EDB_FLAG;
  EDB_head.version = EDB_VERSION;
  EDB_head.n_recs = 0;
  EDB_head.rec_size = recsize;
  EDB_head.table_size = tablesize;
  writeHead();

  EDB_Header verify;
  edbRead(EDB_head_ptr, EDB_REC verify, _header_size);
  if (verify.flag != EDB_FLAG || verify.version != EDB_VERSION) return EDB_ERROR;
  if (verify.n_recs != 0 || verify.rec_size != recsize || verify.table_size != tablesize) return EDB_ERROR;

  return EDB_OK;
}

EDB_Status EDB::open(unsigned long head_ptr)
{
  EDB_head_ptr = head_ptr;
  EDB_Status status = readHead();
  if (status != EDB_OK) return status;
  return EDB_OK;
}

EDB_Status EDB::writeRec(unsigned long recno, const EDB_Rec rec)
{
  edbWrite(recordOffset(recno), rec, EDB_head.rec_size);
  return EDB_OK;
}

EDB_Status EDB::readRec(unsigned long recno, EDB_Rec rec)
{
  if (!isValidRecno(recno)) return EDB_OUT_OF_RANGE;
  edbRead(recordOffset(recno), rec, EDB_head.rec_size);
  return EDB_OK;
}

EDB_Status EDB::deleteRec(unsigned long recno)
{
  if (!isValidRecno(recno)) return EDB_OUT_OF_RANGE;
  if (ensureWritable() != EDB_OK) return EDB_ERROR;

  unsigned long tail = EDB_head.n_recs - recno;
  if (tail > 0) {
    bool shifted = false;
    if (_read_buffer && _write_buffer) {
      unsigned int block_size = 0;
      if (edbComputeShiftBlockSize(tail, EDB_head.rec_size, block_size)) {
        EDB_Rec buf = (byte*)edbMalloc(block_size);
        if (!buf) return EDB_ERROR;
        _read_buffer(recordOffset(recno + 1), buf, block_size);
        _write_buffer(recordOffset(recno), buf, block_size);
        free(buf);
        shifted = true;
      }
    }
    if (!shifted) {
      EDB_Rec rec = (byte*)edbMalloc(EDB_head.rec_size);
      if (!rec) return EDB_ERROR;
      for (unsigned long i = recno + 1; i <= EDB_head.n_recs; i++) {
        EDB_Status status = readRec(i, rec);
        if (status != EDB_OK) { free(rec); return status; }
        status = writeRec(i - 1, rec);
        if (status != EDB_OK) { free(rec); return status; }
      }
      free(rec);
    }
  }

  EDB_head.n_recs--;
  writeHead();
  return EDB_OK;
}

EDB_Status EDB::insertRec(unsigned long recno, const EDB_Rec rec)
{
  if (ensureWritable() != EDB_OK) return EDB_ERROR;
  if (count() == limit()) return EDB_TABLE_FULL;
  if (count() == 0) {
    if (recno != 1) return EDB_OUT_OF_RANGE;
    return appendRec(rec);
  }
  if (recno < 1 || recno > EDB_head.n_recs) return EDB_OUT_OF_RANGE;

  unsigned long tail = EDB_head.n_recs - recno + 1;
  bool shifted = false;
  if (_read_buffer && _write_buffer) {
    unsigned int block_size = 0;
    if (edbComputeShiftBlockSize(tail, EDB_head.rec_size, block_size)) {
      EDB_Rec buf = (byte*)edbMalloc(block_size);
      if (!buf) return EDB_ERROR;
      _read_buffer(recordOffset(recno), buf, block_size);
      _write_buffer(recordOffset(recno + 1), buf, block_size);
      free(buf);
      shifted = true;
    }
  }
  if (!shifted) {
    EDB_Rec buf = (byte*)edbMalloc(EDB_head.rec_size);
    if (!buf) return EDB_ERROR;
    for (unsigned long i = EDB_head.n_recs; i >= recno; i--) {
      EDB_Status status = readRec(i, buf);
      if (status != EDB_OK) { free(buf); return status; }
      status = writeRec(i + 1, buf);
      if (status != EDB_OK) { free(buf); return status; }
    }
    free(buf);
  }

  EDB_Status status = writeRec(recno, rec);
  if (status != EDB_OK) return status;
  EDB_head.n_recs++;
  writeHead();
  return EDB_OK;
}

EDB_Status EDB::updateRec(unsigned long recno, const EDB_Rec rec)
{
  if (!isValidRecno(recno)) return EDB_OUT_OF_RANGE;
  if (ensureWritable() != EDB_OK) return EDB_ERROR;
  return writeRec(recno, rec);
}

EDB_Status EDB::appendRec(const EDB_Rec rec)
{
  if (ensureWritable() != EDB_OK) return EDB_ERROR;
  if (EDB_head.n_recs + 1 > limit()) return EDB_TABLE_FULL;
  EDB_head.n_recs++;
  EDB_Status status = writeRec(EDB_head.n_recs, rec);
  if (status != EDB_OK) {
    EDB_head.n_recs--;
    return status;
  }
  writeHead();
  return EDB_OK;
}

unsigned long EDB::count()
{
  return EDB_head.n_recs;
}

unsigned long EDB::limit()
{
  if (EDB_head.rec_size == 0) return 0;
  if (EDB_head.table_size < _header_size) return 0;
  return (EDB_head.table_size - _header_size) / EDB_head.rec_size;
}

EDB_Status EDB::clear()
{
  EDB_Status status = readHead();
  if (status != EDB_OK) return status;
  return create(EDB_head_ptr, EDB_head.table_size, EDB_head.rec_size);
}

unsigned long EDB::headPtr() const
{
  return EDB_head_ptr;
}

unsigned long EDB::tableSize() const
{
  return EDB_head.table_size;
}

unsigned long EDB::nextTableOffset(unsigned long head_ptr, unsigned long table_size)
{
  return head_ptr + table_size;
}

EDB_Status EDB::openOrCreate(unsigned long head_ptr, unsigned long table_size, unsigned int rec_size)
{
  EDB_Status status = open(head_ptr);
  if (status == EDB_OK) return status;
  return create(head_ptr, table_size, rec_size);
}
