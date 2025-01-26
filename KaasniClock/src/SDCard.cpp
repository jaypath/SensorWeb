#include "SDCard.hpp"

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}


String getNthFileName(String dirName, uint8_t filenum) {
  int count = 0;
  File dir = SD.open(dirName);

  if (!dir) {
    return "ERROR -1"; // Return -1 to indicate an error - could not open dir
  }

  if (!dir.isDirectory()) {
    return "ERROR -2"; //Return -2 to indicate that the path is not a directory
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    if (!entry.isDirectory()) { // Count only files, not subdirectories
        if (count==filenum) {
            String st = entry.name();
            entry.close();
            dir.close();
            return st;
        }
      count++;
    }

    entry.close();
  }
  dir.close();
  return "";
}


int countFilesInDirectory(String dirName) {
  int count = 0;
  File dir = SD.open(dirName);

  if (!dir) {
    return -1; // Return -1 to indicate an error - could not open dir
  }

  if (!dir.isDirectory()) {
    return -2; //Return -2 to indicate that the path is not a directory
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    if (!entry.isDirectory()) { // Count only files, not subdirectories
      count++;
    }

    entry.close();
  }
  dir.close();
  return count;
}
