#ifndef GRAPHICS_HPP
#define GRAPHICS_HPP

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "globals.hpp"
#include "utility.hpp"
#include "Devices.hpp"
#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include <SDCard.hpp>


extern WeatherInfoOptimized WeatherData;
extern LGFX tft;
extern double LAST_BAR;
extern STRUCT_PrefsH Prefs;

// Graphics utility functions
uint16_t set_color(byte r, byte g, byte b);
uint32_t setFont(uint8_t FNTSZ);
uint16_t temp2color(int temp, bool invertgray = false);

// Drawing functions
void drawBmp(const char* filename, int16_t x, int16_t y, uint16_t alpha = TRANSPARENT_COLOR);
void drawBox(int16_t sensorIndex, int X, int Y, byte boxsize_x, byte boxsize_y);

// Text drawing functions
void fcnPrintTxtCenter(String msg, byte FNTSZ, int x = -1, int y = -1);
void fcnPrintTxtColor(int value, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtColor2(int value1, int value2, byte FNTSZ, int x = -1, int y = -1, bool autocontrast = false);
void fcnPrintTxtHeatingCooling(int x, int y);

// Screen drawing functions
void fcnDrawScreen();
void fcnDrawHeader();
void fcnDrawClock();
void fcnDrawWeather();
void fcnDrawSensors(int Y);
void fncDrawCurrentCondition();

// Weather text functions
void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg);
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg);

// WiFi keypad functions
void drawKeyPad4WiFi(uint32_t y, uint8_t keyPage, uint8_t WiFiSet);
bool isTouchKey(int16_t* keyval, uint8_t* keypage);

// Setup display functions
void displaySetupProgress(bool success = true);
void displayWiFiStatus(byte retries, bool success);
void displayOTAProgress(unsigned int progress, unsigned int total);
void displayOTAError(int error);

#endif 