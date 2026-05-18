#include "globals.hpp"

#if defined(_USETFT) && !defined(_ISPERIPHERAL)

#include "graphics.hpp"
#include "BootSecure.hpp"
#include "utility.hpp"


//for drawing sensors. The maximum number of sensors on a screen is 48.
#define MAXALARMS 48
byte alarms[MAXALARMS] = {255};


struct STRUCT_GRAPHICS GRAPHICS;

LGFX tft;

extern WeatherInfoOptimized WeatherData;
extern double LAST_BAR;
extern STRUCT_PrefsH Prefs;
extern Devices_Sensors Sensors;


LGFX_Sprite Sprite180x180(&tft);


// Graphics utility functions
bool initGraphics() {


  Sprite180x180.setPsram(true);
  Sprite180x180.createSprite(180, 180);

  //check if the sprites were created successfully
  if (!Sprite180x180.getBuffer()) {
    shoutThis("Sprite creation failed", true, TFT_RED, 2, 1, true, 0, 0);
    delay(500);
    return false;
  }

  GRAPHICS.IconSet = 0;
  GRAPHICS.StatusFlags = 0;
  GRAPHICS.StatusFlagsChanged = 0;
  GRAPHICS.miscData = 0;
  GRAPHICS.IntervalHourlyWeatherDisplay = 2;
  GRAPHICS.touchX = 0;
  GRAPHICS.touchY = 0;
  GRAPHICS.alarmIndex = 0;
  GRAPHICS.Screen_Now = SCREEN_NONE;
  GRAPHICS.Screen_Next = SCREEN_MAIN;
  GRAPHICS.SubScreen_Now = -1; //this ensures a redraw
  GRAPHICS.SubScreen_Next = 0;

  // GRAPHICS_TIMERS: initScreenFlags is called after memset in deleteCoreStruct, which zeros
  // I entirely. Without this, Timer_*_RESET stay zero and get saved to SD, causing timers
  // to fail on every boot. Explicitly set defaults to match SECOND_TIMER_STRUCT.
  GRAPHICS.GRAPHICS_TIMERS.zeroTimers();
  for (byte i=0;i<MAXSCREENELEMENTS;i++)   GRAPHICS.SCREEN_DATA[i].initScreenElement();

  loadFunctionsForScreen();

  return true;
}

uint16_t set_color(byte r, byte g, byte b) {
  return tft.color565(r, g, b);
}

uint16_t invert_color(uint16_t color) {
  return tft.color565(255-color>>16, 255-color>>8, 255-color);
}

uint16_t convertColor565ToGrayscale(uint16_t color) {
  // 1. Extract the 5-bit red, 6-bit green, and 5-bit blue components.
  // The bit masks and shifts are based on the RGB565 format.
  uint8_t red_5bit = (color >> 11) & 0x1F;
  uint8_t green_6bit = (color >> 5) & 0x3F;
  uint8_t blue_5bit = color & 0x1F;

  // 2. Scale the components to 8-bit (0-255) for accurate calculation.
  uint8_t red_8bit = (red_5bit << 3) | (red_5bit >> 2);
  uint8_t green_8bit = (green_6bit << 2) | (green_6bit >> 4);
  uint8_t blue_8bit = (blue_5bit << 3) | (blue_5bit >> 2);
  
  // 3. Calculate the luminance (grayscale value) using a weighted average.
  // The ITU-R BT.601 standard weights are a common choice.
  // To avoid floating-point math, integer arithmetic with shifting is used.
  uint16_t luminance = (red_8bit * 77 + green_8bit * 150 + blue_8bit * 29) >> 8;
  // The constants are scaled from 0.299, 0.587, 0.114 by multiplying by 256.

  // 4. Scale the 8-bit luminance back to 5-bit and 6-bit values.
  uint8_t gray_5bit = luminance >> 3; // For red and blue
  uint8_t gray_6bit = luminance >> 2; // For green

  // 5. Combine the grayscale components back into a 16-bit RGB565 color.
  return (gray_5bit << 11) | (gray_6bit << 5) | gray_5bit;
}


uint32_t setFont(uint8_t FNTSZ) {
  switch (FNTSZ) {
    case 0:
      tft.setFont(&fonts::TomThumb);      
    break;
    case 1:
      tft.setFont(&fonts::Font0);
    break;
    case 2:
      tft.setFont(&fonts::Font2);
    break;
    case 4:
      tft.setFont(&fonts::Font4);
    break;
    case 6:
      tft.setFont(&fonts::Font6);
    break;
    case 7:
      tft.setFont(&fonts::Font7);
    break;
    case 8:
      tft.setFont(&fonts::Font8);
    break;
    default:
      tft.setFont(&fonts::Font2);
    break;
  }
  return tft.fontHeight(tft.getFont());
}

uint16_t temp2color(int temp, bool invertgray) {
  const uint8_t temp_colors[104] = {
  20, 225, 100, 255, //20
  24, 225, 150, 250, //21 - 24
  27, 225, 200, 250, //25 - 27
  29, 250, 225, 250, //28 - 29
  32, 225, 250, 250, //30 - 32
  34, 100, 175, 250, //33 - 34
  37, 50, 175, 250, //35 - 37
  39, 0, 200, 250, //38 - 39
  42, 0, 225, 250, //40 - 42
  44, 0, 250, 250, //43 - 44
  47, 0, 250, 225, //45 - 47
  51, 0, 250, 200, //48 - 51
  54, 0, 250, 175, //52 - 54
  59, 0, 250, 150, //55 - 59
  64, 0, 250, 125, //60 - 64
  69, 0, 250, 100, //65 - 69
  72, 0, 250, 50, //70 - 72
  75, 0, 250, 0, //73 - 75
  79, 100, 250, 0, //76 - 79
  82, 200, 250, 0, //80 - 82
  84, 250, 250, 0, //83 - 84
  87, 250, 175, 0, //85 - 87
  89, 250, 100, 0, //88 - 89
  92, 250, 75, 0, //90 - 92
  94, 250, 50, 0, //93 - 94
  100, 250, 0, 0 //95 - 100
};
  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }

  if (invertgray) {
    int a = (255-(temp_colors[j+1] + temp_colors[j+2] + temp_colors[j+3])/3)%255;
    
    return tft.color565(byte(a), byte(a), byte(a));
  }

  return tft.color565(temp_colors[j+1],temp_colors[j+2],temp_colors[j+3]);
}

// Drawing functions

