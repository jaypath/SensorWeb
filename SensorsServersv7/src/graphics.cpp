#include "globals.hpp"

#if defined(_USETFT) && !defined(_ISPERIPHERAL)

#include "graphics.hpp"
#include "BootSecure.hpp"



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
LGFX_Sprite Sprite320x100(&tft);


// Graphics utility functions
bool initGraphics() {


  Sprite180x180.createSprite(180, 180, true);
  Sprite320x100.createSprite(320, 100, true);

  //check if the sprites were created successfully
  if (!Sprite180x180.getBuffer() || !Sprite320x100.getBuffer()) {
    Serial.println("Error: Sprite creation failed");
    return false;
  }

  GRAPHICS.IconSet = 0;
  GRAPHICS.MainBMPAreaFlags = 0;

  // GRAPHICS_TIMERS: initScreenFlags is called after memset in deleteCoreStruct, which zeros
  // I entirely. Without this, Timer_*_RESET stay zero and get saved to SD, causing timers
  // to fail on every boot. Explicitly set defaults to match SECOND_TIMER_STRUCT.
  GRAPHICS.GRAPHICS_TIMERS.zeroTimers();

    GRAPHICS.Screen_Now = SCREEN_MAIN;
  GRAPHICS.Screen_Next = SCREEN_MAIN;


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

      // Handle byte swapping logic for direct TFT draw
      bool oldSwapBytes = tft.getSwapBytes();
      if (!sprite) tft.setSwapBytes(true);

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
      if (!sprite) tft.setSwapBytes(oldSwapBytes);
    } else {
      free(lineBuffer);
      bmpFile.close();
      if (!planes) StoreError("BMP Error with planes", ERROR_BMP_PLANES, true);
      if (!bitDepth) StoreError("BMP Error with bitDepth", ERROR_BMP_BITDEPTH, true);
      if (!nocompression) StoreError("BMP Error with compression", ERROR_BMP_COMPRESSION, true);
      return -4; //error with file header

    }
  }
  bmpFile.close();
  free(lineBuffer);
  return 1;
}


/*
void drawBmp(const char *filename, int16_t x, int16_t y,  uint16_t alpha) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) {
    
      SerialPrint("File not found: ",false,5);
      SerialPrint(filename,true,5);
      storeError("drawBmp: File not found: " + String(filename), ERROR_SD_FILEREAD, true);
    
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;
  uint32_t startTime = millis();

  const size_t lineBufSize = 1024 * 2; // 2K for 24-bit row + headroom
  uint8_t* lineBuffer = (uint8_t*)ps_malloc(lineBufSize);
  if (!lineBuffer) {
    bmpFile.close();
    return;
  }

  uint16_t signature = read16(bmpFile);
  if (signature == 0x4D42) {
    read32(bmpFile);
    read32(bmpFile);
    seekOffset = read32(bmpFile);
    read32(bmpFile);
    w = read32(bmpFile);
    h = read32(bmpFile);

    if ((read16(bmpFile) == 1) && (read16(bmpFile) == 24) && (read32(bmpFile) == 0)) {
      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      size_t rowBytes = (size_t)w * 3 + padding;
      size_t outBytes = (size_t)w * 2;
      if (rowBytes + outBytes > lineBufSize) {
        free(lineBuffer);
        bmpFile.close();
        return;
      }

      y += h - 1;
      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFile.seek(seekOffset);

      uint8_t* row24 = lineBuffer;           // 24-bit BGR input row
      uint16_t* row16 = (uint16_t*)(lineBuffer + rowBytes);  // 16-bit RGB565 output row (no overlap)

      for (row = 0; row < h; row++) {
        if (bmpFile.read(row24, rowBytes) != (int)rowBytes) break;
        for (uint16_t col = 0; col < w; col++) {
          b = row24[col * 3 + 0];
          g = row24[col * 3 + 1];
          r = row24[col * 3 + 2];
          row16[col] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
        tft.pushImage(x, y--, w, 1, row16);
      }
      tft.setSwapBytes(oldSwapBytes);
    }
  }
  bmpFile.close();
  free(lineBuffer);
}
*/

