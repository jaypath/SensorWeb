#ifndef GRAPHICS_HPP
    #define GRAPHICS_HPP

    #include "globals.hpp"
    #include "timesetup.hpp"
    #include "utility.hpp"
    #include "SDCard.hpp"
    #include "server.hpp"

extern String GsheetID; //file ID for this month's spreadsheet
extern String GsheetName; //file name for this month's spreadsheet

extern const uint8_t temp_colors[104];

extern LGFX tft;            // declare display variable

// Variables for touch x,y
extern weathertype WeatherData;
extern SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
extern struct ScreenFlags ScreenInfo;

extern byte OldTime[5];

void drawScreen();

void fcnDrawClock();
void fcnDrawWeather();
void fcnDrawHeader();
void fcnDrawCurrentCondition();
void fcnDrawSensors(int Y);
int fcn_write_sensor_data(byte i, int y);
void fcnDrawSettings();
void fcnDrawList();
void drawButton(String b1, String b2,  String b3,  String b4,  String b5,  String b6);

void clearTFT();
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg);
void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg);
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x=-1, int y=-1);
void fcnPrintTxtHeatingCooling(int X,int Y) ;

void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1, String headstr="", String tailstr="");
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x=-1,int y=-1, String headstr="", String tailstr="");

uint16_t temp2color(int temp, bool autoadjustluminance);
void drawBox(byte sensorInd, int X, int Y, byte boxsize_x,byte boxsize_y);
uint16_t set_color(byte r, byte g, byte b);
void color2RGB(uint32_t color,byte* R, byte* G, byte *B, bool is24bit);
void drawBmp(const char *filename, int16_t x, int16_t y, int32_t transparent = 0xFFFF);
uint16_t temp2color(int temp, bool autoadjustluminance = false);
uint8_t color2luminance(uint32_t color, bool is16bit=true);


uint32_t setFont(uint8_t FNTSZ);

#endif