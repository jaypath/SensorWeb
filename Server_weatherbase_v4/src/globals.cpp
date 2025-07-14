#include <globals.hpp>

//initialize variables

//screen
Screen I; //here, I is of type Screen (struct)


//weather
// WEATHER OPTIMIZATION - Comment out the optimized line and uncomment the original line to revert
#ifdef WEATHER_OPTIMIZATION_ENABLED
    WeatherInfoOptimized WeatherData;  // Optimized weather class
#else
    // WeatherInfo WeatherData;  // Original weather class (commented out)
#endif

uint32_t LAST_BAR_READ=0,LAST_BAT_READ=0;
double batteryArray[48] = {0};
double LAST_BAR=0;
uint32_t LAST_WEB_REQUEST = 0;
STRUCT_PrefsH Prefs;


LGFX tft;            // declare display variable

// Variables for touch x,y
int32_t touch_x,touch_y;


#ifdef _DEBUG
uint16_t TESTRUN = 0;
uint32_t WTHRFAIL = 0;
#endif