int8_t drawBmp(const char *filename, int16_t x, int16_t y, LGFX_Sprite *sprite) {
//return 1 if successful, 0 if bounds check failed, -1 if file not found, -2 if error with ps_malloc, -3 if error with rowBytes + outBytes > lineBufSize, -4 if error with file header

  // 1. Initial Bounds Check (Only for direct screen draw)
  if (!sprite && ((x >= tft.width()) || (y >= tft.height()))) return 0;
  else if (sprite && ((x >= sprite->width()) || (y >= sprite->height()))) return 0;

  File bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) {
    SerialPrint("File not found: ", false, 5);
    SerialPrint(filename, true, 5);
    return -1;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;

  const size_t lineBufSize = 1024 * 2; 
  uint8_t* lineBuffer = (uint8_t*)ps_malloc(lineBufSize);
  if (!lineBuffer) {
    bmpFile.close();
    return -2;
  }

  uint16_t signature = read16(bmpFile);
  if (signature == 0x4D42) {
    read32(bmpFile); // File size
    read32(bmpFile); // Reserved
    seekOffset = read32(bmpFile);
    read32(bmpFile); // Header size
    w = read32(bmpFile);
    h = read32(bmpFile);

    if (sprite && ((x + w > sprite->width()) || (y + h > sprite->height()))) {
      free(lineBuffer);
      bmpFile.close();
      return 0; //sprite cannot handle out of bounds
    }

    bool planes=true, bitDepth=true, nocompression=true; 
    if (read16(bmpFile) != 1) planes = false;
    if (read16(bmpFile) != 24) bitDepth = false;
    if (read32(bmpFile) != 0) nocompression = false;

    if (planes && bitDepth && nocompression) {
      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      size_t rowBytes = (size_t)w * 3 + padding;
      size_t outBytes = (size_t)w * 2;

      if (rowBytes + outBytes > lineBufSize) {
        free(lineBuffer);
        bmpFile.close();
        return -3; //error with rowBytes + outBytes > lineBufSize
      }

      // BMPs are stored bottom-to-top
      uint16_t currentY = y + h - 1;
      bmpFile.seek(seekOffset);

      uint8_t* row24 = lineBuffer;
      uint16_t* row16 = (uint16_t*)(lineBuffer + rowBytes);

      // pushImage expects RGB565 endian per destination; BMP rows are packed for swap-on for typical panels
      bool oldSwapBytes;
      if (sprite) {
        oldSwapBytes = sprite->getSwapBytes();
        sprite->setSwapBytes(true);
      } else {
        oldSwapBytes = tft.getSwapBytes();
        tft.setSwapBytes(true);
      }

      for (row = 0; row < h; row++) {
        if (bmpFile.read(row24, rowBytes) != (int)rowBytes) break;

        for (col = 0; col < w; col++) {
          b = row24[col * 3 + 0];
          g = row24[col * 3 + 1];
          r = row24[col * 3 + 2];
          // Convert to RGB565
          row16[col] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        if (sprite) {
          // Push to Sprite
          sprite->pushImage(x, currentY--, w, 1, row16);
        } else {
          // Push to Screen
          tft.pushImage(x, currentY--, w, 1, row16);
        }
      }
      if (sprite) sprite->setSwapBytes(oldSwapBytes);
      else tft.setSwapBytes(oldSwapBytes);
    } else {
      free(lineBuffer);
      bmpFile.close();
      if (!planes) storeError("BMP Error with planes", ERROR_BMP_PLANES, true);
      if (!bitDepth) storeError("BMP Error with bitDepth", ERROR_BMP_BITDEPTH, true);
      if (!nocompression) storeError("BMP Error with compression", ERROR_BMP_COMPRESSION, true);
      return -4; //error with file header

    }
  }
  bmpFile.close();
  free(lineBuffer);
  return 1;
}



void drawBox(int16_t sensorIndex, int X, int Y, byte boxsize_x,byte boxsize_y) {
  uint16_t box_border = set_color(200,175,200);
  uint16_t box_fill = set_color(75,50,75);
  uint16_t text_color = set_color(255,225,255);

  String box_text = "";
  char tempbuf[14];

  // Get sensor data - the sensor already contains its deviceIndex
  ArborysSnsType* sensor = Sensors.snsIndexToPointer(sensorIndex);
  
  if (!sensor || sensor->IsSet == 0) {
    return; // Invalid sensor
  }

  box_text = (String) sensor->snsName + (String) "_";

  if (bitRead(sensor->Flags,0)==1)   box_border = set_color(255,20,20); //generic border for flagged sensors

  if (sensor->expired) {
    box_text += "EXP!_";
    box_border = set_color(150,150,150);
    box_fill = set_color(200,200,200);
    text_color = set_color(55,55,55);
  } else {

    //get sensor vals 1, 4, 3, 70, 61... if any are flagged then set box color
    if (Sensors.isSensorOfType(sensor, "temperature")) {
      box_border = set_color(150,150,20);
      if (isnan(sensor->snsValue)) {
        box_text += (String) ("???_");
        box_fill = set_color(200,200,200);
        text_color = set_color(55,55,55);    
      } else {
        box_text += (String) ((int) sensor->snsValue) + "F_";
        
        if (bitRead(sensor->Flags,0)==1) { //flagged
          if (bitRead(sensor->Flags,5)==1) {        
            box_fill = set_color(255,100,100);
            box_border = set_color(255,20,20);
            text_color = convertColor565ToGrayscale(invert_color(box_fill));
          }
          else {
            box_fill = set_color(150,150,255);
            text_color = convertColor565ToGrayscale(invert_color(box_fill));
          }
        } else {
          box_fill = set_color(147, 235, 157);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      }
    }
    if (Sensors.isSensorOfType(sensor, "soil")) {
      //soil
      box_text += (String) ((int) sensor->snsValue) + "_";

      if (bitRead(sensor->Flags,0)==1) {
        if (bitRead(sensor->Flags,5)==1) { //too dry
          box_border = set_color(255,20,20);
          box_fill = set_color(250,170,100);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
        else { //too wet
          box_border = set_color(255, 15, 46);
          box_fill = set_color(11, 15, 46);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      } else { //just right
        box_border = set_color(100, 70, 20);
        box_fill = set_color(100, 70, 20);
        text_color = convertColor565ToGrayscale(invert_color(box_fill));
      }
    }

    if (Sensors.isSensorOfType(sensor, "humidity")) {
      //humidity
      box_text += (String) ((int) sensor->snsValue) + "%RH_";
      if (bitRead(sensor->Flags,0)==1) {
        if (bitRead(sensor->Flags,5)==0) { //too dry
          box_border = set_color(255,20,20);
          box_fill = set_color(205, 235, 250);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }        else { //too wet
          box_border = set_color(255,20,20);
          box_fill = set_color(70, 168, 179);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      } else {
        box_border = set_color(201, 216, 221);
        box_fill = set_color(155, 211, 229);
        text_color = convertColor565ToGrayscale(invert_color(box_fill));
      }
    }

    if (Sensors.isSensorOfType(sensor, "leak")) {
      //leak
      box_text += "LEAK_";
      box_border = set_color(255,20,20);
      box_fill = set_color(0,190,255);
      text_color = set_color(255-0,255-190,255-255);
    }

    if (Sensors.isSensorOfType(sensor, "bat")) {
      //bat
      box_text += (String) ((int) sensor->snsValue) + "%_";
      box_border = set_color(200,200,200);
      if (bitRead(sensor->Flags,0)==1) {        
        box_border = set_color(255,20,20);
        box_fill = set_color(228, 70, 110);
        text_color = set_color(0,0,0);
      } else {
        box_fill = set_color(150,150,255);
        text_color = set_color(0,0,0);
      }
    }


    if (Sensors.isSensorOfType(sensor, "pressure")) {
      //air pressure
      box_border = set_color(192, 222, 233);
      box_text += (String) ((int) sensor->snsValue) + "hPA_";
      if (bitRead(sensor->Flags,0)==1) {
        box_border = set_color(255,20,20);
        if (bitRead(sensor->Flags,5)==0) { //low pressure              
          box_fill = set_color(99, 151, 223);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        } 
        else {
          box_fill = set_color(214, 153, 152);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      } else {
        box_fill = set_color(152, 225, 189);
        text_color = convertColor565ToGrayscale(invert_color(box_fill));
      }
    }
  }

  //draw  box
  tft.fillRect(X,Y,boxsize_x-2,boxsize_y-2,box_fill);
  tft.drawRect(X,Y,boxsize_x-2,boxsize_y-2,box_border);
  
  Y+=  2;
      
  //print each line to the _, then print on the next line
  byte FNTSZ = 1;
  uint32_t FH = setFont(FNTSZ);
  int strOffset = -1;
  String tempstr;
  while (box_text.length()>1) {
    strOffset = box_text.indexOf("_", 0);
    if (strOffset == -1) { //did not find the break point
      tempstr = box_text;
      box_text = "";
    } else {
      tempstr = box_text.substring(0, strOffset);
      box_text.remove(0, strOffset + 1); //include the trailing "_"
    }

    if (tempstr.length()>=1) {
      //ensure tempstr is only 7 characters.
      if (tempstr.length() > 7) tempstr = tempstr.substring(0, 7);
      fcnPrintTxtCenter(tempstr,FNTSZ, X+boxsize_x/2,Y+FH/2,text_color,text_color,box_fill);
      Y+=2+FH;
    }
  } 
} 

// Text drawing functions
void fcnPrintTxtCenter(int msg, byte FNTSZ, int x, int y, uint16_t color1, uint16_t color2, uint16_t bgcolor, LGFX_Device* target) {
  String t = String(msg);
  fcnPrintTxtCenter(t, FNTSZ,  x,  y, color1,  color2, bgcolor, target);
}

void fcnPrintTxtCenter(String msg, byte FNTSZ, int x, int y, uint16_t color1, uint16_t color2, uint16_t bgcolor, LGFX_Device* target) {
  // Default to the global 'tft' object if no target is provided
  // Using LovyanGFX's base class allows this to work for both screen and sprites
  auto& canvas = (target != nullptr) ? *target : tft;

  int X, Y;
  // LovyanGFX setFont() is void — set font on canvas, then read metrics.
  switch (FNTSZ) {
    case 0:  canvas.setFont(&fonts::TomThumb);    break;
    case 1:  canvas.setFont(&fonts::Font0);    break;
    case 2:  canvas.setFont(&fonts::Font2);    break;
    case 4:  canvas.setFont(&fonts::Font4);    break;
    case 6:  canvas.setFont(&fonts::Font6);    break;
    case 7:  canvas.setFont(&fonts::Font7);    break;
    case 8:  canvas.setFont(&fonts::Font8);    break;
    default: canvas.setFont(&fonts::Font2);    break;
  }
  uint32_t FH = canvas.fontHeight(canvas.getFont());

  // Calculate width using the canvas's current font
  int textW = canvas.textWidth(msg);

  if (x >= 0 && y >= 0) {
      X = x - textW / 2;
      if (X < 0) X = 0;
      Y = y - (int)FH / 2;
      if (Y < 0) Y = 0;
  } else {
      X = x;
      Y = y;
  }

  canvas.setCursor(X, Y);

  if (color1 == color2) {
      canvas.setTextColor(color1, bgcolor);
      canvas.print(msg.c_str());
  } else {
      int slashIdx = msg.indexOf("/");
      if (slashIdx != -1) {
          canvas.setTextColor(color1, bgcolor);
          canvas.print(msg.substring(0, slashIdx).c_str());
          
          canvas.print("/");
          
          canvas.setTextColor(color2, bgcolor);
          canvas.print(msg.substring(slashIdx + 1).c_str());
      } else {
          // Fallback if no slash is found despite different colors
          canvas.setTextColor(color1, bgcolor);
          canvas.print(msg.c_str());
      }
  }

  canvas.setTextDatum(TL_DATUM);
}
/* old function that was restricted to tft, modified to work with sprite and tft now
void fcnPrintTxtCenter(int msg,byte FNTSZ, int x, int y, uint16_t color1, uint16_t color2, uint16_t bgcolor) {
  String t = (String) msg + "";
  fcnPrintTxtCenter(t, FNTSZ,  x,  y, color1,  color2, bgcolor);
}
void fcnPrintTxtCenter(String msg,byte FNTSZ, int x, int y, uint16_t color1, uint16_t color2, uint16_t bgcolor) {
  //if color1 and color2 are specified, then colorize the numbers before and after the slash
int X,Y;
uint32_t FH = setFont(FNTSZ);

if (x>=0 && y>=0) {
  X = x-tft.textWidth(msg)/2;
  if (X<0) X=0;
  Y = y-FH/2;
  if (Y<0) Y=0;
} else {
  X=x;
  Y=y;
}
tft.setCursor(X,Y);
if (color1==color2) {
  tft.print(msg.c_str());
} else {
  tft.setTextColor(color1,bgcolor);
  tft.print(msg.substring(0,msg.indexOf("/")).c_str());
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.print("/");
  tft.setTextColor(color2,bgcolor);
  tft.print(msg.substring(msg.indexOf("/")+1).c_str());
}


tft.setTextDatum(TL_DATUM);
    
return;
}
*/

void fcnPrintTxtHeatingCooling(int X,int Y) {

//print:
  //1,2,3
  //4,5,6
  //where the color of each number is orange for heating, blue for cooling, black for none, or orange on blue for both

  byte FNTSZ = 1;
  String temp;
  uint32_t FH = setFont(FNTSZ);


  uint16_t c_heat = tft.color565(255,180,0); //TFT_ORANGE;
  uint16_t c_ac = tft.color565(0,0,(byte) 255*.7);
  uint16_t c_fan = TFT_GREEN; //tft.color565(135,206,235); //TFT_SKYBLUE;135 206 235
  uint16_t c_errorfg = TFT_BLACK; 
  uint16_t c_errorbg = tft.color565(255,0,0); 
  uint16_t c_FG=TFT_DARKGRAY, c_BG=BG_COLOR;
  
  
  int x = X;
  tft.setTextDatum(TL_DATUM);
  for (byte j=1;j<7;j++) {
    c_FG=TFT_DARKGRAY;
    c_BG=BG_COLOR;
  
    if (j==4) {
      x=X;
      Y+= FH+2; 
    }

    if (bitRead(I.isHeat,j) && bitRead(I.isAC,j)) {
      c_FG = c_errorfg;
      c_BG = c_errorbg;
    } else {
      if (bitRead(I.isFan,j)) {
          c_BG = c_fan; 
      }
      if (bitRead(I.isHeat,j)) {
        c_FG = c_heat;        
      }
      if (bitRead(I.isAC,j)) {
        c_FG = c_ac; 
        if (c_BG != c_fan) c_BG = c_errorbg;
      }
    }
    switch (j) {
      case 1:
        temp = "OF ";
        break;
      case 2:
        temp = "MB ";
        break;
      case 3:
        temp = "LR ";
        break;
      case 4:
        temp = "BR ";
        break;
      case 5:
        temp = "FR ";
        break;
      case 6:
        temp = "DN ";
        break;
    }

    tft.setTextColor(c_FG,c_BG);        
    if (FNTSZ==0)    x +=tft.drawString(temp,x,Y,&fonts::TomThumb);
    if (FNTSZ==1)    x +=tft.drawString(temp,x,Y,&fonts::Font0);
  }
  
  temp = "XX XX XX "; //placeholder to find new X,Y location
  tft.setCursor(X + tft.textWidth(temp) ,Y-FH);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background

  return;
}


void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x,int y,bool autocontrast) {
  uint32_t FH = setFont(FNTSZ);
  uint16_t fg_color = TFT_WHITE;
  uint16_t bg_color = TFT_BLACK;
  
  
  if (x == -1) x = 0;
  if (y == -1) y = 0;
  
  tft.setCursor(x,y);
  
  
  if (autocontrast) {
    bg_color = temp2color(value1,true);  
  }
  

  tft.setTextColor(temp2color(value1),bg_color);
  tft.print(value1);

   fg_color = TFT_WHITE;
   bg_color = TFT_BLACK;
  tft.print("/");
  if (autocontrast) {
    bg_color = temp2color(value2,true);  
  }
  
  tft.setTextColor(temp2color(value2),bg_color);
  tft.print(value2);

   fg_color = TFT_WHITE;
   bg_color = TFT_BLACK;
  
  
}

void fcnPrintTxtColor(int value,byte FNTSZ,int x,int y,bool autocontrast) {
  uint32_t FH = setFont(FNTSZ);
  uint16_t fg_color = TFT_WHITE;
  uint16_t bg_color = TFT_BLACK;
  
  
  if (x == -1) x = 0;
  if (y == -1) y = 0;
  
  tft.setCursor(x,y);


  if (autocontrast) {
    bg_color = temp2color(value,true);  
  }
  

  tft.setTextColor(temp2color(value),bg_color);
  tft.print(value);

   fg_color = TFT_WHITE;
   bg_color = TFT_BLACK;

} 


byte fcnGetAlarms(int32_t whichSensors, byte rows, byte cols) {
  //fill an array with next row*cols alarm entries
//  byte alarms[rows*cols];


if (whichSensors == -1) whichSensors = I.showTheseFlags;

if (whichSensors != 0) {
  if ((whichSensors != I.showTheseFlags) && Sensors.countFlagged(0,0b10000011,0b10000011,0,true,false)==0) return 0; //no critical flagged sensors, don't bother checking for other flagged sensors

  if ((whichSensors == I.showTheseFlags) && Sensors.countFlagged(-1000,0b10000011,0b10000011,0,true,false)==0)     return 0; //no critical flagged sensors per I.showTheseFlags
}

byte alarmsToDisplay = rows*cols;
if (alarmsToDisplay>MAXALARMS) alarmsToDisplay = MAXALARMS;

//init alarms
  for (byte k = 0;k<(MAXALARMS);k++) {
    alarms[k] = 255;
  }
  
//fill up to alarmsToDisplay alarm spots, and remember where we left off. Also, fill each sensor type before moving to the next sensor type
  byte SensorIndex = GRAPHICS.alarmIndex;

  if (whichSensors == 0) bitSet(whichSensors, 11); //all sensor types and flag states are allowed if whichSensors is 0
  String sensorType[10] = {"","","","","","","","","",""};
  //populate sensorType based on whichSensors
  //note that if bit 11 is set, we still want to add each sensor type individually, as this creates the order of sensors to display. The ending all just adds all other sensors in no particular order.
    if (bitRead(whichSensors, 3) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[0] = "leak";
    if (bitRead(whichSensors, 2) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[1] = "soil";
    if (bitRead(whichSensors, 4) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[2] = "temperature";
    if (bitRead(whichSensors, 5) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[3] = "humidity";
    if (bitRead(whichSensors, 6) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[4] = "pressure";
    if (bitRead(whichSensors, 7) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[5] = "battery";
    if (bitRead(whichSensors, 8) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[6] = "HVAC";
    if (bitRead(whichSensors, 9) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[7] = "human";
    if (bitRead(whichSensors, 10) == 1 || bitRead(whichSensors, 11) == 1)   sensorType[8] = "distance";
    if (bitRead(whichSensors, 11) == 1) sensorType[9] = "all"; //this bit only set if whichSensors is 0
    
  //0 = leak, 1 = soil dry, 2 = temperature, 3 = humidity, 4 = pressure, 5 = battery, 6 = HVAC, 7 = human, 8 = distance, 9 = all others (if whichSensors is not 0), but in no particular order

  byte alarmArrayInd = 0;
  for (byte snstypeindex = 0; snstypeindex<10; snstypeindex++) {
    if (sensorType[snstypeindex] == "") continue; //skip if no sensor type
    // Visit SensorIndex before advancing. Calling cycleByteIndex in the while-condition advanced first,
    // which skipped sensor slot GRAPHICS.alarmIndex (always skipped sensor 0 when alarmIndex was 0).
    while (alarmArrayInd < alarmsToDisplay) {
      ArborysSnsType* sensor = Sensors.snsIndexToPointer(SensorIndex);
      if (sensor && sensor->IsSet != 0 && Sensors.isSensorOfType(sensor,sensorType[snstypeindex])) {
        bool isgood = true;

        if (bitRead(whichSensors,11)==0) {
          //this only applies if whichSensors was not 0
          if ((bitRead(whichSensors, 0) == 1) && (bitRead(sensor->Flags, 0) == 0)) isgood = false; //not flagged, exclude
          if ((bitRead(whichSensors, 1) == 1) && (sensor->expired) && (bitRead(sensor->Flags,7) == 1)) isgood = true; //expired and critical/monitored, so include
        }
        if (isgood && inArrayBytes(alarms,alarmsToDisplay,SensorIndex,false) == -1) {
          alarms[alarmArrayInd++] = SensorIndex;          //only include if not already in array
        }
      }
      if (!cycleByteIndex(SensorIndex,NUMSENSORS,GRAPHICS.alarmIndex)) break;
    }
  } 
  byte count = alarmArrayInd;   
  GRAPHICS.alarmIndex = SensorIndex;

  return count;
}

void fcnDrawSensors(int X,int Y, uint8_t rows, uint8_t cols) { //set whichsensors to -1 to show I.showTheseFlags
    /*
    going to show rowsxcols flagged sensors (so up to rows*cols)
    starting at X,Y
  
    returns the number of sensors to display
  
    note that the alarms array will be filled up to MAXALARMS sensors, but we may only be able to display rows*cols of these. In that case only fill alarms array up to rows*cols
  */
  
    int16_t init_X = X;
    if (rows==0) rows = 2;
    if (cols==0) cols = 6;
  
  

    
    byte boxsize_x=50,boxsize_y=50;
    byte gapx = 4;
    byte gapy = 2;
  
    String roomname;
  


  byte alarmArrayInd=0;
  for (byte r=0;r<rows;r++) {
    if (alarms[alarmArrayInd] == 255) break;
    for (byte c=0;c<cols;c++) {
      //draw each alarm index in a box
      if (alarms[alarmArrayInd] == 255) break;
      drawBox(alarms[alarmArrayInd++], X, Y, boxsize_x, boxsize_y);
        
      X=X+boxsize_x+gapx;
    }
    Y+=boxsize_y + gapy;
    X=init_X;
  }

  tft.setTextColor(FG_COLOR,BG_COLOR);

}



// Weather text functions
void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg) {
  // print air pressure
  double tempval;
  uint8_t snsIndex = findSensorByName("Outside", 13); //bme pressure
  if (snsIndex==255) snsIndex = findSensorByName("Outside", 9); //bmp pressure
  if (snsIndex==255) snsIndex = findSensorByName("Outside", 19); //bme680 pressure

  tempPres[0]=0;
  if (snsIndex !=255) {
    // Find the sensor by its index and get its value
    ArborysDevType* device = Sensors.getDeviceBySnsIndex(snsIndex);
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(snsIndex);
    
    if (device && sensor && device->IsSet && sensor->IsSet) {
      tempval = sensor->snsValue;
      if (isnan(tempval)) {
        snprintf(tempPres,10,"??? hPa");
        *fg=FG_COLOR;
        *bg=BG_COLOR;
        return;
      }
      if (tempval>LAST_BAR+.5) {
        snprintf(tempPres,10,"%dhPa^",(int) tempval);
        *fg=tft.color565(255,0,0);
        *bg=BG_COLOR;
      } else {
        if (tempval<LAST_BAR-.5) {
          snprintf(tempPres,10,"%dhPa_",(int) tempval);
          *fg=tft.color565(0,0,255);
          *bg=BG_COLOR;
        } else {
          snprintf(tempPres,10,"%dhPa-",(int) tempval);
          *fg=FG_COLOR;
          *bg=BG_COLOR;
        }
      }

      if (tempval>=1022) {
        *fg=tft.color565(255,0,0);
        *bg=tft.color565(255,255,255);
      }

      if (tempval<=1000) {
        *fg=tft.color565(0,0,255);
        *bg=tft.color565(255,0,0);
      }
    }
  }
  return;
}

void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg) {
  double tempval;
  uint8_t snsIndex = findSensorByName("Outside",12);
  if (snsIndex!=255) {
    // Find the sensor by its index and get its value
    
    ArborysDevType* device = Sensors.getDeviceBySnsIndex(snsIndex);
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(snsIndex);
    
    if (device && sensor && device->IsSet && sensor->IsSet) {
      tempval = (int) sensor->snsValue;
    } else {
      tempval = 0; //currently holds predicted weather
    }
  } else {
    tempval = 0; //currently holds predicted weather
  }

  if ((int) tempval!=0) {
    
    switch ((int) tempval) {
      case 3:
        snprintf(tempPred,10,"GALE");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,0);
        break;
      case 2:
        snprintf(tempPred,10,"WIND");
        *fg=tft.color565(255,0,0);*bg=BG_COLOR;
        break;
      case 1:
        snprintf(tempPred,10,"POOR");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case 0:
        snprintf(tempPred,10,"");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -1:
        snprintf(tempPred,10,"RAIN");
        *fg=FG_COLOR;*bg=BG_COLOR;
        break;
      case -2:
        snprintf(tempPred,10,"RAIN");
        *fg=tft.color565(0,0,255);*bg=BG_COLOR;

        break;
      case -3:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,255,255);
        *bg=tft.color565(0,255,255);

        break;
      case -4:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,255,0);
        *bg=tft.color565(0,255,255);
        break;
      case -5:
        snprintf(tempPred,10,"STRM");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,255,255);
        break;
      case -6:
        snprintf(tempPred,10,"GALE");
        *fg=tft.color565(255,255,0);
        *bg=tft.color565(0,0,255);
        break;
      case -7:
        snprintf(tempPred,10,"TSTRM");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,255);
        break;
      case -8:
        snprintf(tempPred,10,"BOMB");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,255);
        break;
    }
  } 

  return;
}



//START: CORE UPDATE FUNCTIONS
void updateGraphics() {
  bool newsecond = GRAPHICS.GRAPHICS_TIMERS.decimateTimers(I.currentSecond);
  if (newsecond && I.currentSecond == 0 && GRAPHICS.Screen_Now == SCREEN_MAIN) GRAPHICS.GRAPHICS_TIMERS.Timers[7] = 0; //force the clock to redraw.
  if (newsecond || GRAPHICS.GRAPHICS_TIMERS.Timers[0] == 0)     fcnDrawScreen(); //if the timers have decimated or a timer has reached zero, then draw the screen
  
  checkTouchScreen();
}



void fcnDrawScreen() {
  //fires every second, and updates the screen based on the current desired screen and subscreen

  if (GRAPHICS.GRAPHICS_TIMERS.Timers[0] == 0 && GRAPHICS.Screen_Next == GRAPHICS.Screen_Now && GRAPHICS.SubScreen_Next == GRAPHICS.SubScreen_Now)   fcnSwitchToNewScreen(SCREEN_MAIN);   
  

  // Load screen dispatch when the target screen changed, or when we have no draw function yet (e.g. both
  // Screen_* were set to SCREEN_MAIN on init — then Screen_Next == Screen_Now and this branch would skip).
  if (GRAPHICS.Screen_Next != GRAPHICS.Screen_Now || GRAPHICS.SCREEN_DATA[0].drawFunction == nullptr) {
    //set up the functions for the new screen
    loadFunctionsForScreen();
  }

  //execute the main draw function for the current screen (all other draw functions are called by the main screen function)
  if (GRAPHICS.SCREEN_DATA[0].drawFunction != nullptr) {
    GRAPHICS.SCREEN_DATA[0].drawFunction(0);
    GRAPHICS.Screen_Now = GRAPHICS.Screen_Next; 
  } else     loadFunctionsForScreen();

  //note that there are no subscreen specific actions to accomplish here, because they will be handled by the individual functions. So just update the subscreen index.
  //GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;

  return;
}

void loadFunctionsForScreen() {
  //load the appropriate drawing functions, timer defaults, and other required data for the new screen
  if (GRAPHICS.Screen_Next==SCREEN_MAIN )     GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawMainScreen, 0, 0, 320, 480, SCREEN_MAIN, 0, 0, 240, 0, nullptr);
  else if (GRAPHICS.Screen_Next==SCREEN_SENSORS )       GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawSensorScreen, 0, 0, 320, 480, SCREEN_SENSORS, 0, 0, 30, 0, nullptr);    
  else if (GRAPHICS.Screen_Next==SCREEN_ALERT )     GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawAlertScreen, 0, 0, 320, 480, SCREEN_ALERT, 0, 0, 30, 0, nullptr);
  //else if (GRAPHICS.Screen_Next==SCREEN_DAILY )     GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawDailyWeather, 0, 0, 320, 480, SCREEN_DAILY, 0, 0, 30, 0, nullptr);
  //else if (GRAPHICS.Screen_Next==SCREEN_HOURLY )     GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawHourlyWeather, 0, 0, 320, 480, SCREEN_HOURLY, 0, 0, 30, 0, nullptr);
  else if (GRAPHICS.Screen_Next==SCREEN_STATUS )     GRAPHICS.SCREEN_DATA[0].loadScreenElements(&fcnDrawStatusScreen, 0, 0, 320, 480, SCREEN_STATUS, 0, 0, 30, 0, nullptr);

  GRAPHICS.initRemainingScreenElements(1); //initialize all other screen elements. the main screen element is responsible for loading all other screen elements.

  //reset the timer for the main screen
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = GRAPHICS.SCREEN_DATA[0].Timer_RESET;
      //if the screen is changing, then reset the subscreen index
  GRAPHICS.SubScreen_Next = 0;//this indicates that this is a fresh screen load
  GRAPHICS.SubScreen_Now = -1;//this indicates that this is a fresh screen load


  return;
}

void fcnSwitchToNewScreen(SCREENS newScreen) {

GRAPHICS.Screen_Next = newScreen;
GRAPHICS.Screen_Now = SCREEN_NONE; //force a redraw
GRAPHICS.SubScreen_Next = 0;
GRAPHICS.SubScreen_Now = -1; //this forces a fresh screen load

return;
}

void fcnSwitchScreen(int16_t index) {
  if (index <0 || index >= MAXSCREENELEMENTS) return; //this is an error condition

  if (GRAPHICS.SCREEN_DATA[index].RunFcnOnTouch != nullptr && GRAPHICS.SCREEN_DATA[index].Screen_Next != SCREEN_NONE) {
    Serial.printf("Switching to screen %d\n", GRAPHICS.SCREEN_DATA[index].Screen_Next);

    fcnSwitchToNewScreen(GRAPHICS.SCREEN_DATA[index].Screen_Next);
  }

  return;
}

//END: CORE SCREEN DRAW FUNCTIONS

//helper functions --------------------------------------------------------
int8_t helper_findIndex(void (*drawFunction)(int16_t)) {
  for (byte i=0;i<8;i++) {
    if (GRAPHICS.SCREEN_DATA[i].drawFunction == drawFunction) {
      return i;
    }
  }
  return -1;
}

int16_t helper_runScreenElementFunction(int16_t index, bool failOnTimeLeft) {
  //checks for errors and returns the timer index if successful (if failontimeleft is true, then it will return -1 if the timer is not yet zero)

  int16_t status = 0;

  if (GRAPHICS.StatusFlagsChanged > 0 && GRAPHICS.Screen_Now != SCREEN_MAIN) status = -1;
  else if (index < 0)     status = -2;
  else if (index >= MAXSCREENELEMENTS) status = -3; //this is an error condition
  else if (GRAPHICS.SCREEN_DATA[index].drawFunction == nullptr) status = -4; //this is an error condition
  else if (GRAPHICS.GRAPHICS_TIMERS.Timers[GRAPHICS.SCREEN_DATA[index].TimerIndex] > 0 && failOnTimeLeft) status = -5; //this is an error condition
  else if (GRAPHICS.Screen_Next == SCREEN_NONE) status = -6; //this is an error condition
  else   status = GRAPHICS.SCREEN_DATA[index].TimerIndex;
  
  if (status < 0) {
    fcnSwitchToNewScreen(SCREEN_MAIN);    
  }

  return status;
}

void fcnDrawNavButtons(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition

  //clear the nav buttons area
  GRAPHICS.clearScreenArea(index);

  //draw the nav buttons
  uint16_t x = GRAPHICS.SCREEN_DATA[index].X;
  uint16_t y = GRAPHICS.SCREEN_DATA[index].Y;
  uint16_t width = GRAPHICS.SCREEN_DATA[index].W;
  uint16_t height = GRAPHICS.SCREEN_DATA[index].H;
  tft.fillRoundRect(x+2,y+2,width-4,height-4,10,TFT_LIGHTGREY);

  
  //note that there may be multiple yes options, in case a single screen has multiple confirmations (each being a different action/subscreen)
  switch (GRAPHICS.SCREEN_DATA[index].Local_Code) {
    case 100:
      fcnPrintTxtCenter("Yes",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 101:
      fcnPrintTxtCenter("No",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 102:
      fcnPrintTxtCenter("Abort",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 103:
      fcnPrintTxtCenter("Exit",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 104:
      fcnPrintTxtCenter("Next",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 105:
      fcnPrintTxtCenter("Prev",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 106:
      fcnPrintTxtCenter("Reboot",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 107:
      fcnPrintTxtCenter("Bdcst",2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 108:
      fcnPrintTxtCenter("Del Core",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 109:
      fcnPrintTxtCenter("Flush SD",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 110:
      fcnPrintTxtCenter("Del Sns",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 111:
      fcnPrintTxtCenter("WthrUpd",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY); //weather update
      break;
    case 120: //alternative yes
      fcnPrintTxtCenter("YES",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 121://alternative yes
      fcnPrintTxtCenter("YES",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    case 122://alternative yes
      fcnPrintTxtCenter("YES",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
    default:
      fcnPrintTxtCenter("MAIN",1, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
      break;
  }
}

void fcnSwitchSubScreen(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  GRAPHICS.SubScreen_Next = GRAPHICS.SCREEN_DATA[index].Local_Code;
  GRAPHICS.SubScreen_Now = -1; //this forces a fresh screen load
  return;
}



//END: helper functions --------------------------------------------------------

//alert screen draw functions

void fcnDrawAlertScreen(int16_t index) {
  //find my index in the SCREEN_DATA array
  int16_t timernum = helper_runScreenElementFunction(index, false); //second argument is false because we don't want to fail if the timer is not running
  if (timernum<0) return; //this is an error condition

  //if the timer ran out or flags changed then go to main
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] == 0) {
    fcnSwitchToNewScreen(SCREEN_MAIN);
    return;
  }

  if (GRAPHICS.SubScreen_Next != GRAPHICS.SubScreen_Now || GRAPHICS.SCREEN_DATA[1].drawFunction == nullptr) {
    //this screen only has one subscreen.
    if (GRAPHICS.SubScreen_Next == 0) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawAlertText, 0,0,320,420, SCREEN_MAIN, 0, 0, 30, 1, &fcnSwitchScreen); 
    else if (GRAPHICS.SubScreen_Next == 111) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusWeatherUpdate, 0,0,320,360, SCREEN_MAIN, 0, 0, 30, 1, &fcnSwitchScreen);

    //run the new screen elements
    GRAPHICS.SCREEN_DATA[1].drawFunction(1);

    GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;
    
    GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;  
  }
  
  
  return;
}

void fcnDrawAlertText(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition

  //clear the alert screen
  GRAPHICS.SCREEN_DATA[0].clearScreenElementArea();
  
  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen); //this could go in element 1 code, but since this is a simple screen I'll just keep here
  GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_NONE, 111, 0, 30, 3, &fcnSwitchSubScreen); //weather update

  //run the new screen elements
  GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  GRAPHICS.SCREEN_DATA[3].drawFunction(3);

  //draw the alert screen
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);

  String text;
  text.reserve(1000); 
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_RED,BG_COLOR);

  if (WeatherData.NumWeatherEvents == 0) {
    text = "No Weather Alerts\n";
    tft.drawString(text,0,0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    return;
  } else {

    //there is an alert, load the full details.
    WeatherEventFile wEF;
    wEF.getEvent(WeatherData.alertInfo.eventnumber);

    text = "Weather Alert \n" + (String) WeatherData.alertInfo.eventnumber + "/" + (String) WeatherData.NumWeatherEvents + "!! " + WeatherData.getAlertName(nullptr);
    tft.drawString(text,0,0);
    int16_t Y = tft.fontHeight(tft.getFont());

    //draw the 180x80 icon next to the text
    char tempbuf[45];
    snprintf(tempbuf,44,"/Icons/Events/180x180/%s.bmp",wEF.phenomenon);
    drawBmp(tempbuf,0,Y);
    
    text = "Severity: \n" + String(wEF.severity);
    tft.drawString(text,184,Y);

    text = "Urgency: \n" + String(wEF.urgency);
    tft.drawString(text,184,Y+4+tft.fontHeight(tft.getFont()));

    text = "Certainty: \n" + String(wEF.certainty);
    tft.drawString(text,184,Y+8+tft.fontHeight(tft.getFont())*2);
    
    Y=Y+180+4;

    setFont(1);
    text = (String) wEF.event + "\nRecvd: " + String(dateify(WeatherData.lastAlertFetchTime,"mm/dd/yyyy hh:nn:ss")) + "\n" + "Start: " + String(dateify(WeatherData.alertInfo.time_start,"mm/dd/yyyy hh:nn:ss")) + "\n" +
    "End: " + String(dateify(WeatherData.alertInfo.time_end,"mm/dd/yyyy hh:nn:ss")) + "\n" +
    "Description: " + String(wEF.description);
    tft.drawString(text,0,Y);

    setFont(2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
  
  
  return;
}

//-----------------------------------------------------------------------------


//main screen draw functions --------------------------------------------------------
void fcnDrawMainScreen(int16_t index) {
  //main screen draw function
  //find my index in the SCREEN_DATA array
  int16_t timernum = helper_runScreenElementFunction(index, false); //second argument is false because we don't care about the timer here
  if (timernum<0) return; //this is an error condition



  //check for flag changes every 5 seconds
  if (I.currentTime % 5 == 0) {
    //statusflags holds the flag values of the last cycle.
    //statusflagschanged will be calculated now, and determines is any flag change occurred
    //statusflags and statusflagschanged have the following bits:
    //bit 0 = global sensor flag (I.isFlagged). This is used for the header
    //bit 1 = weather event flag (I.WeatherEventFlags). This is used for the header
    //bit 2 = alarm flag (alarmcount is >0)

    if  (I.isFlagged>0) {
      if (isBit(GRAPHICS.StatusFlags,0)==false) setBit(GRAPHICS.StatusFlagsChanged,0); //set bit 0 to 1 if global sensor flag is set
      setBit(GRAPHICS.StatusFlags,0); //set bit 0 to 1 if global sensor flag is set
    }
    else {
      if (isBit(GRAPHICS.StatusFlags,0)==true) setBit(GRAPHICS.StatusFlagsChanged,0); //set bit 0 to 1 if global sensor flag is set
      clearBit(GRAPHICS.StatusFlags,0); //clear bit 0 if global sensor flag is not set
    }  //note that I don't check if the flag did NOT change, because there is no change to the bit set value. So if the flag is set, it will stay set.

    if (WeatherData.NumWeatherEvents>0) {
      if (isBit(GRAPHICS.StatusFlags,1)==false) setBit(GRAPHICS.StatusFlagsChanged,1); //set bit 1 to 1 if weather event flag is set
      setBit(GRAPHICS.StatusFlags,1); //set bit 1 to 1 if weather event flag is set
    }
    else {
      if (isBit(GRAPHICS.StatusFlags,1)==true) setBit(GRAPHICS.StatusFlagsChanged,1); //set bit 1 to 1 if weather event flag is set
      clearBit(GRAPHICS.StatusFlags,1); //clear bit 1 if weather event flag is not set
    }

    GRAPHICS.alarmCount = fcnGetAlarms(-1,3,3);
    if (GRAPHICS.alarmCount>0) {
      if (isBit(GRAPHICS.StatusFlags,2)==false) setBit(GRAPHICS.StatusFlagsChanged,2); //set bit 2 to 1 if alarm count is >0
      setBit(GRAPHICS.StatusFlags,2); //set bit 2 to 1 if alarm count is >0
    }
    else {
      if (isBit(GRAPHICS.StatusFlags,2)==true) setBit(GRAPHICS.StatusFlagsChanged,2); //set bit 2 to 1 if alarm count is >0
      clearBit(GRAPHICS.StatusFlags,2); //clear bit 2 if alarm count is not >0
    }
  }

  //for this screen, rerun all screen elements if main timer is zero or status flags changed
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] == 0 || GRAPHICS.StatusFlagsChanged > 0) {
    GRAPHICS.GRAPHICS_TIMERS.zeroTimers();
  }


  //skip HVAC check for now
  //  bool hvacchanged = (I.isAC != I.wasAC || I.isFan!=I.wasFan || I.isHeat != I.wasHeat);

  //there are no subscreens for the main screen. do nothing.

  // Repopulate tiles when subscreen changes, when the requested top-level screen changed, or when
  // child slots were never bound (e.g. first MAIN draw). Do not rely only on SCREEN_DATA[1]:
  // SubScreen_Now/Next can both be 0 after a prior layout, which would skip this block incorrectly.
  if (GRAPHICS.SubScreen_Next != GRAPHICS.SubScreen_Now || GRAPHICS.Screen_Next != GRAPHICS.Screen_Now
      || GRAPHICS.SCREEN_DATA[1].drawFunction == nullptr) {    
    GRAPHICS.clearScreenArea(0); //clear the screen
    GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawHeader, 0,0,160,30, SCREEN_NONE, 0, 1, 5, 1, nullptr); 
    GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawHeaderInfo, 160,0,160,30, SCREEN_NONE, 0, 2, 5, 2, nullptr); 
    GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawCurrentWeatherIconOrAlert, 0,32,180,180, SCREEN_SENSORS,0, 3, 5, 3, &fcnSwitchScreen); 
    GRAPHICS.SCREEN_DATA[4].loadScreenElements(&fcnDrawCurrentWeatherText, 180,32,140,180, SCREEN_ALERT, 0, 4, 90, 4, &fcnSwitchScreen);
    GRAPHICS.SCREEN_DATA[5].loadScreenElements(&fcnDrawHourlyWeather, 0,218,320,60, SCREEN_NONE,0, 5, 60, 5, nullptr);
    GRAPHICS.SCREEN_DATA[6].loadScreenElements(&fcnDrawDailyWeather, 0,285,320,100, SCREEN_DAILY,0, 6, 30, 6, nullptr);
    GRAPHICS.SCREEN_DATA[7].loadScreenElements(&fcnDrawClock, 0,390,320,90, SCREEN_STATUS,0, 7, 60, 7, &fcnSwitchScreen);
    GRAPHICS.GRAPHICS_TIMERS.zeroTimers();
    GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;
    GRAPHICS.Screen_Now = SCREEN_MAIN;
  }

  //redraw all screen elements that have run out of time
  for (byte i=1;i<MAXSCREENELEMENTS;i++) {
    if (GRAPHICS.SCREEN_DATA[i].drawFunction == nullptr) continue;
    if (GRAPHICS.GRAPHICS_TIMERS.Timers[GRAPHICS.SCREEN_DATA[i].TimerIndex] == 0) GRAPHICS.SCREEN_DATA[i].drawFunction(i);
  }

  //reset my timer if it is zero
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] == 0) GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  return;
}

//redraw header --------------------------------------------------------
void fcnDrawHeader(int16_t index) { 
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition

  if (GRAPHICS.StatusFlagsChanged > 0) GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = 0; //force a redraw of myself if status flags changed

  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum]  > 0) return; 

  //clear the header area
  GRAPHICS.clearScreenArea(index);
//  uint8_t HeaderFlags = 0; //bit 0 - showing date as ddd mm/dd, bit 1 = showing sensorflag ,  bit 2 = showing HVAC in header


  //draw header    
  String st = "";
  tft.setTextDatum(TL_DATUM);

  uint16_t x = GRAPHICS.SCREEN_DATA[index].X,y=GRAPHICS.SCREEN_DATA[index].Y;
  uint32_t FH = setFont(4);
  tft.setCursor(x,y);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  
  uint8_t HeaderFlags = GRAPHICS.SCREEN_DATA[index].Local_Code; //bit 0 - showing date as ddd mm/dd, bit 1 = showing flags ,  bit 2 = showing HVAC in header

  if (isBit(HeaderFlags,0))      st = dateify(I.currentTime,"dow mm/dd");
  else     st = dateify(I.currentTime,"mm/dd dow");
  
  //flip the bit 0 value in headerflags
  flipBit(HeaderFlags,0); //flip bit 0

  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;

  FH = setFont(2);

  
  clearBit(HeaderFlags,1); //clear bit 1 (flags)
  tft.setTextFont(2);
  if(isBit(GRAPHICS.StatusFlags,2)) { //alarm flag
    tft.setTextColor(TFT_BLACK,TFT_RED); //without second arg it is transparent background
    tft.drawString("ALARM",x,y+2);
    setBit(HeaderFlags,1); //set bit 1 to 1
  }
  else if (isBit(GRAPHICS.StatusFlags,0)) {
    tft.setTextColor(TFT_ORANGE,BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y+2);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
    //set bit 1 in headerflags
    setBit(HeaderFlags,1); //set bit 1 to 1
  }
  x += tft.textWidth("ALARM")+4;


  clearBit(HeaderFlags,2); //clear bit 2
  //check if I have an HVAC sensor
  if (Sensors.findSnsOfType("HVAC", false, -1) != -1) {
    //set bit 2 in headerflags
    setBit(HeaderFlags,2); //set bit 2 to 1
    setFont(0);
    fcnPrintTxtHeatingCooling(x,5);
    st = "XX XX XX "; //placeholder to find new X position
    x += tft.textWidth(st)+4;
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  }

  GRAPHICS.SCREEN_DATA[index].Local_Code = (int16_t) HeaderFlags; //save the header flags

  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);
  
  //provide the reset the draw timer.
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;
  
  return;
}

//redraw  header info --------------------------------------------------------
void fcnDrawHeaderInfo(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition

  if (GRAPHICS.StatusFlagsChanged > 0) GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = 0; //force a redraw of myself if status flags changed

  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum]  > 0) return; 

  GRAPHICS.StatusFlagsChanged = 0; //reset since we have checked all flags

  //clear the header info area
  GRAPHICS.clearScreenArea(index);
  //  uint8_t InfoFlags = 0; //bit 0 - showing wifi out, bit 1 =  showing weather alerts, bit 2 = last error msg within 2h, bit 3 = IP in header, bit 4 = dawn/dusk info


 
  String st = "";
  tft.setTextDatum(TL_DATUM);

  uint16_t x = GRAPHICS.SCREEN_DATA[index].X,y=GRAPHICS.SCREEN_DATA[index].Y;
  uint32_t FH = setFont(2);
  tft.setCursor(x,y);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background

  uint8_t InfoFlags = GRAPHICS.SCREEN_DATA[index].Local_Code; //bit 0 - showing wifi out, bit 1 =  showing weather alerts, bit 2 = last error msg within 2h, bit 3 = IP in header, bit 4 = dawn/dusk info


  //Order is: no wifi, then weather alerts, then cycle amongst [IP address, dawn/dusk info, last error msg within 2h]
  if(WiFi.status()!= WL_CONNECTED){  
    InfoFlags =0;
    bitSet(InfoFlags,0); //set bit 0 to 1
    setFont(2);
    st = "NO WIFI!";
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.drawString(st,x,y+4);
    x += tft.textWidth(st)+4;
  } else {
    clearBit(InfoFlags,0); //clear bit 0
    //check if there are weather alerts
    if (isBit(I.WeatherEventFlags,0)  && isBit(I.WeatherEventFlags,1)==false) {
      InfoFlags =0;
      setBit(InfoFlags,1); //set bit 1 to 1

      FH = setFont(1);
      //set text color to dark gray
      tft.setTextColor(TFT_RED,BG_COLOR);
      //draw the IP address
      st = "Wthr Alrt: " + String(WeatherData.alertInfo.phenomenon);
      tft.drawString(st,x,y+4);
      st = WeatherData.alertInfo.event;
      tft.drawString(st,x,y+FH + 2);

    } else {      
      //Check if there was an error message within the past 5 min
      FH = setFont(1);

      if (false && (I.lastErrorTime > 0 && I.lastErrorTime > I.currentTime - 300) && isBit(GRAPHICS.SCREEN_DATA[index].Local_Code,2)==false) {
        InfoFlags =0;
        setBit(InfoFlags,2); //set bit 2 to 1
        //set text color to dark gray
        tft.setTextColor(TFT_WHITE,BG_COLOR);        
        //draw the error message
        st = "Error: ";
        tft.drawString(st,x,y+2);
        st = String(I.lastError);
        tft.drawString(st,x,y+FH + 4);
      } else {
        tft.setTextColor(TFT_LIGHTGREY,BG_COLOR);

        //display my IP address if dawn/dusk info was showing
        if (isBit(InfoFlags,3)==false) {
          InfoFlags =0;
          setBit(InfoFlags,3); //set bit 3 to 1
          //show IP address
          FH = setFont(2);
          st = "My IP: " + Sensors.getMyDeviceIP().toString();
          tft.drawString(st,x,y+2);
    
        } else {
          //show dawn/dusk info
          FH = setFont(4);

          InfoFlags =0;
          setBit(InfoFlags,4); //set bit 4 to 1

          if (WeatherData.sunrise > I.currentTime) {          
            st = "Dawn: " + String(dateify(WeatherData.sunrise,"hh:nn"));
            tft.drawString(st,x,y+2);          
          } else {
            st = "Dusk: " + String(dateify(WeatherData.sunset,"hh:nn"));
            tft.drawString(st,x,y+2);          
          }
        }
      }
    }
  }

  GRAPHICS.SCREEN_DATA[index].Local_Code = (int16_t) InfoFlags; //save the info flags
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background    

  FH = setFont(2);


  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);
  //reset the draw timer.
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  

  return;
}


