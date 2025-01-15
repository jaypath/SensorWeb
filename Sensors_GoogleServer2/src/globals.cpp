#include <globals.hpp>
//initialize variables



time_t t=0;
byte OldTime[5] = {0,0,0,0,0};
int Ypos = 0;

String GsheetID= ""; //file ID for this month's spreadsheet
String GsheetName= ""; //file name for this month's spreadsheet
String lastError;
byte clockHeight = 0;

    

ScreenFlags::ScreenFlags() {}; //this constructs screenflags in advance, so can be used in union later


//weather
weathertype WeatherData;

//sensor
SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!

//Screen
ScreenFlags ScreenInfo;

//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif



LGFX tft;            // declare display variable

