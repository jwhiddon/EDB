This is a re-implementation of the database library originally written by Madhusudana das found here:

http://www.arduino.cc/playground/Code/DatabaseLibrary

This version increases the maximum number of records allowed in a database from 256 records (byte) to a theoretical maximum of 4,294,967,295 records (unsigned long). The maximum record size was also increased from 256 bytes (byte) to 65,534 bytes (unsigned int).

With these changes, it is now possible to use this library in conjunction with the standard Arduino EEPROM library, an external EEPROM such as the AT24C1024, providing 128 - 512 kilobytes of non-volatile storage, or any other platform that supports byte level reading and writing such as an SD card.

Extended Database Library Project Home at the Arduino Playground can be found here:

http://playground.arduino.cc/Code/ExtendedDatabaseLibrary

Examples included:

Arduino EEPROM Library providing 512-4096 bytes of storage
AT24C1024 I2C EEPROM Library providing 128-512 kilobytes of storage
Installing

Unzip the download into your Arduino-00xx/hardware/libraries directory. If the Arduino IDE is already running then exit and restart the Arduino IDE.

Quickstart

How to use in a nutshell:

include EDB.h
define a structure for your records
include an I/O interface such as EEPROM.h
declare an instance of EDB
pick an address in EEPROM for the table to start