void loadBmpToSprite(LGFX_Sprite &sprite, const char *filename, int16_t sx, int16_t sy) {
  File bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) {
    Serial.print("File not found: ");
    Serial.println(filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t r, g, b;

  // Read BMP Header
  uint16_t signature = read16(bmpFile);
  if (signature != 0x4D42) {
    bmpFile.close();
    return;
  }

  read32(bmpFile); // File size
  read32(bmpFile); // Reserved
  seekOffset = read32(bmpFile);
  read32(bmpFile); // Header size
  w = read32(bmpFile);
  h = read32(bmpFile);

  // Check if 24-bit and uncompressed
  if ((read16(bmpFile) == 1) && (read16(bmpFile) == 24) && (read32(bmpFile) == 0)) {
    uint16_t padding = (4 - ((w * 3) & 3)) & 3;
    size_t rowBytes = (size_t)w * 3 + padding;
    
    // Allocate buffer for one row of 24-bit data
    uint8_t* rowBuffer = (uint8_t*)ps_malloc(rowBytes);
    if (!rowBuffer) {
      bmpFile.close();
      return;
    }

    bmpFile.seek(seekOffset);

    // BMPs are stored bottom-to-top. 
    // We iterate through rows and draw from (sy + h - 1) down to sy.
    for (row = 0; row < h; row++) {
      if (bmpFile.read(rowBuffer, rowBytes) != (int)rowBytes) break;
      
      int16_t pixelY = sy + (h - 1 - row);
      
      // Safety check: don't draw outside sprite boundaries
      if (pixelY < 0 || pixelY >= sprite->height()) continue;

      for (col = 0; col < w; col++) {
        int16_t pixelX = sx + col;
        if (pixelX < 0 || pixelX >= sprite->width()) continue;

        b = rowBuffer[col * 3 + 0];
        g = rowBuffer[col * 3 + 1];
        r = rowBuffer[col * 3 + 2];

        // Convert to RGB565 and push to sprite
        uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        sprite->drawPixel(pixelX, pixelY, color);
      }
    }
    free(rowBuffer);
  }
  bmpFile.close();
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
  tft.setTextColor(text_color,box_fill);
  
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
      fcnPrintTxtCenter(tempstr,FNTSZ, X+boxsize_x/2,Y+FH/2);
      Y+=2+FH;
    }
  } 
} 

// Text drawing functions

void fcnPrintTxtCenter(String msg, byte FNTSZ, int x, int y, uint16_t color1, uint16_t color2, uint16_t bgcolor, LGFX_Device* target) {
  // Default to the global 'tft' object if no target is provided
  // Using LovyanGFX's base class allows this to work for both screen and sprites
  auto& canvas = (target != nullptr) ? *target : tft;

  int X, Y;
  // Set the font and get height. setFont in LGFX returns font height.
  uint32_t FH = canvas.setFont(setFont(FNTSZ)); 

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
          
          canvas.setTextColor(FG_COLOR, BG_COLOR);
          canvas.print("/");
          
          canvas.setTextColor(color2, bgcolor);
          canvas.print(msg.substring(slashIdx + 1).c_str());
      } else {
          // Fallback if no slash is found despite different colors
          canvas.setTextColor(color1, bgcolor);
          canvas.print(msg.c_str());
      }
  }

  canvas.setTextDatum(text_datum_t::top_left);
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



// Screen drawing functions
void fcnDrawHeader() {
  //the header is on screen N, and is element 1


  //clear the header area
  GRAPHICS.clearScreenArea(0,1);

  //draw header    
  String st = "";
  uint16_t x = GRAPHICS.SCREEN_0[1].X,y=GRAPHICS.SCREEN_0[1].Y;
  uint32_t FH = setFont(4);
  tft.setCursor(x,y);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  
  tft.setTextDatum(TL_DATUM);
  st = dateify(I.currentTime,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;
  

  tft.setTextDatum(TL_DATUM);
  FH = setFont(2);


  if (I.isFlagged) {
    tft.setTextFont(2);
    tft.setTextColor(tft.color565(255,0,0),BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y+4);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  }
  x += tft.textWidth("FLAG")+10;

  setFont(0);
  fcnPrintTxtHeatingCooling(x,5);
  st = "XX XX XX "; //placeholder to find new X position
  x += tft.textWidth(st)+4;
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background


  if (bitRead(I.WeatherEventFlags,0) == 1) {
    st=WeatherData.alertInfo.event;
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR); 

    tft.drawString(st,x,y+4);

    x += tft.textWidth(st)+4;
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background

  } else {

    //add heat and AC icons to right 
    if ((I.isHeat&1)==1) {
      st="/Icons/HVAC/flame.bmp";
      drawBmp(st.c_str(),GRAPHICS.SCREEN_0[1].W-30,y);
    }
    if ((I.isAC&1)==1) {
      st="/Icons/HVAC/ac30.bmp";
      drawBmp(st.c_str(),GRAPHICS.SCREEN_0[1].W-65,y);
    }

  }

   
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  

  FH = setFont(2);
  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);



  //reset the draw timer.
  GRAPHICS.GRAPHICS_TIMERS.Timers[1] = GRAPHICS.GRAPHICS_TIMERS.Timer0_RESET[1];

  

  return;
}

void fcnDrawDailyWeather() {
  return;
}

void fcnDrawHourlyWeather() {
  return;
}