//redraw  clock --------------------------------------------------------
void fcnDrawClock(int16_t index) {  
  //this function does not need to check for timer expiration, because it is called only when the second is zero
  int16_t timernum = helper_runScreenElementFunction(index, true); 
  if (timernum<0) return; //this is an error condition other than timer expiration

  //clear the clock area
  GRAPHICS.clearScreenArea(index);

  //draw the clock
  int16_t X = GRAPHICS.SCREEN_DATA[index].X + GRAPHICS.SCREEN_DATA[index].W/2;
  int16_t Y = GRAPHICS.SCREEN_DATA[index].Y + GRAPHICS.SCREEN_DATA[index].H/2;
  uint8_t FNTSZ=8;
  uint32_t FH = setFont(FNTSZ);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  char tempbuf[20];
  snprintf(tempbuf,16,"%s",dateify(I.currentTime,"h1:nn"));
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y,FG_COLOR,FG_COLOR,BG_COLOR);

  //reset the clock timer
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  //special case for clock: reset the main screen timer as well (this prevents screen draws when all other functions are running)
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = GRAPHICS.SCREEN_DATA[0].Timer_RESET;


  return;
}
//-----------------------------------------------------------

//redraw  current weather text --------------------------------------------------------
void fcnDrawCurrentWeatherText(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, true); 
  if (timernum<0) return; //this is an error condition

  //clear the current weather text area, which is on screen 0, and is element 3
    GRAPHICS.clearScreenArea(index);

  int FNTSZ = 8;
  uint32_t FH = setFont(FNTSZ);
  int16_t X = GRAPHICS.SCREEN_DATA[index].X + GRAPHICS.SCREEN_DATA[index].W/2; //middle of the area
  int16_t Y = GRAPHICS.SCREEN_DATA[index].Y;

  String st = "";
  byte section_spacer = 3;

  //see if we have local weather and battery
  if (I.haveOutsideTemperatureSensor==true) {

    st = "Local:" ;
    //check for outside battery

    if (I.localBatteryIndex<255) {
      ArborysSnsType* sensor = Sensors.snsIndexToPointer(I.localBatteryIndex);
      if (sensor && sensor->IsSet && sensor->timeLogged + 7200>I.currentTime) {        
        st += " Bat" + (String) (Sensors.returnBatteryPercentage(sensor)) + "%";
      }
    }

    FH = setFont(1);
    tft.setCursor(X,Y);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(st,1,X,Y+FH/2);    
    Y=Y+FH+section_spacer;
  }
  

  // draw current temp
  FH = setFont(FNTSZ);
  tft.setTextColor(temp2color(I.currentOutsideTemp),BG_COLOR);
  fcnPrintTxtCenter(I.currentOutsideTemp,FNTSZ,X,Y+FH/2,temp2color(I.currentOutsideTemp),temp2color(I.currentOutsideTemp),BG_COLOR);
  Y=Y+FH+section_spacer;

  //print today max / min
    FNTSZ = 4;  
    FH = setFont(FNTSZ);
    int8_t tempMaxmin[2];
    WeatherData.getDailyTemp(0,tempMaxmin);
    if (isTempValid(tempMaxmin[0])==false) tempMaxmin[0] = WeatherData.getTemperature(I.currentTime);
    //does local max/min trump reported?
    if (tempMaxmin[0]<I.currentOutsideTemp) tempMaxmin[0]=I.currentOutsideTemp;
    if (tempMaxmin[1]>I.currentOutsideTemp) tempMaxmin[1]=I.currentOutsideTemp;
    I.Tmax = tempMaxmin[0];
    I.Tmin = tempMaxmin[1];

    fcnPrintTxtCenter((String) I.Tmax + "/" + I.Tmin,FNTSZ,X,Y+FH/2,temp2color(I.Tmax),temp2color(I.Tmin),BG_COLOR);  
    
    Y=Y+FH+section_spacer;

  //print pressure info
    uint16_t fg,bg;
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    char tempbuf[15]="";
    fcnPredictionTxt(tempbuf,&fg,&bg);
    if (tempbuf[0]!=0) {// prediction made
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+FH/2,fg,fg,bg);
      Y=Y+FH+section_spacer;
    }
    FNTSZ=2;    
    FH = setFont(FNTSZ);
    tempbuf[0]=0;
    fcnPressureTxt(tempbuf,&fg,&bg);

    if (tempbuf[0]!=0) {
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+FH/2,fg,fg,bg);
      Y=Y+FH+section_spacer;
    }
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    if (WeatherData.flag_snow) {
      snprintf(tempbuf,14,"Snow: %u%%",WeatherData.getDailyPoP(0));
      fg = tft.color565(100,100,255);
      bg = TFT_WHITE;
    }
    else {
      if (WeatherData.flag_ice) {
        snprintf(tempbuf,14,"Ice: %u%%",WeatherData.getDailyPoP(0));
        fg = tft.color565(255,100,100);;
        bg = TFT_WHITE;
      }
      else {
        snprintf(tempbuf,14,"Rain: %u%%",WeatherData.getDailyPoP(0));
        if (WeatherData.getDailyPoP(0)>30)  {
          fg = tft.color565(255,0,0);
          bg = tft.color565(0,0,255);
        }
        else {
          fg = FG_COLOR;
          bg = BG_COLOR;
        }
      }
    }

    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+2+FH/2,fg,fg,bg);

  //end current wthr
  I.lastCurrentOutsideTemp = I.currentOutsideTemp;

  //reset the timer
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  return;
}

