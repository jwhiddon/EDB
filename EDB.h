/*
  EDB.h
  Extended Database Library for Arduino
  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary
*/

#ifndef EDB_PROM
#define EDB_PROM
#define EDB_FLAG B11011011

struct EDB_Header
{
  byte flag;
  unsigned long n_recs;
  unsigned int rec_size;
  unsigned long table_size;
};

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
    EDB_Status appendRec(EDB_Rec rec);
    unsigned long limit();
    unsigned long count();
    void clear();
  private:
    unsigned long EDB_head_ptr;
    unsigned long EDB_table_ptr;
    EDB_Write_Handler *_write_byte;
    EDB_Read_Handler *_read_byte;
    EDB_Write_Buffer *_write_buffer;
    EDB_Read_Buffer *_read_buffer;
    EDB_Header EDB_head;
    void edbWrite(unsigned long ee, const byte* p, unsigned int);
    void edbRead(unsigned long ee, byte* p, unsigned int);
    void writeHead();
    void readHead();
    EDB_Status writeRec(unsigned long, const EDB_Rec);
};

extern EDB edb;

#endif