void fcnDrawScreen() {
  //screen 0 - main weather screen
//screen 1 - sensor info screen
//screen 2 - WeatherAlerts screen
//screen 3 - daily weather info screen
//screen 4 - hourly weather info screen
//screen 5 - status screen


  if(WiFi.status()!= WL_CONNECTED){  
    screenWiFiDown();
    return;
  }

  if (GRAPHICS.GRAPHICS_TIMERS.Timer_ScreenChange == 0 && GRAPHICS.Screen_Now != SCREEN_MAIN) {
    GRAPHICS.Screen_Next = SCREEN_MAIN;
    GRAPHICS.SubScreen_Next = 0;
    GRAPHICS.GRAPHICS_TIMERS.forceRedraw_MAIN = true;
    GRAPHICS.GRAPHICS_TIMERS.Timer_ScreenChange = GRAPHICS.GRAPHICS_TIMERS.Timer_ScreenChange_RESET;
  }

  //the current desired screen is different what was last drawn... prep for that change
  if (GRAPHICS.SubScreen_Next != GRAPHICS.SubScreen_Now || GRAPHICS.Screen_Next!=GRAPHICS.Screen_Now) {
    GRAPHICS.clearScreenArea(0,0);
  }

  if (GRAPHICS.Screen_Next!=GRAPHICS.Screen_Now) {
    GRAPHICS.SubScreen_Next = 0;

    if (GRAPHICS.Screen_Next==SCREEN_MAIN)       GRAPHICS.GRAPHICS_TIMERS.forceRedraw_MAIN = true;
    
  }


  if (GRAPHICS.Screen_Next==SCREEN_MAIN ) fcnDrawMainScreen(); 
  else if (GRAPHICS.Screen_Now==GRAPHICS.Screen_Next &&  GRAPHICS.SubScreen_Now==GRAPHICS.SubScreen_Next) return; //no screen update needed
  else if (GRAPHICS.Screen_Next==SCREEN_SENSORS ) fcnDrawSensorScreen();
  else if (GRAPHICS.Screen_Next==SCREEN_ALERT )      fcnDrawAlertScreen();
  else if (GRAPHICS.Screen_Next==SCREEN_DAILY )      fcnDrawDailyWeather();
  else if (GRAPHICS.Screen_Next==SCREEN_HOURLY )      fcnDrawHourlyWeather();  
  else if (GRAPHICS.Screen_Next==SCREEN_STATUS )      fcnDrawStatus();  

GRAPHICS.Screen_Now = GRAPHICS.Screen_Next;
GRAPHICS.SubScreen_Now = GRAPHICS.SubScreen_Next;

  return;
}

void fcnDrawMainScreen() {
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[6] == 0 ) fcnDrawClock(); 
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[1] == 0) fcnDrawHeader();
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[3] == 0 ) fcnDrawCurrentWeatherText();  
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[2] == 0 ) fcnDrawCurrentWeatherIconOrAlert();
  if (GRAPHICS.GRAPHICS_TIMERS.Timers[4] == 0 ) fcnDrawFutureWeather();
  return;
}


void fcnDrawNavButtons(const char* buttonText[6]) {
  //clear the nav buttons area
  tft.fillRect(0,GRAPHICS.Y_SCRALL_NAVBUTTONS,tft.width(),tft.height()-GRAPHICS.Y_SCRALL_NAVBUTTONS,BG_COLOR);
  tft.setTextFont(2);
  tft.setTextColor(BG_COLOR,TFT_LIGHTGREY);
  tft.setTextDatum(MC_DATUM);
  
  byte width = GRAPHICS.buttonWidth-GRAPHICS.buttonInteriorMargin*2;
  byte height = GRAPHICS.buttonWidth-GRAPHICS.buttonInteriorMargin*2;
  uint16_t y = GRAPHICS.Y_SCRALL_NAVBUTTONS + GRAPHICS.buttonInteriorMargin;

  //draw 6 boxes across the nav bar
  for (byte k=0;k<6;k++) {

    uint16_t x = k*GRAPHICS.buttonWidth + GRAPHICS.buttonInteriorMargin;

    tft.fillRoundRect(x,y,width,height,10,TFT_LIGHTGREY);
    tft.setTextColor(TFT_BLACK,TFT_LIGHTGREY);
    fcnPrintTxtCenter(buttonText[k],2, x+width/2,y+height/2,TFT_BLACK,TFT_BLACK,TFT_LIGHTGREY);
  }
  tft.setTextColor(FG_COLOR,BG_COLOR);  
  tft.setTextDatum(TL_DATUM);
}