//redraw current weather icon or alert --------------------------------------------------------
void fcnDrawCurrentWeatherIconOrAlert(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, true); 
  if (timernum<0) return; //this is an error condition

  //assign iconflags to the first 3 bits of the local code
  uint8_t IconFlags = GRAPHICS.SCREEN_DATA[index].Local_Code & 0b00000111; //bit 0 - showing icon, bit 1 = showing flags, bit 2 = showing alerts
  
  //assign skipcount to the second byte of the local code
  uint8_t skipcount = (GRAPHICS.SCREEN_DATA[index].Local_Code>>8) & 0xFF;
  uint8_t oldIconFlags = IconFlags;
  bool weatherAlerts = isBit(I.WeatherEventFlags, 0);


  if (isBit(IconFlags,0)) {
    if (GRAPHICS.alarmCount > 0) {
      IconFlags = 2; //was showing icon, now showing alerts
    }
    else {
      if (weatherAlerts) IconFlags = 4; //no flags, skip to alerts
      else IconFlags = 1; //no flags, no alerts, show icon
    }
  }
  else if (isBit(IconFlags,1)) {
    if (weatherAlerts)       IconFlags = 4; //was showing flags, now showing weather alerts
    else      IconFlags = 1; //was showing flags, now showing icon
  }  
  else {
    IconFlags = 1; //was showing alerts or there was an error, now showing flags
  }

  if (IconFlags == oldIconFlags && skipcount <12 && IconFlags == 1) {
    //do nothing, already showing what we need!
    skipcount++;
    GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;
    return;
  } else skipcount = 0;

    //clear the icon area, it must be redrawn now
    GRAPHICS.clearScreenArea(index);

  if (IconFlags == 1)     {
    fcnDrawWeatherSprite180(GRAPHICS.SCREEN_DATA[index].X,GRAPHICS.SCREEN_DATA[index].Y,Sprite180x180,false);   
    GRAPHICS.SCREEN_DATA[index].Screen_Next = SCREEN_SENSORS; //set the screen next to show sensors
  }
  else if (IconFlags == 2)  { //show alerts
        fcnDrawSensors(GRAPHICS.SCREEN_DATA[index].X,GRAPHICS.SCREEN_DATA[index].Y,3,3);            
        GRAPHICS.SCREEN_DATA[index].Screen_Next = SCREEN_SENSORS; //set the screen next to show alerts      
  }
  else if (IconFlags == 4) {
    fcnDrawWeatherSprite180(GRAPHICS.SCREEN_DATA[index].X,GRAPHICS.SCREEN_DATA[index].Y,Sprite180x180,true);   
    GRAPHICS.SCREEN_DATA[index].Screen_Next = SCREEN_ALERT; //set the screen next to show alerts
  }
  else  {
    IconFlags = 1; //was showing nothing, now showing icon
    fcnDrawWeatherSprite180(GRAPHICS.SCREEN_DATA[index].X,GRAPHICS.SCREEN_DATA[index].Y,Sprite180x180,false);   
    GRAPHICS.SCREEN_DATA[index].Screen_Next = SCREEN_SENSORS; //set the screen next to show sensors and reset the timer    
  }
  
  //reset the timer. If either alert flags or event flags are present, then the reset is 5 seconds, otherwise 30 seconds.
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  GRAPHICS.SCREEN_DATA[index].Local_Code = (int16_t) IconFlags | (skipcount<<8); //save the icon flags and skipcount

  return;
}


