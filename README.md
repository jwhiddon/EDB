# Arduino Extended Database Library

This is a re-implementation of the [Arduino Database Library](http://playground.arduino.cc/Code/DatabaseLibrary) originally written by Madhusudana das.

This version increases the maximum number of records allowed in a database from 256 records (byte) to a theoretical maximum of 4,294,967,295 records (unsigned long). The maximum record size was also increased from 256 bytes (byte) to 65,534 bytes (unsigned int).

With these changes, it is now possible to use this library in conjunction with the standard Arduino EEPROM library, an external EEPROM such as the AT24C1024 providing 128 - 512 kilobytes of non-volatile storage, or any other platform that supports byte level reading and writing such as an SD card.

[Extended Database Library project's home at the Arduino Playground](http://playground.arduino.cc/Code/ExtendedDatabaseLibrary)

## Install

* Unzip the download into your Arduino libraries directory
* If the Arduino IDE is already running then exit and restart the Arduino IDE

## Getting Started

* Include EDB.h in your Arduino sketch
* Define the data structure for your records
* Include an I/O interface such as EEPROM.h
* Declare an instance of EDB in your Arduino sketch
* Pick an EEPROM address at which the table should start

## Examples

* [Simple Example using internal Arduino EEPROM](https://github.com/jwhiddon/arduino-edb/tree/master/examples/EDB_Simple)
* [Arduino EEPROM providing 4096 - 32768 bits of address space](https://github.com/jwhiddon/arduino-edb/tree/master/examples/EDB_Internal_EEPROM)
* [AT24C1024 I2C EEPROM providing 1,048,576 bits of address space](https://github.com/jwhiddon/arduino-edb/tree/master/examples/EDB_AT24C1024)
* [24XX512 EEPROM providing up to up to 4 Mbit of address space](https://github.com/jwhiddon/arduino-edb/tree/master/examples/EDB_24XX512)

## Releases

### 1.0.1 - Pending
* Updated to support Arduino 1.0.0+
* Bug fix in EDB::open
* Moved project to github

### 1.0.0 - Dec 8, 2009
* Initial release