void fcnDrawAlertScreen() {
  String text;
  text.reserve(1000); 
  tft.fillRect(0,0,tft.width(),tft.height(),BG_COLOR);
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
    
    text = "Severity: \n" + wEF.severity;
    tft.drawString(text,184,Y);

    text = "Urgency: \n" + wEF.urgency;
    tft.drawString(text,184,Y+4+tft.fontHeight(tft.getFont()));

    text = "Certainty: \n" + wEF.certainty;
    tft.drawString(text,184,Y+8+tft.fontHeight(tft.getFont())*2);
    
    Y=Y+180+4;

    setFont(1);
    text = (String) wEF.event + "\nRecvd: " + String(dateify(WeatherData.lastAlertFetchTime,"mm/dd/yyyy hh:nn:ss")) + "\n" + "Start: " + String(dateify(WeatherData.alertInfo.time_start,"mm/dd/yyyy hh:nn:ss")) + "\n" +
    "End: " + String(dateify(WeatherData.alertInfo.time_end,"mm/dd/yyyy hh:nn:ss")) + "\n" +
    "Description: " + wEF.description;
    tft.drawString(text,0,Y);

    setFont(2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
  
  
  return;
}


void fcnDrawSensorInfo() {
  int16_t boxnum = GRAPHICS.SubScreen_Next;

  if (boxnum < 0 || boxnum >= MAXALARMS || alarms[boxnum] == 255) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Sensor " + String(boxnum) + " not found",tft.width()/2,25);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    return;
  }

//figure out which sensor boxnum equates to    
  ArborysSnsType* sensor = Sensors.snsIndexToPointer(alarms[boxnum]);
  if (!sensor || sensor->IsSet == 0) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED,BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Sensor not found",tft.width()/2,25);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    return;
  }


  int16_t X=0,graphstart=0, graphheight=150,graphwidth=300;
  byte rightmargin=5;
  
  tft.fillRect(0,0,tft.width(),tft.height(),BG_COLOR);
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
  success = retrieveSensorDataFromSD(Sensors.getDeviceMACBySnsIndex(alarms[boxnum]), sensor->snsType, sensor->snsID, &N, t, v, f, 0,endtime,true); //this fills from tn backwards to N*delta samples
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
  tft.printf("Device IP:%s\n",Sensors.getDeviceIPBySnsIndex(alarms[boxnum]).toString().c_str());
  tft.printf("Sns type: %d\n",sensor->snsType);
  tft.printf("Sns ID: %d\n",sensor->snsID);
  tft.printf("Sns name: %s\n",sensor->snsName);
  tft.printf("Sns last logged: %s\n",dateify(sensor->timeLogged,"mm/dd/yyyy hh:nn:ss"));
  tft.printf("Sns flags: %s\n",String(sensor->Flags,BIN).c_str());

  return;
}