bool fcnDrawWeatherSprite180(uint16_t X, uint16_t Y, LGFX_Sprite &sprite, bool useAlerts) {
  //check if sprite needs to be redrawn, then push to screen

  bool refilled = false;
  if (sprite.getBuffer() == NULL) return false;

  refilled =fillSprite180(X,Y,sprite,useAlerts);    
  
  sprite.pushSprite(X,Y);
  GRAPHICS.SPRITE_180x180.X = X;
  GRAPHICS.SPRITE_180x180.Y = Y;
  GRAPHICS.SPRITE_180x180.W = 180;
  GRAPHICS.SPRITE_180x180.H = 180;

  //now add text overlays if appropriate.
  uint8_t FNTSZ = 0;
  byte deltaY = setFont(FNTSZ);

  //add info atop icon
  sprite.setTextDatum(TL_DATUM);
  sprite.setTextFont(FNTSZ);

  //precipitation alerts, these go at Y=170-fontheight 
  if (WeatherData.getPoP() > 50) {//greater than 50% cumulative precip prob in next 24 h
    uint32_t Next_Precip = WeatherData.nextPrecip();
    uint32_t nextrain = WeatherData.nextRain();
    double rain =  WeatherData.getRain() * 0.0393701; //to inches
    uint32_t nextsnow = WeatherData.nextSnow();
    double snow =  (WeatherData.getSnow() +   WeatherData.getIce())*0.0393701;
    char tempbuf[20];
    if (nextsnow < I.currentTime + 86400 && snow > 0) {
        sprite.setTextColor(sprite.color565(255,0,0),sprite.color565(0,0,255));
        snprintf(tempbuf,20,"Snow %.1f\"",snow);
        FNTSZ=4;      
    } else {
      if (rain>0 && nextrain< I.currentTime + 86400 ) {
        sprite.setTextColor(sprite.color565(255,255,0),sprite.color565(0,0,255));
        snprintf(tempbuf,20,"Rain@%s",dateify(Next_Precip,"hh:nn"));
        FNTSZ=2;
      }
    }
    sprite.drawString(tempbuf,5,Y+170-deltaY);    
  }


  //draw weather alert text if appropriate
  if (WeatherData.NumWeatherEvents>0 ) {
    WeatherData.loadNextWeatherAlert();
    //there is an alert
    WeatherEventFile wEF;
    wEF.getEvent(WeatherData.alertInfo.eventnumber);

    String description  = (String) "#" + (String) WeatherData.alertInfo.eventnumber + "/" + (String) WeatherData.NumWeatherEvents + ": " + (String) wEF.phenomenon + "\n";  
    description += (String) "When: " + (String) dateify(WeatherData.alertInfo.time_start,"mm-dd@hh") + (String) "\n";
    description += (String) "Severity/Certainty: " + wEF.severity + "/" + wEF.certainty + (String) "\n";
    description += (String) wEF.event;
    

    //draw text in hot orange on transparent background
    sprite.setTextColor(sprite.color565(255,128,0));
    sprite.drawString(description,5,Y+10);    
  }

  return refilled;
}


