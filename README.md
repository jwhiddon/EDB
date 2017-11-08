# Arduino Extended Database Library
This Arduino Extended Database Library increases the maximum number of records allowed in a database from 256 records (byte) to a theoretical maximum of 4,294,967,295 records (unsigned long). The maximum record size was also increased from 256 bytes (byte) to 65,534 bytes (unsigned int).

You may use this library in conjunction with the standard Arduino EEPROM library, an external EEPROM such as the AT24C1024, or any other platform that supports byte level reading and writing such as an SD card.

[Extended Database Library project's home at the Arduino Playground](http://playground.arduino.cc/Code/ExtendedDatabaseLibrary)

## Credits
This is a re-implementation of the [Arduino Database Library](http://playground.arduino.cc/Code/DatabaseLibrary) originally written by Madhusudana das.

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
* [SD Card example](https://github.com/jwhiddon/EDB/tree/master/examples/EDB_SDCARD)
* [SPIFFS example](https://github.com/jwhiddon/EDB/tree/master/examples/EDB_SPIFFS)

## Releases

### 1.0.6 - Nov 2, 2017
* Added a new buffer W/R handlers to increase speed in SPIFFS and SD Cards
* Added EDB_SPIFFS_Optimized example
* Added EDB_SDCARD_Optimized example
* Fixed a Warning about typedef in Compilation

### 1.0.5 - Oct 4, 2016
* Updated to support Arduino 1.6.12+

### 1.0.4 - Aug 6, 2016
* Updated library.properties to support all architectures

### 1.0.3 - Aug 6, 2016
* Added EDB_SPIFFS example
* Added EDB_SDCARD example
* Added EDB_ERROR to EDB_Status and a byte flag at the beginning of database header to allow the user to check if the database has been successfully created and is readable

### 1.0.2 - Feb 12, 2016
* Update filename extensions in example sketches
* More Arduino IDE related files

### 1.0.0 - Feb 12, 2016
* Updated to support Arduino 1.0.0+
* Bug fix in EDB::open
* Moved project to github

### 0.7.0 - Dec 8, 2009
* Initial release
