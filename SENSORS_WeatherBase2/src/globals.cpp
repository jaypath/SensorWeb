#include <globals.hpp>

//initialize variables



time_t ALIVESINCE = 0;


//server
String WEBHTML;
WiFi_type WIFI_INFO;



//sensor
SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!

//screen
Screen I; //here, I is of type Screen (struct)


//weather
WeatherInfo WeatherData;
uint32_t LAST_BAR_READ=0,LAST_BAT_READ=0;
double batteryArray[48] = {0};
double LAST_BAR=0;
uint32_t LAST_WEB_REQUEST = 0;



LGFX tft;            // declare display variable

// Variables for touch x,y
int32_t touch_x,touch_y;
