#include <globals.hpp>
#include "Devices.hpp"

#if defined(_USEWEATHER) || defined(_USEWEATHERLITE)
#include "Weather_Optimized.hpp"
WeatherInfoOptimized WeatherData;
#endif


//initialize variables

//screen
STRUCT_CORE I; //here, I is of type Screen (struct)


STRUCT_PrefsH Prefs;


#if _IS_SERVER_HUB
uint32_t LAST_BAR_READ=0,LAST_BAT_READ=0;
double batteryArray[48] = {0};
double LAST_BAR=0;
#endif

extern String WEBHTML;

#ifdef _USETFT
#if _IS_SERVER_HUB
    #include "graphics.hpp"
#else
    #include "Clock480X480.hpp"
#endif

#endif

