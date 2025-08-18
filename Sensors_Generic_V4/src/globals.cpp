#include "../../GLOBAL_LIBRARY/globals.hpp"
#include "../../GLOBAL_LIBRARY/Devices.hpp"


#ifdef _USEWEATHER
#include "Weather_Optimized.hpp"
//weather
WeatherInfoOptimized WeatherData;  // Optimized weather class

#endif
//initialize variables

//screen
// Globals now provided by GLOBAL_LIBRARY/globals.cpp

#ifdef _USETFT
LGFX tft;            // declare display variable

// Variables for touch x,y
int32_t touch_x,touch_y;
#endif

uint32_t LAST_WEB_REQUEST = 0;





