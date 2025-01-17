#ifndef SDCard_HPP
#define SDCard_HPP

#include "GLOBALS.hpp"
#include <SD.h>

int countFilesInDirectory(String dirName);
uint16_t read16(File &f);
uint32_t read32(File &f);
String getNthFileName(String dirName, uint8_t filenum);

#endif