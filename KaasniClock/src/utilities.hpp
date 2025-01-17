#ifndef UTILITY_HPP
#define UTILITY_HPP
#include <globals.hpp>
#include <timesetup.hpp>
#include "SDCard.hpp"


void fcnDrawClock();
void drawBmp(const char *filename, int16_t x, int16_t y,int32_t transparent);
void fcnDrawPic() ;
String breakString(String *inputstr,String token);


#endif