#ifdef _ISPERIPHERAL
#ifdef _USELEDMATRIX
#ifndef LEDMATRIX_HPP
#define LEDMATRIX_HPP
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "Devices.hpp"
#include "globals.hpp"

void Matrix_Init(void);
uint32_t Matrix_Draw(bool allowInvert = false, const char* message = "");

extern Devices_Sensors Sensors;
extern STRUCT_CORE I;



#endif
#endif
#endif
