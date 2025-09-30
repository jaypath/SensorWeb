#include "graphics.hpp"
#include "globals.hpp"




//for drawing sensors. The maximum number of sensors on a screen is 48.
#define MAXALARMS 48
byte alarms[MAXALARMS] = {255};



LGFX tft;

extern WeatherInfoOptimized WeatherData;
extern double LAST_BAR;
extern STRUCT_PrefsH Prefs;
extern Devices_Sensors Sensors;



// Graphics utility functions
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
  return tft.fontHeight();
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
void drawBmp(const char *filename, int16_t x, int16_t y,  uint16_t alpha) {
  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFile = SD.open(filename, FILE_READ);
  if (!bmpFile) {
    
      SerialPrint("File not found: ",false);
      SerialPrint(filename,true);
    
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;
  uint32_t startTime = millis();

  uint16_t signature = read16(bmpFile);
  if (signature == 0x4D42) {
    read32(bmpFile);
    read32(bmpFile);
    seekOffset = read32(bmpFile);
    read32(bmpFile);
    w = read32(bmpFile);
    h = read32(bmpFile);

    if ((read16(bmpFile) == 1) && (read16(bmpFile) == 24) && (read32(bmpFile) == 0)) {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFile.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        bmpFile.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++) {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
    }
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
  SnsType* sensor = Sensors.getSensorBySnsIndex(sensorIndex);
  
  if (!sensor || sensor->IsSet == 0) {
    return; // Invalid sensor
  }

  box_text = (String) sensor->snsName + (String) "_";

  if (sensor->expired) {
    box_text += "EXP!_";
    box_border = set_color(150,150,150);
    box_fill = set_color(200,200,200);
    text_color = set_color(55,55,55);
  } else {

    //get sensor vals 1, 4, 3, 70, 61... if any are flagged then set box color
    byte snstype = sensor->snsType;
    if (snstype==1 || snstype==4 || snstype==10 || snstype==14 || snstype==17) {
      //temperature
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
    if (snstype==3) {
      //soil
      box_text += (String) ((int) sensor->snsValue) + "_";

      if (bitRead(sensor->Flags,0)==1) {
        if (bitRead(sensor->Flags,5)==1) { //too dry
          box_border = set_color(150,20,20);
          box_fill = set_color(250,170,100);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
        else { //too wet
          box_border = set_color(111, 15, 46);
          box_fill = set_color(11, 15, 46);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      } else { //just right
        box_border = set_color(100, 70, 20);
        box_fill = set_color(100, 70, 20);
        text_color = convertColor565ToGrayscale(invert_color(box_fill));
      }
    }

    if (snstype==2 || snstype==5 || snstype==15 || snstype==18) {
      //humidity
      box_text += (String) ((int) sensor->snsValue) + "%RH_";
      if (bitRead(sensor->Flags,0)==1) {
        if (bitRead(sensor->Flags,5)==0) { //too dry
          box_border = set_color(201, 216, 221);
          box_fill = set_color(205, 235, 250);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }        else { //too wet
          box_border = set_color(201, 216, 221);
          box_fill = set_color(70, 168, 179);
          text_color = convertColor565ToGrayscale(invert_color(box_fill));
        }
      } else {
        box_border = set_color(201, 216, 221);
        box_fill = set_color(155, 211, 229);
        text_color = convertColor565ToGrayscale(invert_color(box_fill));
      }
    }

    if (snstype==58) {
      //leak
      box_text += "LEAK_";
      box_border = set_color(0,0,255);
      box_fill = set_color(0,190,255);
      text_color = set_color(255-0,255-190,255-255);
    }

    if (snstype==61) {
      //bat
      box_text += (String) ((int) sensor->snsValue) + "%_";
      box_border = set_color(200,200,200);
      if (bitRead(sensor->Flags,0)==1) {        
        box_fill = set_color(228, 70, 110);
        text_color = set_color(0,0,0);
      } else {
        box_fill = set_color(150,150,255);
        text_color = set_color(0,0,0);
      }
    }


    if (snstype==9 || snstype==13 || snstype==19) {
      //air pressure
      box_border = set_color(192, 222, 233);
      box_text += (String) ((int) sensor->snsValue) + "hPA_";
      if (bitRead(sensor->Flags,0)==1) {
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
  //redraw header every  [flagged change, isheat, iscool] changed  or every new hour
  if (((I.isFlagged!=I.wasFlagged || I.isAC != I.wasAC || I.isFan!=I.wasFan || I.isHeat != I.wasHeat) ) || I.lastHeaderTime+I.cycleHeaderMinutes*60<I.currentTime)     {
    I.lastHeaderTime = I.currentTime;
    if (I.wasFlagged != I.isFlagged) I.lastFlagViewTime = 0; //set the flag view time to 0 so we can show flags
    I.wasFlagged = I.isFlagged;
    I.wasAC = I.isAC;
    I.wasFan = I.isFan;
    I.wasHeat = I.isHeat;
  }   else return;


  tft.fillRect(0,0,tft.width(),I.HEADER_Y,BG_COLOR); //clear the header area

  //draw header    
  String st = "";
  uint16_t x = 0,y=0;
  uint32_t FH = setFont(4);
  tft.setCursor(0,0);
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  
  tft.setTextDatum(TL_DATUM);
  st = dateify(I.currentTime,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;
  

  y=4;
  tft.setTextDatum(TL_DATUM);
  FH = setFont(2);


  if (I.isFlagged) {
    tft.setTextFont(2);
    tft.setTextColor(tft.color565(255,0,0),BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y);
    tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  }
  x += tft.textWidth("FLAG")+10;

  setFont(0);
  fcnPrintTxtHeatingCooling(x,5);
  st = "XX XX XX "; //placeholder to find new X position
  x += tft.textWidth(st)+4;
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background


//add heat and AC icons to right 
  if ((I.isHeat&1)==1) {
    st="/HVAC/flame.bmp";
    drawBmp(st.c_str(),tft.width()-30,0);
  }
  if ((I.isAC&1)==1) {
    st="/HVAC/ac30.bmp";
    drawBmp(st.c_str(),tft.width()-65,0);
  }
   
  tft.setTextColor(FG_COLOR,BG_COLOR); //without second arg it is transparent background
  

  FH = setFont(2);

  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, FG_COLOR);

  return;
}

void fcnDrawScreen() {
  
  if(WiFi.status()!= WL_CONNECTED){  
    screenWiFiDown();
  }

  if (I.ScreenNum==0 ) {    
    if (I.oldScreenNum!=I.ScreenNum) { //switched from another screen 
      I.lastHeaderTime=0; //last time header was drawn
      I.lastWeatherTime=0; //last time weather was drawn
      I.lastCurrentConditionTime=0; //last time current condition was drawn
      I.lastClockDrawTime=0; //last time clock was updated, whether flag or not
      I.lastFutureConditionTime=0; //last time future condition was drawn      
      I.ScreenNum=0;
      I.oldScreenNum = I.ScreenNum;
      tft.fillScreen(BG_COLOR);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      tft.setCursor(0,0);
    }
    fcnDrawClock(); 
    fcnDrawHeader();
    fncDrawCurrentCondition();
    fcnDrawCurrentWeather();
    fcnDrawFutureWeather();
  } else {
    if (I.ScreenNum==1 && I.oldScreenNum!=I.ScreenNum) {
      fcnDrawSensorScreen();
        I.oldScreenNum = I.ScreenNum;
    } else {
      if ((I.ScreenNum==2 || I.ScreenNum==3) && I.oldScreenNum!=I.ScreenNum) { //draw config or confirm
        fcnDrawStatus();
        I.oldScreenNum = I.ScreenNum;
      } else {
        if (I.ScreenNum>3 && I.oldScreenNum!=I.ScreenNum) { //draw sensor info
          fcnDrawSensorInfo();
          I.oldScreenNum = I.ScreenNum;
        }
      }
    }
  }
  
  return;
}


void fcnDrawSensorInfo() {
  byte boxnum = I.ScreenNum-10;
  if (boxnum >= 0 && boxnum < MAXALARMS && alarms[boxnum] != 255) {
//figure out which sensor boxnum equates to    
    SnsType* sensor = Sensors.getSensorBySnsIndex(alarms[boxnum]);
    if (sensor && sensor->IsSet ) {


    int16_t X=0,graphstart=0, graphheight=150,graphwidth=300;
   byte rightmargin=5;
  
    tft.fillRect(0,0,tft.width(),tft.height(),BG_COLOR);
    tft.setTextFont(2);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    tft.setCursor(0,0);
    tft.printf("Sensor Info for sensor %s",sensor->snsName);
    graphstart+=tft.fontHeight()+4;

    //draw an xy graph of the historical sensor data

    //get the historical sensor data
    
    byte N=100;
    uint32_t  endtime=-1;
    
    uint32_t t[N]={0};
    double v[N]={0};
    uint8_t f[N]={0};
    
    bool success=false;
    success = retrieveSensorDataFromSD(Sensors.getDeviceMACBySnsIndex(alarms[boxnum]), sensor->snsType, sensor->snsID, &N, t, v, f, 0,endtime,true); //this fills from tn backwards to N*delta samples
    SerialPrint(String("Success: ") + success + " N: " + N + "\n");
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
      tft.setCursor(0,graphstart+graphheight-tft.fontHeight()-4);
      tft.printf("%.0f",minv);
      //draw the x axis labels
      tft.setCursor(tft.width()-graphwidth,graphstart+graphheight+tft.fontHeight()+4);
      tft.printf("%s",dateify(mint,"mm/dd/yyyy hh:nn:ss"));
      tft.setCursor(tft.width()-tft.textWidth("mm/dd/yyyy hh:nn:ss"),graphstart+graphheight+tft.fontHeight()+4);
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
    tft.setCursor(0,graphstart+graphheight+tft.fontHeight()*2);
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
    tft.printf("Device IP: %s\n",IPToString(Sensors.getDeviceIPBySnsIndex(alarms[boxnum])).c_str());
    tft.printf("Sns type: %d\n",sensor->snsType);
    tft.printf("Sns ID: %d\n",sensor->snsID);
    tft.printf("Sns name: %s\n",sensor->snsName);
    tft.printf("Sns last logged: %s\n",dateify(sensor->timeLogged,"mm/dd/yyyy hh:nn:ss"));


    } else tft.printf("Sensor not set");
    } else {
      tft.setTextFont(2);
      tft.setTextColor(TFT_RED,BG_COLOR);
      tft.setCursor(tft.width()-150,tft.height()-25);
      tft.printf("Sensor not found");
      tft.setTextColor(FG_COLOR,BG_COLOR);
    }


  return;
}


void fcnDrawSensorScreen() {
        //on screen 1, draw the sensors. Touching a sensor box will move to screen 3, where that sensor's info will be displayed
      //touching the bottom right corner will move to screen 2, where the status will be displayed
      tft.fillRect(0,0,tft.width(),tft.height(),BG_COLOR);
      tft.setTextFont(2);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      tft.setCursor(0,0);
      tft.printf("Sensors (Devices: %d; Sensors: %d)",Sensors.numDevices,Sensors.numSensors);
      fcnDrawSensors(0,18,8,6,0);

      //draw a box at the bottom right corner of screen, that when pushed moves to next previous screen
      tft.fillRoundRect(0,tft.height()-50,50,50,10,TFT_LIGHTGREY);
      tft.setTextColor(BG_COLOR,TFT_LIGHTGREY);
      tft.setCursor(25,tft.height()-25);
      fcnPrintTxtCenter("Previous",1,25,tft.height()-25);

      tft.fillRoundRect(54,tft.height()-50,50,50,10,TFT_LIGHTGREY);
      tft.setTextColor(BG_COLOR,TFT_LIGHTGREY);
      tft.setCursor(54+25,tft.height()-25);
      fcnPrintTxtCenter("Next",1,54+25,tft.height()-25);

  return;
}

void fcnDrawStatus() {
  //draw config screen
  tft.fillRect(0,0,tft.width(),tft.height(),BG_COLOR);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);  
  if (I.ScreenNum==2) {
    tft.setTextFont(1);
    tft.println("Status");
    tft.printf("Report Time: %s\n",(I.currentTime>20000)?dateify(I.currentTime,"mm/dd/yyyy hh:nn:ss"):"???");
    tft.printf("Alive Since: %s\n",(I.ALIVESINCE!=0)?dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss"):"???");
    tft.printf("Last Reset Time: %s\n",(I.lastResetTime!=0)?dateify(I.lastResetTime,"mm/dd/yyyy hh:nn:ss"):"???");
    tft.printf("Last LAN Msg Time: %s\n",(I.lastESPNOW_TIME!=0)?dateify(I.lastESPNOW_TIME,"mm/dd/yyyy hh:nn:ss"):"???");
    tft.printf("Last LAN Msg State: %s\n",(I.lastESPNOW_STATE==2)?"Receive Success":((I.lastESPNOW_STATE==1)?"Send Success":((I.lastESPNOW_STATE==0)?"Indeterminate":((I.lastESPNOW_STATE==-1)?"Send Fail":((I.lastESPNOW_STATE==-2)?"Receive Fail": "Unknown")))));
    tft.printf("Last Error Time: %s\n",(I.lastErrorTime!=0)?dateify(I.lastErrorTime,"mm/dd/yyyy hh:nn:ss"):"???");
    tft.printf("Last Error: %s\n",I.lastError);
    tft.printf("Wifi fail count : %d\n",I.wifiFailCount);
    tft.printf("Reboots since last: %d\n",I.rebootsSinceLast);
    tft.printf("DST Offset: %d\n",I.DSTOFFSET);
    tft.printf("Local Weather Index: %d\n",I.localWeatherIndex);
    tft.printf("Local Battery Index: %d\n",I.localBatteryIndex);
    tft.printf("Local Battery Level: %d\n",I.localBatteryLevel);
    tft.printf("Local Weather Index: %d\n",I.localWeatherIndex);

    tft.fillRoundRect(0,tft.height()-50,50,50,10,TFT_LIGHTGREY);
    tft.setTextColor(BG_COLOR,TFT_LIGHTGREY);
    tft.setCursor(25,tft.height()-25);
    fcnPrintTxtCenter("Reset I",1,25,tft.height()-25);
  } else {
    //draw confirm screen

    tft.setTextFont(2);
    tft.printf("Are you sure?\n");
    tft.setTextFont(1);
    tft.printf("This will delete stored screen data\n");
    tft.printf("and reset screen defaults.\n");
    tft.printf("The device will reboot.\n");
    tft.printf("This will not affect sensor data.\n");
    tft.printf("Touch CONFIRM to continue.\n");
    tft.printf("Touch anywhere else to abort.\n");

    tft.fillRoundRect(0,tft.height()-50,50,50,10,TFT_LIGHTGREY);
    tft.setTextColor(BG_COLOR,TFT_LIGHTGREY);
    tft.setCursor(25,tft.height()-25);
    fcnPrintTxtCenter("Confirm",1,25,tft.height()-25);
  }

}

void fcnDrawSensors(int X,int Y, uint8_t rows, uint8_t cols, int32_t whichSensors) { //set whichsensors to -1 to show I.showTheseFlags
  /*
  going to show rowsxcols flagged sensors (so up to rows*cols)
  starting at X,Y

  note that the alarms array will be filled up to MAXALARMS sensors, but we may only be able to display rows*cols of these. In that case only fill alarms array up to rows*cols
*/


  int16_t init_X = X;
  if (rows==0) rows = 2;
  if (cols==0) cols = 6;

  
  byte alarmsToDisplay = rows*cols;
  if (alarmsToDisplay>MAXALARMS) alarmsToDisplay = MAXALARMS;


  byte boxsize_x=50,boxsize_y=50;
  byte gapx = 4;
  byte gapy = 2;

  String roomname;

  //fill an array with next row*cols alarm entries
//  byte alarms[rows*cols];
  byte alarmArrayInd = 0;
  
//init alarms
  for (byte k = 0;k<(MAXALARMS);k++) {
    alarms[k] = 255;
  }
  
//fill up to alarmsToDisplay alarm spots, and remember where we left off. Also, fill each sensor type before moving to the next sensor type
  byte SensorIndex = I.alarmIndex;

  if (whichSensors == -1) whichSensors = I.showTheseFlags;

  String sensorType[10] = {"leak", "soil", "temperature", "humidity", "pressure", "battery", "HVAC", "human", "server","all"};
  //0 = leak, 1 = soil dry, 2 = temperature, 3 = humidity, 4 = pressure, 5 = battery, 6 = HVAC, 7 = human, 8 = server, 9 = anything else
  for (byte snstypeindex = 0; snstypeindex<10; snstypeindex++) {
    
    while (cycleByteIndex(&SensorIndex,NUMSENSORS,I.alarmIndex) == true && alarmArrayInd<(alarmsToDisplay)) {
      SnsType* sensor = Sensors.getSensorBySnsIndex(SensorIndex);
      if (!sensor || sensor->IsSet == 0) continue;
      bool isgood = false;

      //this check is not redundant, as this checks for what is allowed, as the previous check puts sensors in order of sensor type
      if (Sensors.isSensorOfType(SensorIndex,sensorType[snstypeindex])) {
        if (bitRead(whichSensors, 0) == 0) { //any flag
          isgood = true;
        } else { //must be flagged
          if (bitRead(sensor->Flags, 0) == 0) continue; //not flagged, exclude
          if (bitRead(whichSensors, 1) == 1 && (sensor->expired )) isgood = true; //expired so include
          if (bitRead(whichSensors, 2) == 1 && Sensors.isSensorOfType(SensorIndex,"soil")) isgood = true;
          if (bitRead(whichSensors, 3) == 1 && Sensors.isSensorOfType(SensorIndex,"leak")) isgood = true;
          if (bitRead(whichSensors, 4) == 1 && Sensors.isSensorOfType(SensorIndex,"temperature")) isgood = true;
          if (bitRead(whichSensors, 5) == 1 && Sensors.isSensorOfType(SensorIndex,"humidity")) isgood = true;
          if (bitRead(whichSensors, 6) == 1 && Sensors.isSensorOfType(SensorIndex,"pressure")) isgood = true;
          if (bitRead(whichSensors, 7) == 1 && Sensors.isSensorOfType(SensorIndex,"battery")) isgood = true;
          if (bitRead(whichSensors, 8) == 1 && Sensors.isSensorOfType(SensorIndex,"HVAC")) isgood = true;
        }
        //if bit 0 is 0 then include all sensors
        //if bit 0 is set then include flagged 
        //if bit 1 is set the include expired (overrides bit 0)
        //  if bits 0 or 1 are set then include sensors if they meet criteria of subsequent bits
        //aggregate sensors in this order: leak, soil dry, temperature, humidity, pressure, battery, HVAC
      }

      if (isgood && inArrayBytes(alarms,alarmsToDisplay,SensorIndex,false) == -1) alarms[alarmArrayInd++] = SensorIndex;          //only include if not already in array
      
    }      
  } 
  I.alarmIndex = SensorIndex;
  
  alarmArrayInd=0;
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
  
  if (I.lastClockDrawTime==0 || (I.currentTime != I.lastClockDrawTime &&  second(I.currentTime)==0) || I.currentMinute != minute(I.currentTime)) {
    I.lastClockDrawTime = I.currentTime;
    I.currentMinute = minute(I.currentTime);

    int16_t X = tft.width();
    uint8_t FNTSZ=8;
    uint32_t FH = setFont(FNTSZ);
    tft.setTextColor(FG_COLOR,BG_COLOR);
    char tempbuf[20];
    int Y = tft.height() - I.CLOCK_Y;
    tft.fillRect(0,Y,X,I.CLOCK_Y,BG_COLOR); //clear the  clock area
    snprintf(tempbuf,16,"%s",dateify(I.currentTime,"h1:nn"));
    fcnPrintTxtCenter((String) tempbuf,FNTSZ, X/2,Y+I.CLOCK_Y/2);
  }


  return;
}

void fncDrawCurrentCondition() {

  //redraw current condition if 
  //the last current temp does not match the current temp
  //the current condition has not been updated in currentConditionInterval min

  if ((uint32_t) (I.lastCurrentConditionTime!=0 && I.lastCurrentConditionTime + (uint32_t) I.cycleCurrentConditionMinutes*60 > I.currentTime)) {
    return;
  }

  I.lastCurrentConditionTime = I.currentTime;
  
  int FNTSZ = 8;
  uint32_t FH = setFont(FNTSZ);
  int X = 180+(tft.width()-180)/2; //middle of area on side of icon
  int Z = 10;
  int Y = I.HEADER_Y;

  tft.fillRect(180,Y,tft.width()-180,180,BG_COLOR); //clear the cc area

  String st = "";
  byte section_spacer = 3;


  //see if we have local weather
  if (I.localWeatherIndex<255) {
    SnsType* sensor = Sensors.getSensorBySnsIndex(I.localWeatherIndex);
    if (sensor && sensor->IsSet ) { 
      st = "Local@" + (String) dateify(sensor->timeLogged,"h1:nn");
      if (I.localBatteryIndex<255)     st += " Bat" + (String) (I.localBatteryLevel) + "%";
    
      FH = setFont(1);
      Z = 2;
      tft.setCursor(X,Y+Z);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      fcnPrintTxtCenter(st,1,X,Y+Z+FH/2);
      //Y+=FH+Z;
    }
  }
  
  // draw current temp
  FH = setFont(FNTSZ);
  X = 180+(tft.width()-180)/2; //middle of area on side of icon
  Z = 10;
  tft.setTextColor(temp2color(I.currentTemp),BG_COLOR);
  fcnPrintTxtCenter(I.currentTemp,FNTSZ,X,Y+Z+FH/2);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  Z+=FH+section_spacer;

  //print today max / min
    FNTSZ = 4;  
    FH = setFont(FNTSZ);
    int8_t tempMaxmin[2];
    WeatherData.getDailyTemp(0,tempMaxmin);
    if (tempMaxmin[0] == -125) tempMaxmin[0] = WeatherData.getTemperature(I.currentTime);
    //does local max/min trump reported?
    if (tempMaxmin[0]<I.currentTemp) tempMaxmin[0]=I.currentTemp;
    if (tempMaxmin[1]>I.currentTemp) tempMaxmin[1]=I.currentTemp;
    I.Tmax = tempMaxmin[0];
    I.Tmin = tempMaxmin[1];

    fcnPrintTxtCenter((String) I.Tmax + "/" + I.Tmin,FNTSZ,X,Y+Z+FH/2,temp2color(I.Tmax),temp2color(I.Tmin));  
    tft.setTextColor(FG_COLOR,BG_COLOR);

    Z=Z+FH+section_spacer;

  //print pressure info
    uint16_t fg,bg;
    FNTSZ = 4;
    FH = setFont(FNTSZ);
    char tempbuf[15]="";
    fcnPredictionTxt(tempbuf,&fg,&bg);
    tft.setTextColor(fg,bg);
    if (tempbuf[0]!=0) {// prediction made
      fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
      Z=Z+FH+section_spacer;
      FNTSZ=2;
    }
    
    FH = setFont(FNTSZ);
    tempbuf[0]=0;
    fcnPressureTxt(tempbuf,&fg,&bg);
    tft.setTextColor(fg,bg);
    fcnPrintTxtCenter(tempbuf,FNTSZ,X,Y+Z+FH/2);
    Z=Z+FH+section_spacer;

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
  I.lastCurrentTemp = I.currentTemp;
  
  return;
}

void fcnDrawCurrentWeather() {
//this function will draw the current weather image and any flags that are flagged
bool forceweather = false;
bool forceflags = false;


if (I.lastWeatherTime==0) forceweather=true;
else {
  //are we flagged?
  if (I.isFlagged>0) {
    //but not all flags matter - depends on which are flagged and when
    if (Sensors.countFlagged(-1000,0b00000011,0b00000011,I.currentTime-86400)>0) {
      //ok, critical sensors are flagged, were we already showing flags?

      if (I.lastFlagViewTime==0) forceflags=true;
      else {
        if (I.lastFlagViewTime < I.lastWeatherTime) { //we are showing weather, not flags
          if (I.lastWeatherTime + I.cycleFlagSeconds<I.currentTime) forceflags=true; //showed weather for at least the cycle time and flags are due)
          else return; //not time to do anything yet
        } else {
          //we are showing flags, not weather
          if (I.lastFlagViewTime + I.cycleFlagSeconds<I.currentTime) forceweather=true; //showed flags for at least the cycle time and weather is due)
          else return; //not time to do anything yet
        }
      }       
    } else {
      // No critical flagged sensors, show weather if flags were previously shown
      if (I.lastFlagViewTime > 0 && I.lastFlagViewTime >= I.lastWeatherTime) {
        forceweather = true;
      }
    }
  } else {
    // No flags at all, show weather if flags were previously shown
    if (I.lastFlagViewTime > 0 && I.lastFlagViewTime >= I.lastWeatherTime) {
      forceweather = true;
    }
  }
}

if (forceflags) {
  I.lastFlagViewTime = I.currentTime;
  tft.fillRect(0,I.HEADER_Y,180,180,BG_COLOR); //clear the bmp area
  fcnDrawSensors(8,I.HEADER_Y,3,3,-1);      
  return;
}

//do I draw  weather?
if (forceweather==false && (uint32_t) I.lastWeatherTime>0 && I.lastWeatherTime+I.cycleWeatherMinutes*60>I.currentTime)   return; //not time to show  weather and no need to redraw

I.lastWeatherTime = I.currentTime;

  int X=0,Y = I.HEADER_Y,Z=0; //header ends at 30

  tft.fillRect(0,I.HEADER_Y,180,180,BG_COLOR); //clear the bmp area


  int i=0,j=0;
  char tempbuf[45];
  uint8_t FNTSZ = 0;

  byte deltaY = setFont(FNTSZ);

  byte section_spacer = 3;

  //draw icon for NOW
  int iconID = WeatherData.getWeatherID(0);
  
  if (I.currentTime > WeatherData.sunrise  && I.currentTime< WeatherData.sunset) snprintf(tempbuf,44,"/BMP180x180day/%d.bmp", iconID); //the listed sunrise is for this day, so might be in the past. If it is in the past, we are in the day if sunset is in the future, otherwise in the night.
  else     snprintf(tempbuf,44,"/BMP180x180night/%d.bmp",iconID);

  drawBmp(tempbuf,0,Y);

  //add info atop icon
  if (WeatherData.getPoP() > 50) {//greater than 50% cumulative precip prob in next 24 h
    uint32_t Next_Precip = WeatherData.nextPrecip();
    uint32_t nextrain = WeatherData.nextRain();
    double rain =  WeatherData.getRain() * 0.0393701; //to inches
    uint32_t nextsnow = WeatherData.nextSnow();
    double snow =  (WeatherData.getSnow() +   WeatherData.getIce())*0.0393701;
    if (nextsnow < I.currentTime + 86400 && snow > 0) {
        tft.setTextColor(tft.color565(255,0,0),tft.color565(255,0,0));
        snprintf(tempbuf,30,"Snow %.1f\"",dateify(nextsnow,"hh"),snow);
        FNTSZ=4;      
    } else {
      if (rain>0 && nextrain< I.currentTime + 86400 ) {
        tft.setTextColor(tft.color565(255,255,0),tft.color565(0,0,255));
        snprintf(tempbuf,30,"Rain@%s",dateify(Next_Precip,"hh:nn"));
        FNTSZ=2;
      }
    }
    deltaY = setFont(FNTSZ);
    fcnPrintTxtCenter(tempbuf,FNTSZ,5,Y+170-deltaY);
  }
  return;

}  


void fcnDrawFutureWeather() {
  
  //this function will draw futuer weather or flags


  bool forcedraw = false;

  //do I draw future weather?
  if (forcedraw==false && (uint32_t) I.lastFutureConditionTime>0 && I.lastFutureConditionTime+I.cycleFutureConditionsMinutes*60>I.currentTime)   return; //not time to show future weather
  

  I.lastFutureConditionTime = I.currentTime;


  byte section_spacer = 3;
  int16_t Y = I.HEADER_Y+180+section_spacer;
  int16_t X = tft.width(),Z=0;
  int16_t deltaY = (tft.height()-I.CLOCK_Y)-Y-section_spacer+1;

  //clear the area
  tft.fillRect(0,Y,X,deltaY,BG_COLOR);


  char tempbuf[45];
  uint8_t FNTSZ = 0;
  int iconID = 0;
  int i=0,j=0;


  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,Y);
  X=0;
    for (i=1;i<7;i++) { //draw 6 icons, with I.HourlyInterval hours between icons. Note that index 1 is  1 hour from now
      if (i*I.IntervalHourlyWeather>72) break; //safety check if user asked for too large an interval ... cannot display past download limit
      Z=0;
      X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
      iconID = WeatherData.getWeatherID(I.currentTime+i*I.IntervalHourlyWeather*60*60);
      uint32_t temptime = I.currentTime+i*I.IntervalHourlyWeather*3600;
      if ((temptime > WeatherData.sunrise  && temptime < WeatherData.sunset) || temptime > WeatherData.sunrise + 86400) snprintf(tempbuf,29,"/BMP30x30day/%d.bmp",iconID); //the listed sunrise is for this day, so might be in the past. If it is in the past, we are in the day if sunset is in the future, otherwise in the night. But if we are past the prior sunrise +24h we are in the NEXT day
      else snprintf(tempbuf,29,"/BMP30x30night/%d.bmp",iconID);
      
      drawBmp(tempbuf,X,Y);
      Z+=30;

      tft.setTextColor(FG_COLOR,BG_COLOR);
      X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

      FNTSZ=1;
      deltaY = setFont(FNTSZ);
      snprintf(tempbuf,6,"%s:00",dateify(I.currentTime+i*I.IntervalHourlyWeather*60*60,"hh"));
      tft.setTextFont(FNTSZ); //small font
      fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);
      Z+=deltaY+section_spacer;
      tft.setTextDatum(TL_DATUM);
      FNTSZ=4;
      deltaY = setFont(FNTSZ);
      tft.setTextFont(FNTSZ); //med font
      byte temp = WeatherData.getTemperature(I.currentTime + i*I.IntervalHourlyWeather*3600);
      tft.setTextColor(temp2color(temp));
      fcnPrintTxtCenter(temp,FNTSZ,X,Y+Z+deltaY/2);
      tft.setTextColor(FG_COLOR,BG_COLOR);
      Z+=deltaY+section_spacer;
    }
    tft.setTextColor(FG_COLOR,BG_COLOR);
    Y+=Z;


  //now draw daily weather
    tft.setCursor(0,Y);
    for (i=1;i<6;i++) {
      int8_t tmm[2];
      Z=0;
      X = (i-1)*(tft.width()/5) + ((tft.width()/5)-60)/2; 
      iconID = WeatherData.getDailyWeatherID(i,true);
      snprintf(tempbuf,29,"/BMP60x60day/%d.bmp",iconID); //alway print day icons for future days

      drawBmp(tempbuf,X,Y);
      Z+=60;
      
      X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
      FNTSZ=2;
      deltaY = setFont(FNTSZ);
      snprintf(tempbuf,31,"%s",dateify(I.currentTime+i*60*60*24,"DOW"));
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

  if (snsIndex !=255) {
    // Find the sensor by its index and get its value
    DevType* device = Sensors.getDeviceBySnsIndex(snsIndex);
    SnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
    
    if (device && sensor && device->IsSet && sensor->IsSet) {
      tempval = sensor->snsValue;
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
    
    DevType* device = Sensors.getDeviceBySnsIndex(snsIndex);
    SnsType* sensor = Sensors.getSensorBySnsIndex(snsIndex);
    
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


// Setup display functions
void displaySetupProgress(bool success) {
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.println("OK.\n");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  } else {
    tft.setTextColor(TFT_RED);
    tft.println("FAIL");
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
}

void displayWiFiStatus(byte retries, bool success) {
  if (success) {
    tft.setTextColor(TFT_GREEN);
    tft.printf("Wifi ok, %u attempts.\nWifi IP is %s\nMAC is %s\n",retries,IPToString(Prefs.MYIP).c_str(),MACToString(Prefs.PROCID).c_str());
    tft.setTextColor(FG_COLOR);
  } else {
    tft.printf("Wifi FAILED %d attempts - reboot in 120s",retries);
  }
}

void displayOTAProgress(unsigned int progress, unsigned int total) {
  tft.fillScreen(BG_COLOR);            // Clear screen
  tft.setTextFont(2);
  tft.setCursor(0,0);
  tft.println("OTA started, receiving data.");
}

void displayOTAError(int error) {
  tft.setCursor(0,0);
  tft.print("OTA error: ");
  tft.println(error);
} 

void screenWiFiDown() {
  tft.clear();
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_RED);
  tft.printf("WiFi may be down.\nAP mode enabled.\nConnect to: %s\nIP: 192.168.4.1\nif wifi credentials need to be updated.\nRebooting in 5 min...", WiFi.softAPSSID().c_str());
}

void checkTouchScreen() {
  tft.getTouch(&I.touchX, &I.touchY);
  
  if (I.touchX > 0 && I.touchY > 0) {
    tft.fillCircle(I.touchX,I.touchY,12,TFT_GOLD);

    switch (I.ScreenNum) {
      case 0:
        I.screenChangeTimer = 30;
        I.ScreenNum=1;
        break; //do nothing
      case 1:
        if ( I.touchY > tft.height()-50 && I.touchY < tft.height()) {
          if(I.touchX > 0 && I.touchX < 52)           I.ScreenNum=0;
          else if(I.touchX >= 52 && I.touchX < 104) I.ScreenNum=2;
        } 
        else {
          if (I.touchX > 0 && I.touchX < tft.width()) {
            //There are 8 rows and 6 cols, so MAXALARMS boxes
            //figure out the box number, starting from upper left and moving right and then down.
            byte boxnum = (I.touchX/54) + (I.touchY/54)*6;
            I.ScreenNum=10+boxnum;
          }
        }
        break;
      case 2:
        if (I.touchX < 50 && I.touchY > tft.height()-50 && I.touchY < tft.height()) {
          I.screenChangeTimer = 30;
          I.ScreenNum=3; //confirm deletion screen
        } else {
          I.screenChangeTimer = 30;
          I.ScreenNum=0;
        }
        break; 
      case 3:
        if (I.touchX < 50 && I.touchY > tft.height()-50 && I.touchY < tft.height()) {
          //touched a reset button
          deleteCoreStruct(); //delete the screen flags file        
          controlledReboot("Reset I",RESET_USER,true); //reset the device
        } else {
          I.screenChangeTimer = 30;
          I.ScreenNum=0;
        }
        break; //do nothing
      default:
        //displaying sensor info. any touch on this screen will return to sensor screen
        I.screenChangeTimer = 30;
        I.ScreenNum=1;
        break; 

    }

    fcnDrawScreen();
    delay(150);
 

  }

  I.touchX = 0;
  I.touchY = 0;

}