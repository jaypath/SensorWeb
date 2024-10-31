#ifndef SDCard_HPP
#define SDCard_HPP

#include <SD.h>
#include <utility.hpp>
#include <globals.hpp>



bool writeSensorSD(struct SensorVal &S, String filename);

#endif