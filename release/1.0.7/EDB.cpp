/*
  EDB.cpp
  Extended Database Library for Arduino
  Release 1.0.7 — drop-in safety fixes for 1.0.6 users
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

void* EDB::edbMalloc(unsigned int size)
{
#ifdef EDB_TEST
  if (_edb_malloc_fail) return NULL;
#endif
  return malloc(size);
}

void EDB::writeHead()
{
  edbWrite(EDB_head_ptr, EDB_REC EDB_head, (unsigned int)sizeof(EDB_Header));
}

EDB_Status EDB::readHead()
{
  edbRead(EDB_head_ptr, EDB_REC EDB_head, (unsigned int)sizeof(EDB_Header));
  return validateHeader();
}

EDB_Status EDB::validateHeader() const
{
  if (EDB_head.flag != EDB_FLAG) return EDB_ERROR;
  if (EDB_head.rec_size == 0) return EDB_ERROR;
  if (EDB_head.table_size < sizeof(EDB_Header)) return EDB_ERROR;
  if ((EDB_head.table_size - sizeof(EDB_Header)) < EDB_head.rec_size) return EDB_ERROR;
  if (EDB_head.n_recs > limit()) return EDB_ERROR;
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

EDB::EDB(EDB_Write_Handler *w, EDB_Read_Handler *r)
{
  _write_byte = w;
  _read_byte = r;
  _write_buffer = NULL;
  _read_buffer = NULL;
}

EDB::EDB(EDB_Write_Buffer *w, EDB_Read_Buffer *r)
{
  _write_byte = NULL;
  _read_byte = NULL;
  _write_buffer = w;
  _read_buffer = r;
}

EDB_Status EDB::create(unsigned long head_ptr, unsigned long tablesize, unsigned int recsize)
{
  if (recsize == 0) return EDB_ERROR;
  if (tablesize < sizeof(EDB_Header)) return EDB_ERROR;
  if ((tablesize - sizeof(EDB_Header)) < recsize) return EDB_ERROR;

  EDB_head_ptr = head_ptr;
  EDB_table_ptr = sizeof(EDB_Header) + EDB_head_ptr;
  EDB_head.flag = EDB_FLAG;
  EDB_head.n_recs = 0;
  EDB_head.rec_size = recsize;
  EDB_head.table_size = tablesize;
  writeHead();

  EDB_Header verify;
  edbRead(EDB_head_ptr, EDB_REC verify, (unsigned int)sizeof(EDB_Header));
  if (verify.flag != EDB_FLAG) return EDB_ERROR;
  if (verify.n_recs != 0 || verify.rec_size != recsize || verify.table_size != tablesize) return EDB_ERROR;

  return EDB_OK;
}

EDB_Status EDB::open(unsigned long head_ptr)
{
  EDB_head_ptr = head_ptr;
  EDB_table_ptr = sizeof(EDB_Header) + EDB_head_ptr;
  return readHead();
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
  if (count() == limit()) return EDB_TABLE_FULL;
  if (count() == 0) {
    if (recno != 1) return EDB_OUT_OF_RANGE;
    return appendRec((EDB_Rec)rec);
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
  return writeRec(recno, rec);
}

EDB_Status EDB::appendRec(EDB_Rec rec)
{
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
  if (EDB_head.table_size < sizeof(EDB_Header)) return 0;
  return (EDB_head.table_size - sizeof(EDB_Header)) / EDB_head.rec_size;
}

void EDB::clear()
{
  if (readHead() != EDB_OK) return;
  create(EDB_head_ptr, EDB_head.table_size, EDB_head.rec_size);
}
