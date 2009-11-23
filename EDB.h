/*
  EDB.h
  Extended Database Library for Arduino

  http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary

  Based on code from:
  Database library for Arduino 
  Written by Madhusudana das
  http://www.arduino.cc/playground/Code/DatabaseLibrary

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef EDB_PROM
#define EDB_PROM

struct EDB_Header
{
  unsigned long n_recs;
  unsigned int rec_size;
  unsigned long table_size;
};

typedef enum EDB_Status { 
                          EDB_OK,
                          EDB_OUT_OF_RANGE,
                          EDB_TABLE_FULL
                        };

typedef byte* EDB_Rec;

#define EDB_REC (byte*)(void*)&

class EDB {
  public:
    typedef void EDB_Write_Handler(unsigned long, const uint8_t);
    typedef uint8_t EDB_Read_Handler(unsigned long);
    EDB(EDB_Write_Handler *, EDB_Read_Handler *);
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
    EDB_Header EDB_head;
    void edbWrite(unsigned long ee, const byte* p, unsigned int);
    void edbRead(unsigned long ee, byte* p, unsigned int);
    void writeHead();
    void readHead();
    EDB_Status writeRec(unsigned long, const EDB_Rec);
};

extern EDB edb;

#endif
