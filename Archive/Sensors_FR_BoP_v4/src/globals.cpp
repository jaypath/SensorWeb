#include <globals.hpp>

#ifdef _USEWEATHER
#include "Weather_Optimized.hpp"
//weather
WeatherInfoOptimized WeatherData;  // Optimized weather class

#endif

#include "Devices.hpp"

// Global instance
Devices_Sensors Sensors;


//initialize variables

//screen
STRUCT_CORE I; //here, I is of type Screen (struct)


STRUCT_PrefsH Prefs;


#ifndef _ISPERIPHERAL
uint32_t LAST_BAR_READ=0,LAST_BAT_READ=0;
double batteryArray[48] = {0};
double LAST_BAR=0;
#endif

extern String WEBHTML;


#ifdef _USETFT
#include "graphics.hpp"

//extern LGFX tft;            // declare display variable

// Variables for touch x,y
int32_t touch_x,touch_y;
#endif