void fcnDrawSensorScreen() {
  const char* buttonText[6] = {"Main", "Brdcst", "", "", "", ""};
  byte alarmcount = fcnGetAlarms(0,8,6);

 if (GRAPHICS.SubScreen_Next >0 && GRAPHICS.SubScreen_Next < 100) {    
    fcnDrawNavButtons(buttonText);  
    fcnDrawSensorInfo();    
    return;
  } 
  else if (GRAPHICS.SubScreen_Next == 101) {    
    //go to main
    GRAPHICS.Screen_Next = SCREEN_MAIN;
    GRAPHICS.SubScreen_Next = 0;
    return;
  }
  else if (GRAPHICS.SubScreen_Next == 102) {
    //broadcast my presence
    broadcastServerPresence(true, 2);
    tft.setTextFont(4);
    tft.printf("Broadcast sent\n");
    tft.setTextFont(2);
    tft.printf("You must return to the main screen before retrying a broadcast.\n");

    fcnDrawNavButtons(buttonText);  
    return;
  }


  fcnDrawNavButtons(buttonText);  

  tft.setTextFont(2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  fcnDrawSensors(0,18,8,6);
  tft.setCursor(0,tft.height()-GRAPHICS.buttonWidth-20);
  tft.printf("Sensors (Devices: %d; Sensors: %d; Alarms: %d)",Sensors.numDevices,Sensors.numSensors,alarmcount);


  return;
}

void fcnDrawStatus() {

    
  if (GRAPHICS.SubScreen_Next == 2) {    
    tft.setTextFont(2);
    tft.printf("Are you sure?\n");
    tft.setTextFont(1);
    tft.printf("This will delete stored screen data\n");
    tft.printf("and reset screen defaults.\n");
    tft.printf("The device will reboot.\n");
    tft.printf("This will not delete sensor data.\n");
    tft.printf("Confirm to continue.\n");
    tft.printf("Touch anywhere else to abort.\n");

    const char* altButtonText[6] = {"Confirm","Abort","","","",""};
    fcnDrawNavButtons(altButtonText);  
    return;
  } 
  else if (GRAPHICS.SubScreen_Next == 3) {
    tft.printf("Are you sure?\n");
    tft.setTextFont(1);
    tft.printf("This will delete stored screen data\n");
    tft.printf("and WiFi Settings.\n");
    tft.printf("The device will reboot.\n");
    tft.printf("This will not delete sensor data.\n");
    tft.printf("Confirm to continue.\n");
    tft.printf("Touch anywhere else to abort.\n");

    const char* altButtonText[6] = {"Confirm","Abort","","","",""};
    fcnDrawNavButtons(altButtonText);  
    return;

  }
  else if (GRAPHICS.SubScreen_Next == 101) {
    deleteCoreStruct(); //delete the screen flags file        
    controlledReboot("Reset I",RESET_USER,true); //reset the device
    return;
  }
  else if (GRAPHICS.SubScreen_Next == 102) {
    deleteCoreStruct(); //delete the screen flags file   
    BootSecure bootSecure;
    if (!bootSecure.flushPrefs()) {
      tft.setTextColor(TFT_RED);
      tft.setCursor(0,tft.height()-75);
      tft.println("Failed to reset settings");
      tft.setTextColor(FG_COLOR);
      delay(2000);
    }
    controlledReboot("Reset All",RESET_USER,true); //reset the device
    return;
  }

  const char* buttonText[6] = {"Main","Del Core","Del All","","",""};
  fcnDrawNavButtons(buttonText);  
  
  //draw config screen
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);  
  tft.setTextFont(1);
  tft.println("Status");
  tft.printf("Device Name: %s\n",Prefs.DEVICENAME);
  tft.printf("Device IP:\n");
  tft.printf("Version: %s\n",PROJECT_VER);
  tft.setTextFont(2);
  tft.printf("%s\n",WiFi.localIP().toString().c_str());
  tft.setTextFont(1);
  tft.printf("Report Time: %s\n",(I.currentTime>20000)?dateify(I.currentTime,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Alive Since: %s\n",(I.ALIVESINCE!=0)?dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss"):"???");
  tft.printf("Last Reset Time: %s\n",(I.lastResetTime!=0)?dateify(I.lastResetTime,"mm/dd/yyyy hh:nn:ss"):"???");
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
  tft.printf("Timezone: %ld \n", Prefs.TimeZoneOffset);
  //if DST is enabled, print the DST offset and date of DST start and end
  tft.printf("DST: ");
  if (I.DST==1) tft.printf("Yes, %ld sec\n",I.DSTOffset);
  else if (I.DST==0) tft.print("No\n");
  else tft.print("Not used here\n");
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
    while (cycleByteIndex(SensorIndex,NUMSENSORS,GRAPHICS.alarmIndex) == true && alarmArrayInd<(alarmsToDisplay)) {
      ArborysSnsType* sensor = Sensors.snsIndexToPointer(SensorIndex);
      if (!sensor || sensor->IsSet == 0) continue;
      if (!Sensors.isSensorOfType(sensor,sensorType[snstypeindex])) continue; //only check sensors of this type
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

void fcnDrawClock() {
  //the clock is on screen 0, and is element 6

  //clear the clock area
  GRAPHICS.clearScreenArea(0,6);

  //draw the clock
  int16_t X = GRAPHICS.SCREEN_0[6].X + GRAPHICS.SCREEN_0[6].W/2;
  int16_t Y = GRAPHICS.SCREEN_0[6].Y + GRAPHICS.SCREEN_0[6].H/2;
  uint8_t FNTSZ=8;
  uint32_t FH = setFont(FNTSZ);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  char tempbuf[20];
  snprintf(tempbuf,16,"%s",dateify(I.currentTime,"h1:nn"));
  fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y);

  //reset the clock timer
  GRAPHICS.GRAPHICS_TIMERS.Timers[6] = GRAPHICS.GRAPHICS_TIMERS.Timer0_RESET[6];

  return;
}

void fcnDrawCurrentWeatherText() {
  //clear the current weather text area, which is on screen 0, and is element 3
  GRAPHICS.clearScreenArea(0,3);

  int FNTSZ = 8;
  uint32_t FH = setFont(FNTSZ);
  int16_t X = GRAPHICS.SCREEN_0[3].X + GRAPHICS.SCREEN_0[3].W/2; //middle of the area
  int16_t Y = GRAPHICS.SCREEN_0[3].Y;

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
    Z = 2;
    tft.setCursor(X,Y+Z);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(st,1,X,Y+FH/2);    
    Y=Y+FH+section_spacer;
  }
  

  // draw current temp
  FH = setFont(FNTSZ);
  tft.setTextColor(temp2color(I.currentOutsideTemp),BG_COLOR);
  fcnPrintTxtCenter(I.currentOutsideTemp,FNTSZ,X,Y+FH/2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
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

    fcnPrintTxtCenter((String) I.Tmax + "/" + I.Tmin,FNTSZ,X,Y+FH/2,temp2color(I.Tmax),temp2color(I.Tmin));  
    tft.setTextColor(FG_COLOR,BG_COLOR);

    Y=Y+FH+section_spacer;

  //print pressure info
    uint16_t fg,bg;
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    char tempbuf[15]="";
    fcnPredictionTxt(tempbuf,&fg,&bg);
    tft.setTextColor(fg,bg);
    if (tempbuf[0]!=0) {// prediction made
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+FH/2);
      Y=Y+FH+section_spacer;
    }
    FNTSZ=2;    
    FH = setFont(FNTSZ);
    tempbuf[0]=0;
    fcnPressureTxt(tempbuf,&fg,&bg);

    if (tempbuf[0]!=0) {
      tft.setTextColor(fg,bg);
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+FH/2);
      Y=Y+FH+section_spacer;
    }
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    if (WeatherData.flag_snow) snprintf(tempbuf,14,"Snow: %u%%",WeatherData.getDailyPoP(0));
    else {
      if (WeatherData.flag_ice) snprintf(tempbuf,14,"Ice: %u%%",WeatherData.getDailyPoP(0));
      else snprintf(tempbuf,14,"PoP: %u%%",WeatherData.getDailyPoP(0));
    }

    if (WeatherData.getDailyPoP(0)>30)  tft.setTextColor(tft.color565(255,0,0),tft.color565(0,0,255));
    else tft.setTextColor(FG_COLOR,BG_COLOR);
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
    Z=Z+FH+section_spacer;

  //end current wthr
  I.lastCurrentOutsideTemp = I.currentOutsideTemp;

          //reset the timer

          GRAPHICS.GRAPHICS_TIMERS.Timers[3] = GRAPHICS.GRAPHICS_TIMERS.Timer0_RESET[3];

  
  return;
}

