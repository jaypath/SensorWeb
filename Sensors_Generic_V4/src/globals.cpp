#include <globals.hpp>
#include "Devices.hpp"


#ifdef _USEWEATHER
#include "Weather_Optimized.hpp"
//weather
WeatherInfoOptimized WeatherData;  // Optimized weather class

#endif
//initialize variables

//screen
STRUCT_CORE I; //here, I is of a struct with core variables


STRUCT_PrefsH Prefs;

#ifdef _USETFT
LGFX tft;            // declare display variable

// Variables for touch x,y
int32_t touch_x,touch_y;
#endif

uint32_t LAST_WEB_REQUEST = 0;