bool fillSprite180(int16_t X, int16_t Y, LGFX_Sprite &sprite, bool useAlerts) {

  char filename[50];

  if (useAlerts && WeatherData.NumWeatherEvents>0) {
    WeatherData.loadNextWeatherAlert();
    if (WeatherData.alertInfo.phenomenon != 0) snprintf(filename,50,"/Icons/Events/180x180/%s.bmp",WeatherData.alertInfo.phenomenon);
    else snprintf(filename,50,"/Icons/Events/180x180/Unknown.bmp");
  } else {
    bool found = false;
    byte failCount =0;
    char filedir[21];
    //get the weather icon, switching to the next available set
    while (failCount < 10 && found == false) {
      GRAPHICS.IconSet++;
      if (GRAPHICS.IconSet > 99) GRAPHICS.IconSet = 0;
      snprintf(filedir,30,"/Icons/Set%d", GRAPHICS.IconSet);
      if (FileOrDirectoryExists(filedir))    found = true;
      else     failCount++;  
    }

    if (found == false) {
      GRAPHICS.IconSet = 0; //fallback to generic icons
      snprintf(filedir,21,"/Icons/Set0");
    }

    //draw icon for NOW
    int iconID = WeatherData.getWeatherID(0);
    if (iconID < 0) iconID = 999;
    
    if (I.currentTime > WeatherData.sunrise  && I.currentTime< WeatherData.sunset) snprintf(filename,50,"%s/BMP180x180day/%d.bmp", filedir, iconID); //the listed sunrise is for this day, so might be in the past. If it is in the past, we are in the day if sunset is in the future, otherwise in the night.
    else     snprintf(filename,50,"%s/BMP180x180night/%d.bmp", filedir, iconID);

  }

  //check if the filename is different from the current filename
  if (strcmp(filename,GRAPHICS.SPRITE_180x180.filename) != 0) {
    strcpy(GRAPHICS.SPRITE_180x180.filename,filename);
  } else {
    return false;
  }



  if (drawBmp(GRAPHICS.SPRITE_180x180.filename,0,0,&sprite) != 1) {
    //failed to draw the bitmap, so fill the sprite with an error message
    char tempbuf[30];
    //print to the sprite an error message indicating that there was a failure loading BMP file
    sprite.setCursor(0,0);
    sprite.print("BMP Load Error:\n");
    sprite.println(filename);
    return false;
  }

  return true;

}  

//redraw future weather icons and text --------------------------------------------------------
void fcnDrawHourlyWeather(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, true); 
  if (timernum<0) return; //this is an error condition

  //clear the future weather area, it must be redrawn now
  GRAPHICS.clearScreenArea(index);  
  byte section_spacer = 3;
  int16_t Y = GRAPHICS.SCREEN_DATA[index].Y;
  int16_t X = GRAPHICS.SCREEN_DATA[index].X;
  int16_t Z = 0;
  int16_t deltaY = GRAPHICS.SCREEN_DATA[index].H-section_spacer+1;
  int16_t FNTSZ = 0;
  char tempbuf[50];

  //choose icon set
  uint16_t iconID = 0;
  int i=0,j=0;

  //going to draw direct to screen, these are smaller and faster than using a sprite
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(X,Y);

  for (i=1;i<7;i++) { //draw 6 icons, with I.HourlyInterval hours between icons. Note that index 1 is  1 hour from now
    if (i*GRAPHICS.IntervalHourlyWeatherDisplay>72) break; //safety check if user asked for too large an interval ... cannot display past download limit
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    uint32_t temptime = I.currentTime+i*GRAPHICS.IntervalHourlyWeatherDisplay*60*60;
    iconID = WeatherData.getWeatherID(temptime);
    if (iconID < 0) iconID = 999;      
    if (temptime > WeatherData.sunrise  && temptime< WeatherData.sunset) snprintf(tempbuf,49,"/Icons/Set%d/BMP30x30day/%d.bmp", GRAPHICS.IconSet, iconID); //the listed sunrise is for this day, so might be in the past. If it is in the past, we are in the day if sunset is in the future, otherwise in the night.
    else     snprintf(tempbuf,49,"/Icons/Set%d/BMP30x30night/%d.bmp", GRAPHICS.IconSet, iconID);
          
    drawBmp(tempbuf,X,Y);
    Z+=30;

    tft.setTextColor(FG_COLOR,BG_COLOR);
    X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

    FNTSZ=1;
    deltaY = setFont(FNTSZ);
    snprintf(tempbuf,49,"%s:00",dateify(I.currentTime+i*GRAPHICS.IntervalHourlyWeatherDisplay*60*60,"hh"));
    tft.setTextFont(FNTSZ); //small font
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);
    Z+=deltaY+section_spacer;
    tft.setTextDatum(TL_DATUM);
    FNTSZ=4;
    deltaY = setFont(FNTSZ);
    tft.setTextFont(FNTSZ); //med font
    byte temp = WeatherData.getTemperature(I.currentTime + i*GRAPHICS.IntervalHourlyWeatherDisplay*3600);
    
    fcnPrintTxtCenter(temp,FNTSZ,X,Y+Z+deltaY/2,temp2color(temp));
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Z+=deltaY+section_spacer;
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;

  return;
}
//-----------------------------------------------------------