void fcnDrawCurrentWeatherIconOrAlert() {

  //this is element 2 on screen 0

  //if here we are going to draw over the sprite area. clear it.
  tft.fillRect(GRAPHICS.SCREEN_0[2].X,GRAPHICS.SCREEN_0[2].Y,GRAPHICS.SCREEN_0[2].W,GRAPHICS.SCREEN_0[2].H,BG_COLOR);

  //update the FlagsAndAlerts flag
  if (bitRead(I.isFlagged,0) == 1) {
    bitWrite(GRAPHICS.FlagsAndAlerts,0,1); //set bit 0 to 1 - there are flagged sensors
  } else {
    bitWrite(GRAPHICS.FlagsAndAlerts,0,0); //set bit 0 to 0 - there are no flagged sensors
  }

  //update the weather alerts bit
  if (bitRead(I.WeatherEventFlags,0) == 1) {
    bitWrite(GRAPHICS.FlagsAndAlerts,1,1); //set bit 1 to 1 - there are weather alerts
  } else {
    bitWrite(GRAPHICS.FlagsAndAlerts,1,0); //set bit 1 to 0 - there are no weather alerts
  }

  

  //what do I show on the screen?
  //if bit 3 is set OR if bits 0 AND 1 are NOT set, then show icon, no need to think any harder!
  if (bitRead(GRAPHICS.FlagsAndAlerts,3) == 1 || (bitRead(GRAPHICS.FlagsAndAlerts,0) == 0 && bitRead(GRAPHICS.FlagsAndAlerts,1) == 0)) {
    fcnDrawWeatherSprite180(GRAPHICS.SCREEN_0[2].X,GRAPHICS.SCREEN_0[2].Y,GRAPHICS.SPRITE_180x180,false);   
    bitWrite(GRAPHICS.FlagsAndAlerts,3,0); //set bit 3 to 0 - showed icon, so next show something else
  } else {
    //not going to show the icon... so what is next? If alerts are not set OR we didn't show flags last, show flags
    if (bitRead(GRAPHICS.FlagsAndAlerts,1) == 0 || bitRead(GRAPHICS.FlagsAndAlerts,2) == 0) {
      bitWrite(GRAPHICS.FlagsAndAlerts,2,1); //set bit 2 to 1 - showed flags, so next show something else
      if (fcnGetAlarms(-1,3,3)>0) {
        fcnDrawSensors(GRAPHICS.SCREEN_0[2].X,GRAPHICS.SCREEN_0[2].Y,3,3);            
        return;
      }
         
    } else {
      bitWrite(GRAPHICS.FlagsAndAlerts,2,0); //set bit 2 to 0 - showed alerts, so next show something else
      fcnDrawWeatherSprite180(GRAPHICS.SCREEN_0[2].X,GRAPHICS.SCREEN_0[2].Y,GRAPHICS.SPRITE_180x180,true);   
    }
  }

}


bool fcnDrawWeatherSprite180(uint16_t X, uint16_t Y, LGFX_Sprite &sprite, bool useAlerts) {
  //refill the sprite, then push to screen

  bool refilled = false;
  if (sprite.getBuffer() == NULL) return false;

  refilled =fillSprite180(X,Y,sprite);    
  
  sprite.pushSprite(X,Y);
  GRAPHICS.SPRITE_180x180.X = X;
  GRAPHICS.SPRITE_180x180.Y = Y;
  GRAPHICS.SPRITE_180x180.W = 180;
  GRAPHICS.SPRITE_180x180.H = 180;

  //now add text overlays if appropriate.
  uint8_t FNTSZ = 0;
  byte deltaY = setFont(FNTSZ);

  //add info atop icon
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
    deltaY = setFont(FNTSZ);
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextFont(FNTSZ);
    sprite.drawString(tempbuf,5,Y+170-deltaY);    
  }
  return refilled;
}


bool getWeatherAlertDetails(WeatherEventFile* wEF) {
  //get the weather alert details, return false if no alert found

  //get the weather alert
  if (!WeatherData.loadNextWeatherAlert()) {
    return false;
  }

  //get the alert details
  wEF.getEvent(WeatherData.alertInfo.eventnumber);
  

  String description = wEF.description;
  description.replace("\n", " "); //replace newlines with spaces  
  
  //now draw the text

  tft.setTextColor(TFT_RED,BG_COLOR);
  
  int16_t FH = setFont(1);
  tft.drawString((String) "#" + (String) WeatherData.alertInfo.eventnumber + "/" + (String) WeatherData.NumWeatherEvents + ": " + wEF.event, X, Y);  
  Y+=FH*2+2;

  tft.drawString((String) "When: " + (String) dateify(WeatherData.alertInfo.time_start,"mm-dd@hh"), X, Y);
  Y+=FH+2;

  tft.drawString((String) "Severity/Certainty: " + wEF.severity + "/" + wEF.certainty, X, Y);
  Y+=FH+2;

  tft.drawString(description, X, Y);

  tft.setTextColor(FG_COLOR,BG_COLOR);
}

