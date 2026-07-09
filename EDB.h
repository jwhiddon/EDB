/*
  EDB.h
  Extended Database Library for Arduino
  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary
*/

#ifndef EDB_H
#define EDB_H

#ifndef EDB_PROM
#define EDB_PROM
#endif

#define EDB_FLAG 0xDB
#define EDB_VERSION 2
#define EDB_HEADER_V2_SIZE 12

#if defined(__GNUC__)
#define EDB_PACKED __attribute__((packed))
#else
#define EDB_PACKED
#endif

struct EDB_PACKED EDB_Header
{
  byte flag;
  byte version;
  unsigned long n_recs;
  unsigned int rec_size;
  unsigned long table_size;
};

#if defined(__cplusplus) && __cplusplus >= 201103L && !defined(EDB_TEST)
static_assert(sizeof(EDB_Header) == EDB_HEADER_V2_SIZE, "EDB_Header must be 12 bytes");
#endif

enum EDB_Status {
  EDB_OK,
  EDB_ERROR,
  EDB_OUT_OF_RANGE,
  EDB_TABLE_FULL
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
    unsigned long EDB_table_ptr;
    unsigned int _header_size;
    bool _is_v2;
    EDB_Write_Handler *_write_byte;
    EDB_Read_Handler *_read_byte;
    EDB_Write_Buffer *_write_buffer;
    EDB_Read_Buffer *_read_buffer;
    EDB_Header EDB_head;
    void edbWrite(unsigned long ee, const byte* p, unsigned int);
    void edbRead(unsigned long ee, byte* p, unsigned int);
    void writeHead();
    EDB_Status readHead();
    EDB_Status validateHeader() const;
    bool isValidRecno(unsigned long recno) const;
    unsigned long recordOffset(unsigned long recno) const;
    EDB_Status writeRec(unsigned long, const EDB_Rec);
    EDB_Status readV1Header();
    EDB_Status ensureWritable();
    byte readByte(unsigned long address) const;
    void* edbMalloc(unsigned int size);
};

#ifndef EDB_NO_GLOBAL
// Legacy 1.0.x header declared a global instance; sketches that use `EDB edb(...)`
// must still provide the definition. Multiple tables use separate EDB objects instead.
extern EDB edb;
#endif

#endif