//redraw  daily weather icons and text --------------------------------------------------------
void fcnDrawDailyWeather(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, true); 
  if (timernum<0) return; //this is an error condition

  //clear the future weather area, it must be redrawn now
  GRAPHICS.clearScreenArea(index);  

  byte section_spacer = 3;
  int16_t Y = GRAPHICS.SCREEN_DATA[index].Y;
  int16_t X = GRAPHICS.SCREEN_DATA[index].X;
  int16_t Z = 0;
  int16_t deltaY = GRAPHICS.SCREEN_DATA[index].H-section_spacer+1;

  int16_t FNTSZ = 0;
  char tempbuf[50];

  //choose icon set
  uint16_t iconID = 0;
  int i=0,j=0;

  //going to draw direct to screen, these are smaller and faster than using a sprite
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(X,Y);
  
  
  //now draw daily weather
  tft.setCursor(0,Y);
  for (i=1;i<6;i++) {
    int8_t tmm[2];
    Z=0;
    X = (i-1)*(tft.width()/5) + ((tft.width()/5)-60)/2; 
    iconID = WeatherData.getDailyWeatherID(i,true);
    snprintf(tempbuf,49,"/Icons/Set%d/BMP60x60day/%d.bmp", GRAPHICS.IconSet, iconID); //icon
    
    drawBmp(tempbuf,X,Y);
    Z+=60;
    
    X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
    FNTSZ=2;
    deltaY = setFont(FNTSZ);
    snprintf(tempbuf,50,"%s",dateify(I.currentTime+i*60*60*24,"DOW"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);
    
    Z+=deltaY;
    WeatherData.getDailyTemp(i,tmm);
    fcnPrintTxtCenter((String) tmm[0] + "/" + tmm[1],FNTSZ,X,Y+Z+deltaY/2,temp2color(tmm[0]),temp2color(tmm[1])); 
    tft.setTextColor(FG_COLOR,BG_COLOR);
    
    Z+=deltaY+section_spacer;

  }
  GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;
}
//-----------------------------------------------------------
//-----------------------------------------------------------
//-----------------------------------------------------------
//-----------------------------------------------------------


void fcnDrawSensorScreen(int16_t index) {
  //this will be the main distribution screen for sensors drawing. This function decides what to draw and distributes tasks to subfunctions.
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0 )     return; 

  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] == 0) {
    fcnSwitchToNewScreen(SCREEN_MAIN);
    return;
  }

  //now assign the screen elements, based on subscreen info
  //subscreen 0 = sensor summary
  //subscreen 1 = sensor details
  //subscreen 101 = del sensor
  //subscreen 102 = del sensor confirm
  if (GRAPHICS.SubScreen_Next != GRAPHICS.SubScreen_Now || GRAPHICS.SCREEN_DATA[1].drawFunction == nullptr) {
    //subscreen changed!
    //clear the current subscreen
    GRAPHICS.SCREEN_DATA[0].clearScreenElementArea();
    GRAPHICS.initRemainingScreenElements(2); //initialize all other screen elements. the main screen element is responsible for loading all other screen elements.
    GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;
    GRAPHICS.Screen_Now = GRAPHICS.SCREEN_DATA[0].Screen_Next; //this should match screen_next

    if (GRAPHICS.SubScreen_Next == 0) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawSensorSummarySubcreen, 0,0,320,360, SCREEN_NONE, 0, 1, 30, 1, &fcnConvertTouchToSensorSubscreen);
    else if (GRAPHICS.SubScreen_Next ==1) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawSensorDetailsSubscreen, 0,0,320,360, SCREEN_MAIN, 0, 1, 30, 1, &fcnSwitchScreen); 
    else if (GRAPHICS.SubScreen_Next == 110) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawSensorDelSnsConfirm, 0,0,320,360, SCREEN_NONE, 0, 1, 30, 1, nullptr);
    else if (GRAPHICS.SubScreen_Next == 107) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawSensorBroadcast, 0,0,320,360, SCREEN_MAIN, 0, 1, 30, 1, &fcnSwitchScreen);
    else if (GRAPHICS.SubScreen_Next == 100) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawSensorDelSns, 0,0,320,360, SCREEN_MAIN, 0, 1, 30, 1, &fcnSwitchScreen); 

    
    //run the new subscreen
    GRAPHICS.SCREEN_DATA[1].drawFunction(1);

    GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;
    
    
  }



  return;

}

void fcnConvertTouchToSensorSubscreen(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  //reset main timer
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = GRAPHICS.SCREEN_DATA[0].Timer_RESET;


  //the sensor summary screen is a grid of 8x6 sensor boxes. Each box is 64x64 pixels. Determine which box was touched.
  int16_t boxsize6 = tft.width()/6;

  byte row = (uint16_t) GRAPHICS.touchY / boxsize6; //zero indexed row
  byte col = (uint16_t) GRAPHICS.touchX / boxsize6; //zero indexed column

  GRAPHICS.SubScreen_Next = 1; 
  GRAPHICS.miscData = row*6 + col;
  if (GRAPHICS.miscData < 0 || GRAPHICS.miscData >= MAXALARMS || alarms[GRAPHICS.miscData] == 255)   GRAPHICS.miscData=-1;
  else GRAPHICS.miscData = alarms[GRAPHICS.miscData];
  
  return;
}


void fcnDrawSensorDelSnsConfirm(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  //reset main timer to modest interval
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 5;

  //this screen asks for confirmation before deleting a sensor
  GRAPHICS.clearScreenArea(0); //clear the entire screen
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);

  if (GRAPHICS.miscData >= 0 && GRAPHICS.miscData < NUMSENSORS)   {
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(GRAPHICS.miscData);
    tft.printf("Are you sure you want\nto delete sensor:\n%s\n%s?",MACToString(Sensors.getDeviceMACBySnsIndex(GRAPHICS.miscData),':',true).c_str(),sensor->snsName);
    GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_NONE, 100, 0, 30, 2, &fcnSwitchSubScreen); 
    GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawNavButtons, 128,419,64,60, SCREEN_MAIN, 101, 0, 30, 3, &fcnSwitchScreen);   

    //run these new screen elements
    GRAPHICS.SCREEN_DATA[2].drawFunction(2);
    GRAPHICS.SCREEN_DATA[3].drawFunction(3);
  }  else {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.setCursor(0,0);
    tft.println("Sensor not found");
    tft.println("Touch EXIT button to\nreturn to main screen");

    tft.setTextColor(FG_COLOR,BG_COLOR);
    GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen);   
    GRAPHICS.SCREEN_DATA[2].drawFunction(2);

  }

  return;
}

void fcnDrawSensorDelSns(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  GRAPHICS.clearScreenArea(0); //clear the entire screen  

  //delete the sensor!
  if (GRAPHICS.miscData >= 0 && GRAPHICS.miscData < NUMSENSORS) {
    ArborysSnsType* sensor = Sensors.snsIndexToPointer(GRAPHICS.miscData);
    Sensors.initSensor(GRAPHICS.miscData);
  }
  GRAPHICS.miscData = -1;

//reset main timer to short interval
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 5;

  //this screen asks for confirmation before deleting a sensor
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.println("Sensor cleared");
  tft.println("Touch anywhere to\nreturn to main screen");

  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen); 
  GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  return;
}

void fcnDrawSensorDetailsSubscreen(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  //reset main timer 
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 30;
  GRAPHICS.clearScreenArea(0); //clear the entire screen  


  if (GRAPHICS.miscData < 0 || GRAPHICS.miscData >= NUMSENSORS) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.setCursor(0,0);
    tft.println("Sensor " + String(GRAPHICS.miscData) + " not found");
    tft.println("Touch anywhere to\nreturn to main screen");
    tft.setTextColor(FG_COLOR,BG_COLOR);

    //reset main timer 
    GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 5;
    GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen); 
    GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  
    return;
  }

//does sensor exist?
  ArborysSnsType* sensor = Sensors.snsIndexToPointer(GRAPHICS.miscData);
  if (!sensor || !Sensors.isSensorInit(GRAPHICS.miscData)) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Sensor " + String(GRAPHICS.miscData) + " not found",tft.width()/2,25);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    return;
  }
  int16_t X=0,graphstart=0, graphheight=150,graphwidth=300;
  byte rightmargin=5;
  
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.printf("Sensor Info for sensor %s",sensor->snsName);
  graphstart+=tft.fontHeight(tft.getFont())+4;

  //draw an xy graph of the historical sensor data

  //get the historical sensor data
  
  byte N=100;
  uint32_t  endtime=-1;
  
  uint32_t t[N]={0};
  double v[N]={0};
  uint8_t f[N]={0};
  
  bool success=false;
  success = retrieveSensorDataFromSD(Sensors.getDeviceMACBySnsIndex(GRAPHICS.miscData), sensor->snsType, sensor->snsID, &N, t, v, f, 0,endtime,true); //this fills from tn backwards to N*delta samples
  uint32_t maxt=0, mint=UINT32_MAX;
  double maxv=0, minv=1000000;

  if (success) {
    tft.fillRect(0,graphstart,tft.width(),graphheight,BG_COLOR); //grid will be tft.width() wide and 100 pixels high
    tft.setTextFont(1);
    tft.drawLine(0,graphstart+graphheight,tft.width()-rightmargin,graphstart+graphheight,FG_COLOR);
    
    //find max and min of v
    for (byte k=0;k<N;k++) {
      if (v[k]>maxv) maxv=v[k];
      if (v[k]<minv) minv=v[k];
      if (t[k]>maxt) maxt=t[k];
      if (t[k]<mint) mint=t[k];
    }

    //draw the y axis labels
    tft.setTextFont(0);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setCursor(0,graphstart);
    tft.printf("%.0f",maxv);
    tft.setCursor(0,graphstart+graphheight-tft.fontHeight(tft.getFont())-4);
    tft.printf("%.0f",minv);
    //draw the x axis labels
    tft.setCursor(tft.width()-graphwidth,graphstart+graphheight+tft.fontHeight(tft.getFont())+4);
    tft.printf("%s",dateify(mint,"mm/dd/yyyy hh:nn:ss"));
    tft.setCursor(tft.width()-tft.textWidth("mm/dd/yyyy hh:nn:ss"),graphstart+graphheight+tft.fontHeight(tft.getFont())+4);
    tft.printf("%s",dateify(maxt,"mm/dd/yyyy hh:nn:ss"));

    //draw a vertical line at the start of the graph
    tft.drawLine(tft.width()-graphwidth,graphstart,tft.width()-graphwidth,graphstart+graphheight,FG_COLOR);

    double oldtval = -1;
    //draw pixels representing each [time,value]. Scale to maxv and minv to fit in 100 pixel height and 300 pixel width, and to maxt and mint to fit in 300 pixel width
    for (byte k=0;k<N;k++) {
      //ensure pixels do not overlap
      double tval = (double)(t[k]-mint)/(maxt-mint)*(graphwidth-rightmargin); //right margin so that plotting does not hit screen edges
      //SerialPrint(String("tval: ") + tval + " oldtval: " + oldtval + "\n");
      if ((int) tval != (int) oldtval || k==N-1) { //draw the last pixel no matter what
        double vscale = (v[k]-minv)/(maxv-minv)*graphheight;          
        if (bitRead(f[k],0)==1) {
          tft.drawPixel(tval+(tft.width()-graphwidth),-1*vscale+(graphstart+graphheight),TFT_RED);
        } else {
          tft.drawPixel(tval+(tft.width()-graphwidth),-1*vscale+(graphstart+graphheight),TFT_GREEN);
        }
        oldtval = tval;
      }
    }
    double tval = (double)(sensor->timeLogged-mint)/(maxt-mint)*(graphwidth-rightmargin); //right margin so that plotting does not hit screen edges
    double vscale = (sensor->snsValue-minv)/(maxv-minv)*graphheight;          
    tft.fillCircle(tval+(tft.width()-graphwidth),-1*vscale+(graphstart+graphheight),2,TFT_GOLD); //most recent value
  }
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,graphstart+graphheight+tft.fontHeight(tft.getFont())*2);
  tft.printf("Min: %.02f\n",minv);
  if (bitRead(sensor->Flags,0)==1) {
    tft.setTextColor(TFT_RED,BG_COLOR);
  } else {
    tft.setTextColor(TFT_GREEN,BG_COLOR);
  }
  tft.printf("Current: %0.02f\n",sensor->snsValue);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.printf("Max: %.02f\n",maxv);

  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.printf("Device IP:%s\n",Sensors.getDeviceIPBySnsIndex(GRAPHICS.miscData).toString().c_str());
  tft.printf("Sns type: %d\n",sensor->snsType);
  tft.printf("Sns ID: %d\n",sensor->snsID);
  tft.printf("Sns name: %s\n",sensor->snsName);
  tft.printf("Sns last logged: %s\n",dateify(sensor->timeLogged,"mm/dd/yyyy hh:nn:ss"));
  tft.printf("Sns flags: %s\n",String(sensor->Flags,BIN).c_str());

  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen); 
  GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawNavButtons, 128,419,64,60, SCREEN_NONE, 110, 0, 30, 3, &fcnSwitchSubScreen); 

  GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  GRAPHICS.SCREEN_DATA[3].drawFunction(3);


  return;
}