bool fillSprite180(int16_t X, int16_t Y, LGFX_Sprite &sprite, bool useAlerts) {

  char filename[50];

  if (useAlerts) {
    snprintf(filename,50,"/Icons/Events/180x180/%s.bmp",WeatherData.alertInfo.phenomenon);
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
    snprintf(tempbuf,30,"BMP Load Error:\n%s", filename);
    sprite.fillRect(0,0,180,180,BG_COLOR);
    sprite.setTextColor(FG_COLOR,BG_COLOR);
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextFont(2);
    sprite.setTextSize(1);
    sprite.drawString(tempbuf,5,5);    
    return false;
  }

  return true;

}  


void fcnDrawFutureWeather() {
  GRAPHICS.GRAPHICS_TIMERS.Timer_FutureWeather = GRAPHICS.GRAPHICS_TIMERS.Timer_FutureWeather_RESET;
  //this function will draw futuer weather or flags


  byte section_spacer = 3;
  int16_t Y = GRAPHICS.Y_SCRMAIN_HEADER+180+section_spacer;
  int16_t X = tft.width(),Z=0;
  int16_t deltaY = (tft.height()-GRAPHICS.Y_SCRMAIN_CLOCK)-Y-section_spacer+1;

  int16_t FNTSZ = 0;
  char tempbuf[50];

  GRAPHICS.Y_SCRMAIN_HOURLY_START = Y;
  //clear the area
  tft.fillRect(0,Y,X,deltaY,BG_COLOR);



  //choose icon set
  uint16_t iconID = 0;
  int i=0,j=0;


  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,Y);
  X=0;
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
      tft.setTextColor(temp2color(temp));
      fcnPrintTxtCenter(temp,FNTSZ,X,Y+Z+deltaY/2);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      Z+=deltaY+section_spacer;
    }
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Y+=Z;

    GRAPHICS.Y_SCRMAIN_DAILY_START = Y;
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





void updateGraphics() {
  fcnCheckGraphicsTimers();
  checkTouchScreen();
  fcnDrawScreen();
}

void fcnCheckGraphicsTimers() {
  //check if any section needs to be redrawn now. This will be the case if time runs out, if forceRedraw is true, or for the special cases listed below.
  //check the following areas of main screen:
  //header - needs to be redrawn if: flags changed,  event flags changed, or HVAC changed

  if (!GRAPHICS.GRAPHICS_TIMERS.decimateTimers(I.currentTime, I.currentSecond)) return;

  bool flagchanged = (I.isFlagged!=I.wasFlagged);
  bool eventflagchanged = (bitRead(I.WeatherEventFlags,0) != bitRead(I.WeatherEventFlags,1));
  bool hvacchanged = (I.isAC != I.wasAC || I.isFan!=I.wasFan || I.isHeat != I.wasHeat);


  if (GRAPHICS.GRAPHICS_TIMERS.forceRedraw_MAIN == true) {
    GRAPHICS.GRAPHICS_TIMERS.zeroTimers();
    GRAPHICS.GRAPHICS_TIMERS.forceRedraw_MAIN = false;
  }

  //header check
  if (eventflagchanged ||
      flagchanged || 
      hvacchanged) GRAPHICS.GRAPHICS_TIMERS.Timer_Header = 0;


  //current weather - redraw if we need to show flagged sensors or weather alerts, or switch back to icon
  if (bitRead(I.isFlagged,0) == 1 && (GRAPHICS.GRAPHICS_TIMERS.Timer_CurrentWeatherIcon == 0 || GRAPHICS.GRAPHICS_TIMERS.Timer_CurrentWeatherIconAlert == 0)) {
    //if either of these hits zero then they both do.
    GRAPHICS.GRAPHICS_TIMERS.Timer_CurrentWeatherIcon = 0;
    GRAPHICS.GRAPHICS_TIMERS.Timer_CurrentWeatherIconAlert = 0;
  } 

  //current condition
  //just draw if timer is zero
    //future weather
  //just draw if timer is zero
  
  //clock
  //clock timers are handled internally

  //cleanup
  bitWrite(I.WeatherEventFlags,1,bitRead(I.WeatherEventFlags,0)); //set bit 1 to the value of bit 0
  I.wasFlagged = I.isFlagged;
  I.wasAC = I.isAC;
  I.wasFan = I.isFan;
  I.wasHeat = I.isHeat;

  
}

void checkTouchScreen() {
  int32_t tX, tY;
  if (tft.getTouch(&tX, &tY)) {
    GRAPHICS.touchX = tX;
    GRAPHICS.touchY = tY;


    GRAPHICS.GRAPHICS_TIMERS.Timer_ScreenChange = GRAPHICS.GRAPHICS_TIMERS.Timer_ScreenChange_RESET;

    tft.fillCircle(GRAPHICS.touchX,GRAPHICS.touchY,12,TFT_GOLD);

    fcnHandleTouch(); //now Screen_Next, SubScreen_Next, touchX, touchY are updated.

    
    while (tft.getTouch(&tX, &tY)) { delay(10); }

  }
  else {
    GRAPHICS.touchX = -1;
    GRAPHICS.touchY = -1;
  }

}