void fcnDrawSensorSummarySubcreen(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  GRAPHICS.miscData = -1;
  
  //reset the main timer
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = GRAPHICS.SCREEN_DATA[0].Timer_RESET;
  GRAPHICS.clearScreenArea(0); //clear the entire screen  

  //populate the nav buttons for this screen
  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_MAIN, 0, 0, 5, 2, &fcnSwitchScreen); 
  GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_NONE, 107, 0, 5, 3, &fcnSwitchSubScreen); 

  //run the new screen elements
  GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  GRAPHICS.SCREEN_DATA[3].drawFunction(3);

  byte alarmcount = fcnGetAlarms(0,8,6);


  int16_t FH = setFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  fcnDrawSensors(0,18,8,6);
  tft.setCursor(0,tft.height()-FH-62);
  tft.printf("Devices: %d; Sensors: %d; Alarms: %d",Sensors.numDevices,Sensors.numSensors,alarmcount);

  return;
}


void fcnDrawSensorBroadcast(int16_t index) {
  if (index <0 || index >= MAXALARMS) return; //this is an error condition

  GRAPHICS.clearScreenArea(0); //clear the entire screen  
  //delete the sensor!

//reset main timer to short interval
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 3;

  broadcastServerPresence(true, 2);

  //this screen asks for confirmation before deleting a sensor
  tft.setTextFont(4);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.println("Broadcast sent");
  tft.setTextFont(2);

  tft.println("Touch anywhere to\nreturn to main screen");

}

//----------------------------------------------------------  
//----------------------------------------------------------  
//----------------------------------------------------------  

//draw the status screen --------------------------------------------------------
void fcnDrawStatusScreen(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0 ) return;



  //now assign the screen elements, based on subscreen info
  //subscreen 0 = status summary
  //subscreen 108 = confirm delete core
  //subscreen 100 = delete core
  //subscreen 106 = reboot
  //subscreen 111 = weather update
  //subscreen 3 = 

  if (GRAPHICS.SubScreen_Next != GRAPHICS.SubScreen_Now || GRAPHICS.SCREEN_DATA[1].drawFunction == nullptr) {

    GRAPHICS.Screen_Now = GRAPHICS.SCREEN_DATA[0].Screen_Next; //this should match screen_next
    GRAPHICS.Screen_Next = GRAPHICS.SCREEN_DATA[0].Screen_Next; //just to be sure

    GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;

    //clear the current subscreen
    GRAPHICS.SCREEN_DATA[0].clearScreenElementArea();
    GRAPHICS.initRemainingScreenElements(2); //initialize all other screen elements. the main screen element is responsible for loading all other screen elements.

    GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] = GRAPHICS.SCREEN_DATA[index].Timer_RESET;


    if (GRAPHICS.SubScreen_Next == 0) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusText, 0,0,320,360, SCREEN_MAIN, 0, 0, 30, 1, &fcnSwitchScreen); //for ease, touching the screen moves to main
    else if (GRAPHICS.SubScreen_Next == 108) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusDelCoreConfirm, 0,0,320,360, SCREEN_NONE, 0, 0, 30, 1, nullptr);
    else if (GRAPHICS.SubScreen_Next == 100) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusDelCore, 0,0,320,360, SCREEN_NONE, 0, 0, 30, 1, nullptr);
    else if (GRAPHICS.SubScreen_Next == 106) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusReboot, 0,0,320,360, SCREEN_NONE, 0, 0, 30, 1, nullptr); 
    else if (GRAPHICS.SubScreen_Next == 111) GRAPHICS.SCREEN_DATA[1].loadScreenElements(&fcnDrawStatusWeatherUpdate, 0,0,320,360, SCREEN_MAIN, 0, 0, 30, 1, &fcnSwitchScreen);

    //run the new subscreen
    GRAPHICS.SCREEN_DATA[1].drawFunction(1);


  }

  if (GRAPHICS.GRAPHICS_TIMERS.Timers[timernum] == 0) {
    fcnSwitchToNewScreen(SCREEN_MAIN);
    return;
  }

  return;
}

void fcnDrawStatusDelCoreConfirm(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition
  GRAPHICS.clearScreenArea(0); //clear the entire screen  

  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.println("Are you sure?");
  tft.println("This will delete stored screen data");
  tft.println("and reset screen defaults.");
  tft.println("The device will reboot.");

  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_NONE, 100, 0, 30, 2, &fcnSwitchSubScreen); 

  GRAPHICS.SCREEN_DATA[2].drawFunction(2);

  //set the timer to a smaller amount
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 10;

  return;
}

void fcnDrawStatusWeatherUpdate(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition
  GRAPHICS.clearScreenArea(0); //clear the screen
  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.println("Weather update in progress...");
  bool success = WeatherData.updateWeather(0);
  if (success) {
    tft.println("Weather update successful");
  } else {
    tft.println("Weather update failed");
  }
  if (WeatherData.lastUpdateT != 0) {
    tft.printf("Last update: %s\n",dateify(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss"));
  } else {
    tft.println("No prior successful update time");
  }
  tft.println("Touch anywhere to\nreturn to main screen");

  //set the timer to a smaller amount
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = 5;

  return;
}

void fcnDrawStatusDelCore(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition
  deleteCoreStruct(); //delete the screen flags file        
  controlledReboot("Reset I",RESET_USER,true); //reset the device
  return;
}

void fcnDrawStatusReboot(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition
  controlledReboot("Reboot",RESET_USER,true); //reboot the device
  return;
}


void fcnDrawStatusText(int16_t index) {
  int16_t timernum = helper_runScreenElementFunction(index, false); 
  if (timernum<0) return; //this is an error condition

  GRAPHICS.clearScreenArea(0); //clear the screen (also zeros Timers[0]; restore below)

  // Same pattern as fcnDrawSensorSummarySubcreen: clearScreenArea(0) zeros the main element timer,
  // which would leave Timers[0]==0 and force fcnDrawScreen() every loop → flicker on SCREEN_STATUS.
  GRAPHICS.GRAPHICS_TIMERS.Timers[0] = GRAPHICS.SCREEN_DATA[0].Timer_RESET;

  //add the function buttons
  GRAPHICS.SCREEN_DATA[2].loadScreenElements(&fcnDrawNavButtons, 0,419,64,60, SCREEN_MAIN, 0, 0, 30, 2, &fcnSwitchScreen); 
  GRAPHICS.SCREEN_DATA[3].loadScreenElements(&fcnDrawNavButtons, 64,419,64,60, SCREEN_NONE, 108, 0, 30, 3, &fcnSwitchSubScreen); 
  GRAPHICS.SCREEN_DATA[4].loadScreenElements(&fcnDrawNavButtons, 128,419,64,60, SCREEN_NONE, 106, 0, 30, 4, &fcnSwitchSubScreen); 
  GRAPHICS.SCREEN_DATA[5].loadScreenElements(&fcnDrawNavButtons, 192,419,64,60, SCREEN_NONE, 111, 0, 30, 5, &fcnSwitchSubScreen); 

  //run the new screen elements
  GRAPHICS.SCREEN_DATA[4].drawFunction(4);
  GRAPHICS.SCREEN_DATA[2].drawFunction(2);
  GRAPHICS.SCREEN_DATA[3].drawFunction(3);
  GRAPHICS.SCREEN_DATA[5].drawFunction(5);


  //draw config screen
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);  
  tft.setTextFont(1);
  tft.println("Status");
  tft.printf("Report Time: %s\n",(I.currentTime>20000)?dateify(I.currentTime,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Alive Since: %s\n",(I.ALIVESINCE!=0)?dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Last Reset Time: %s\n",(I.lastResetTime!=0)?dateify(I.lastResetTime,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Device Name: %s\n",Prefs.DEVICENAME);
  tft.printf("Device IP:\n");
  tft.printf("OS Version: %s\n",PROJECT_VER);
  //print memory usage
  tft.printf("Heap Total: %d\n",ESP.getHeapSize());
  tft.printf("Heap Available: %d\n",ESP.getFreeHeap());
  tft.printf("Free PSRAM: %d\n",ESP.getFreePsram());
  tft.setTextFont(2);
  tft.printf("%s\n",WiFi.localIP().toString().c_str());
  tft.setTextFont(1);
  tft.printf("-----------------------\n");
  tft.printf("Weather Event Flags: %s\n",(isBit(I.WeatherEventFlags, 0)?"Yes":"No"));
  tft.printf("Num Weather Events: %d\n",WeatherData.NumWeatherEvents);
  tft.printf("Last weather update: %s\n",(WeatherData.lastUpdateT!=0)?dateify(WeatherData.lastUpdateT,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("-----------------------\n");
  tft.printf("LAN Messages Received today: %d\n",I.UDP_RECEIVES);    
  tft.printf("LAN Messages Sent today: %d\n",I.UDP_SENDS);
  tft.printf("HTTP Messages Received today: %d\n",I.HTTP_RECEIVES);
  tft.printf("HTTP Messages Sent today: %d\n",I.HTTP_SENDS);
  tft.printf("UDP Messages Received today: %d\n",I.UDP_RECEIVES);    
  tft.printf("UDP Messages Sent today: %d\n",I.UDP_SENDS);
  tft.printf("-----------------------\n");
    
  tft.printf("Last Error Time: %s\n",(I.lastErrorTime!=0)?dateify(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Last Error: %s\n",I.lastError);
  tft.printf("Wifi fail count : %d\n",I.wifiFailCount);
  tft.printf("Reboots since last: %d\n",I.rebootsSinceLast);
  tft.printf("-----------------------\n");
  //list flag data
  tft.printf("Flagged sensors: %d\n",I.isFlagged);
  tft.printf("Expired sensors: %d\n",I.isExpired);
  tft.printf("-----------------------\n");
  tft.printf("Timezone: %ld \n", Prefs.TimeZoneOffset);
  //if DST is enabled, print the DST offset and date of DST start and end
  tft.printf("DST: ");
  if (I.DST==1) tft.printf("Not active\n");
  else if (I.DST==2) tft.printf("Active, offset %ld min\n",I.DSTOffset/60);
  else tft.printf("Not used here\n");
  if (I.DST>=0) {
    tft.printf("DST Start: %s\n", dateify(I.DSTStartUnixTime,"mm/dd/yyyy hh:nn:ss"));
    tft.printf("DST End: %s\n", dateify(I.DSTEndUnixTime,"mm/dd/yyyy hh:nn:ss"));
  }

  tft.printf("-----------------------\n");
  tft.printf("Local Battery Index: %d\n",I.localBatteryIndex);
  tft.printf("Have Outside Temperature Sensor: %s\n",I.haveOutsideTemperatureSensor?"Yes":"No");
  tft.printf("Current Outside Temp: %d\n",I.currentOutsideTemp);
  tft.printf("Current Outside Humidity: %d\n",I.currentOutsideHumidity);
  tft.printf("Current Outside Pressure: %d\n",I.currentOutsidePressure);

  return;
}



void checkTouchScreen() {
  int32_t tX, tY;
  if (tft.getTouch(&tX, &tY)) {
    GRAPHICS.touchX = tX;
    GRAPHICS.touchY = tY;

    while (tft.getTouch(&tX, &tY)) { delay(10); }
    SerialPrint("Touch detected at " + String(GRAPHICS.touchX) + "," + String(GRAPHICS.touchY),false);
    
    for (byte i=1;i<MAXSCREENELEMENTS;i++) { //note that we skip element 0, which is the main screen
      if (GRAPHICS.SCREEN_DATA[i].X <= GRAPHICS.touchX && GRAPHICS.SCREEN_DATA[i].X + GRAPHICS.SCREEN_DATA[i].W >= GRAPHICS.touchX && GRAPHICS.SCREEN_DATA[i].Y <= GRAPHICS.touchY && GRAPHICS.SCREEN_DATA[i].Y + GRAPHICS.SCREEN_DATA[i].H >= GRAPHICS.touchY) {
        //you touched the i-th element of the current screen
        
        if (GRAPHICS.SCREEN_DATA[i].RunFcnOnTouch != nullptr) {
          SerialPrint(" on element " + String(i) + " running function",true);
          GRAPHICS.SCREEN_DATA[i].RunFcnOnTouch(i);
        } else {
          SerialPrint(" on element " + String(i) + " but no function to run",true);
        }
        return;
      }
    }

    SerialPrint(" on no element registered",true);
  }
  else {
    GRAPHICS.touchX = -1;
    GRAPHICS.touchY = -1;
  }

}


#endif