// Setup display functions
void fcnHandleTouch() {
  //determine what action to take based on the screen number, SubScreen_Next, and touch location
int16_t boxsize6 = tft.width()/6;
int16_t MenuArea = tft.height() - boxsize6;

  switch (GRAPHICS.Screen_Next) {
    case SCREEN_MAIN:
      //what does the user want to do?
      if (GRAPHICS.touchX > 0 && GRAPHICS.touchX < 180 && GRAPHICS.touchY > GRAPHICS.Y_SCRMAIN_HEADER && GRAPHICS.touchY < GRAPHICS.Y_SCRMAIN_HEADER+180) {        
        GRAPHICS.Screen_Next=SCREEN_ALERT;
        GRAPHICS.SubScreen_Next = 0;
      }
      else if (GRAPHICS.touchX > 180 && GRAPHICS.touchX < tft.width() && GRAPHICS.touchY > GRAPHICS.Y_SCRMAIN_HEADER && GRAPHICS.touchY < GRAPHICS.Y_SCRMAIN_HEADER+180) {
        GRAPHICS.Screen_Next=SCREEN_SENSORS;
        GRAPHICS.SubScreen_Next = 0; //overview screen
      }
      else if (GRAPHICS.touchX > 0 && GRAPHICS.touchX < tft.width() && GRAPHICS.touchY > GRAPHICS.Y_SCRMAIN_HOURLY_START && GRAPHICS.touchY < GRAPHICS.Y_SCRMAIN_DAILY_START) {
        GRAPHICS.Screen_Next=SCREEN_HOURLY;
        GRAPHICS.SubScreen_Next = 0;
      }
      else if (GRAPHICS.touchY > GRAPHICS.Y_SCRMAIN_DAILY_START && GRAPHICS.touchY < (tft.height() - GRAPHICS.Y_SCRMAIN_CLOCK)) {
        //determine which day the user touched
        uint16_t day = (int)GRAPHICS.touchX / (tft.width()/5) + 1;
        GRAPHICS.Screen_Next=SCREEN_DAILY;
        GRAPHICS.SubScreen_Next = day;
      }
      else if (GRAPHICS.touchY > (tft.height() - GRAPHICS.Y_SCRMAIN_CLOCK) && GRAPHICS.touchY < tft.height()) {
        GRAPHICS.Screen_Next=SCREEN_STATUS;
        GRAPHICS.SubScreen_Next = 0;
      }
      return;
    case SCREEN_SENSORS:
      //on this screen:
      // if the user touches one of the sensor boxes, then zoom in on that box. If we were already on a zoom in screen, then go back to main.
      //in the bottom menu buttons, 
      //If the user touches the left most box then go back to main. 
      //if the user touches the second box then broadcast my presence
      //if the user touches the third box then 
      //if the user touches the fourth box then 
      //if the user touches the fifth box then 
      //if the user touches the sixth box then 

      GRAPHICS.Screen_Next=SCREEN_SENSORS;
        
      //first, determine if the user touched the sensor area or one of the buttons
      if (GRAPHICS.touchY > MenuArea) {
        //touched a button... stay on this page for now
        GRAPHICS.SubScreen_Next = (GRAPHICS.touchX / boxsize6)+1 + 100;
      } else {
        //touched a sensor box        
        GRAPHICS.SubScreen_Next = ((uint16_t) GRAPHICS.touchX / boxsize6)+1 + ((uint16_t) GRAPHICS.touchY / boxsize6) + 1 -2; //the -2 is to convert 2 one-indexes to zero-indexex
      }

      return;

    case SCREEN_ALERT:
    case SCREEN_DAILY:    
      //first, determine if the user touched the sensor area or one of the buttons
      if (GRAPHICS.touchY > MenuArea) {
        //touched a button... stay on this page for now        
        GRAPHICS.SubScreen_Next = (GRAPHICS.touchX / boxsize6)+1 + 100;
      } else {
        GRAPHICS.Screen_Next=SCREEN_MAIN;
        GRAPHICS.SubScreen_Next = 0;
      }
      return;      
    case SCREEN_HOURLY:
      //to be implemented
      GRAPHICS.Screen_Next=SCREEN_MAIN;
      return;
    case SCREEN_STATUS:
      //to be implemented. For now, just use the currently existing status screen
      GRAPHICS.Screen_Next=SCREEN_STATUS;
      if (GRAPHICS.touchY > MenuArea) {
        //touched a button... this will dictate the subscreen actions based on:
        //main, Delete I, Delete Prefs and I
        //subsequently, requires confirmation.

        uint16_t button = (uint16_t) (GRAPHICS.touchX / boxsize6)+1;
        
        if (GRAPHICS.SubScreen_Now == 0) {
          GRAPHICS.SubScreen_Next = button ;
        }
        else {
          if (button == 1) GRAPHICS.SubScreen_Next = GRAPHICS.SubScreen_Now + 100;
          else {
            GRAPHICS.Screen_Next=SCREEN_MAIN;
            GRAPHICS.SubScreen_Next = 0;
          }
        }

      } else {
        GRAPHICS.Screen_Next=SCREEN_STATUS;
        GRAPHICS.SubScreen_Next = 0;
      }
      return;

    default:  
      GRAPHICS.Screen_Next=SCREEN_MAIN;
      return;
  }
  
}


#endif