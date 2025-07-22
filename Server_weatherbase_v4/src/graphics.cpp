#include "graphics.hpp"
#include "globals.hpp"




LGFX tft;

extern WeatherInfoOptimized WeatherData;
extern double LAST_BAR;
extern STRUCT_PrefsH Prefs;
extern Devices_Sensors Sensors;



// Graphics utility functions
uint16_t set_color(byte r, byte g, byte b) {
  return tft.color565(r, g, b);
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
    #ifdef _DEBUG
      Serial.print("File not found: ");
      Serial.println(filename);
    #endif
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
      box_text += (String) ((int) sensor->snsValue) + "F_";
      if (bitRead(sensor->Flags,5)==1) {        
        box_border = set_color(150,20,20);
        box_fill = set_color(255,100,100);
        text_color = set_color(255-255,255-100,255-100);
      }
      else {
        box_border = set_color(20,20,150);
        box_fill = set_color(150,150,255);
        text_color = set_color(255-150,255-150,255-255);
      }

    }
    if (snstype==3) {
      //soil
      box_text += (String) ((int) sensor->snsValue) + "_";
      box_border = set_color(65,45,20);
      box_fill = set_color(250,170,100);
      text_color = set_color(255-250,255-170,255-100);
    }

    if (snstype==2 || snstype==5 || snstype==15 || snstype==18) {
      //humidity
      box_text += (String) ((int) sensor->snsValue) + "%RH_";
      if (bitRead(sensor->Flags,5)==0) {
        box_border = set_color(65,45,20);
        box_fill = set_color(250,170,100);
        text_color = set_color(255-250,255-170,255-100);
      }
      else {
        box_border = set_color(0,0,255);
        box_fill = set_color(0,190,255);
        text_color = set_color(255-0,255-190,255-255);
      }
    }

    if (snstype==58) {
      //leak
      box_text += "LEAK_";
      box_border = set_color(0,0,255);
      box_fill = set_color(0,190,255);
      text_color = set_color(255-0,255-190,255-255);
    }

    if (snstype==60 || snstype==61) {
      //bat
      box_text += (String) ((int) sensor->snsValue) + "%(LOW)_";
      box_border = set_color(20,20,150);
      box_fill = set_color(150,150,255);
      text_color = set_color(255-150,255-150,255-255);
    }


    if (snstype==9 || snstype==13 || snstype==19) {
      //air pressure
      box_text += (String) ((int) sensor->snsValue) + "hPA_";
      if (bitRead(sensor->Flags,5)==0) { //low pressure    
        box_border = set_color(20,20,100);
        box_fill = set_color(100,100,255);
        text_color = set_color(255-100,255-100,255-255);
      } 
      else {
        box_border = set_color(100,20,20);
        box_fill = set_color(255,100,100);
        text_color = set_color(255-255,255-100,255-100);
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
  if (((I.isFlagged!=I.wasFlagged || I.isAC != I.wasAC || I.isFan!=I.wasFan || I.isHeat != I.wasHeat) ) || I.lastHeader+3599<I.currentTime)     {
    I.lastHeader = I.currentTime;
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
  fcnDrawClock(); 
  fcnDrawHeader();
  fncDrawCurrentCondition();
  fcnDrawCurrentWeather();
  fcnDrawFutureWeather();

  return;
}

void fcnDrawSensors(int Y, int deltaY) {
  /*
  going to show rowsxcols flagged sensors (so up to rows*cols)
  will save the last leave off position and continue from that on each redraw
*/

  int X = 0;
  
  byte boxsize_x=50,boxsize_y=50;
  byte gapx = 4;
  byte gapy = 2;
  byte rows = 3, cols = 6;

  Y+= (deltaY - rows*(boxsize_y + gapy))/2;

  String roomname;

  X = 0;
 

  //fill an array with next row*cols alarm entries
  int16_t alarms[rows*cols];
  byte alarmArrayInd = 0;
  
//init alarms
  for (byte k = 0;k<(rows*cols);k++) {
    alarms[k] = -1;
  }
  
//fill up to row*cols alarm spots, and remember where we left off
  int16_t SensorIndex = I.alarmIndex;

 cycleIndex(&SensorIndex,NUMSENSORS,I.alarmIndex);

  while (SensorIndex>=0 && alarmArrayInd<(rows*cols)) {
      SnsType* sensor = Sensors.getSensorBySnsIndex(SensorIndex);
      if (sensor && sensor->IsSet != 0) {
        
        // Skip HVAC and other special sensors
        if ((sensor->snsType < 50 || sensor->snsType > 60) && sensor->snsType <99) {

          if ((bitRead(sensor->Flags, 0) == 1 ) || (sensor->expired == true && bitRead(sensor->Flags, 7) == 1)) { //bit 0 is flagged and bit 7 ismonitored
            alarms[alarmArrayInd++] = SensorIndex;          
          }
        }
      }

      cycleIndex(&SensorIndex,NUMSENSORS,I.alarmIndex);
      
  } 
  if (SensorIndex<0) I.alarmIndex = 0;
  else   I.alarmIndex = SensorIndex;
  
  alarmArrayInd=0;

  for (byte r=0;r<rows;r++) {
    if (alarms[alarmArrayInd] == -1) break;
    for (byte c=0;c<cols;c++) {
      //draw each alarm index in a box
      if (alarms[alarmArrayInd] == -1) break;
      drawBox(alarms[alarmArrayInd++], X, Y, boxsize_x, boxsize_y);
        
      X=X+boxsize_x+gapx;
    }
    Y+=boxsize_y + gapy;
    X=0;
  }
    tft.setTextColor(FG_COLOR,BG_COLOR);
}

void fcnDrawClock() {


  if (I.lastClock==0 || (I.currentTime != I.lastClock &&  second(I.currentTime)==0)) {
    I.lastClock = I.currentTime;

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

  if ((I.lastCurrentConditionTime!=0 && I.lastCurrentConditionTime + I.currentConditionInterval*60 > I.currentTime) || (I.lastCurrentTemp!=0 && I.lastCurrentTemp == I.currentTemp)) return;
  
 
  SerialPrint("Redrawing Current Condition\n");

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
    if (sensor && sensor->IsSet) {
      st = "Local @" + (String) dateify(sensor->timeLogged,"h1:nn");
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
  I.lastCurrentConditionTime = I.currentTime;
  

  return;
}

void fcnDrawCurrentWeather() {
  if ((uint32_t) (I.currentTime<I.lastWeather+I.weatherTime*60) && I.lastWeather>0) return;
  I.lastWeather = I.currentTime;

  int X=0,Y = I.HEADER_Y,Z=0; //header ends at 30

  tft.fillRect(0,I.HEADER_Y,180,180,BG_COLOR); //clear the bmp area
  tft.fillRect(0,I.HEADER_Y+180,tft.width(),tft.height()-(180+I.HEADER_Y+I.CLOCK_Y),BG_COLOR); //clear the weather area


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


//break this into a separate function
void fcnDrawFutureWeather() {
  //this function will draw futuer weather or flags


  bool forcedraw = false;
  char tempbuf[45];
  int i=0,j=0;
  byte section_spacer = 3;
  uint8_t FNTSZ = 0;
  int iconID = 0;
  int16_t Y = I.HEADER_Y+180+section_spacer;
  int16_t X = tft.width(),Z=0;
  int16_t deltaY = (tft.height()-I.CLOCK_Y)-Y-section_spacer+1;

  //if isflagged, then show rooms with flags. Note that outside sensors and RH sensors alone do not trigger a flag.
  if (I.isFlagged) {
    if (I.currentTime>I.lastFlagView+I.flagViewTime) {
      I.lastFlagView = I.currentTime;
      if (I.ScreenNum==1) {
        I.ScreenNum = 0; //stop showing flags
        forcedraw=true;
      }  else {
        I.ScreenNum = 1;
        tft.fillRect(0,Y,X,deltaY,BG_COLOR); //clear the future weather area
        fcnDrawSensors(Y,deltaY);
        return;
      }
    } 
    else if (I.ScreenNum==1) return;
  }
  
  if (I.wasFlagged==true && I.isFlagged == false) {
    //force full redraw
    forcedraw=true;
  }

  if (((uint32_t) (I.currentTime<I.lastWeather+I.weatherTime*60) && I.lastWeather>0) && forcedraw==false) return;

  //clear the area
  tft.fillRect(0,Y,X,deltaY,BG_COLOR);

  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setCursor(0,Y);

  FNTSZ = 4;
  deltaY = setFont(FNTSZ);
    
    for (i=1;i<7;i++) { //draw 6 icons, with I.HourlyInterval hours between icons. Note that index 1 is  1 hour from now
      if (i*I.HourlyInterval>72) break; //safety check if user asked for too large an interval ... cannot display past download limit
      
      Z=0;
      X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
      iconID = WeatherData.getWeatherID(I.currentTime+i*I.HourlyInterval*60*60);
      uint32_t temptime = I.currentTime+i*I.HourlyInterval*3600;
      if ((temptime > WeatherData.sunrise  && temptime < WeatherData.sunset) || temptime > WeatherData.sunrise + 86400) snprintf(tempbuf,29,"/BMP30x30day/%d.bmp",iconID); //the listed sunrise is for this day, so might be in the past. If it is in the past, we are in the day if sunset is in the future, otherwise in the night. But if we are past the prior sunrise +24h we are in the NEXT day
      else snprintf(tempbuf,29,"/BMP30x30night/%d.bmp",iconID);
      
      drawBmp(tempbuf,X,Y);
      Z+=30;

      tft.setTextColor(FG_COLOR,BG_COLOR);
      X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

      FNTSZ=1;
      deltaY = setFont(FNTSZ);
      snprintf(tempbuf,6,"%s:00",dateify(I.currentTime+i*I.HourlyInterval*60*60,"hh"));
      tft.setTextFont(FNTSZ); //small font
      fcnPrintTxtCenter((String) tempbuf,FNTSZ, X,Y+Z+deltaY/2);
      Z+=deltaY+section_spacer;

      FNTSZ=4;
      deltaY = setFont(FNTSZ);
      tft.setTextFont(FNTSZ); //med font
      byte temp = WeatherData.getTemperature(I.currentTime + i*I.HourlyInterval*3600);
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

// WiFi keypad functions
void drawKeyPad4WiFi(uint32_t y,uint8_t keyPage,uint8_t WiFiSet) {
  
  tft.fillRect(0,y,TFT_WIDTH,TFT_HEIGHT-y,BG_COLOR);
  tft.setCursor(0,y);
  uint16_t fh=setFont(2);
  tft.fillRect(0,y,TFT_WIDTH,(fh+4)*2,TFT_DARKGRAY);
  tft.setCursor(0,y+2);
  tft.setTextColor(TFT_YELLOW,TFT_DARKGRAY);
  if (WiFiSet==0)  tft.printf("SSID: %s",Prefs.WIFISSID);
  else tft.printf("PWD: %s",Prefs.WIFIPWD);
  y+=2*(fh+4);
  tft.setCursor(0,y);

  I.line_clear=y;
  
  I.line_keyboard=y;  

  byte boxL = TFT_WIDTH/6;
  byte i,j;
  //now draw keyboard
  for (i=0;i<=6;i++) {
    tft.drawFastHLine(0,y+boxL*i,TFT_WIDTH,TFT_WHITE);
    tft.drawFastVLine(boxL*i,y,TFT_HEIGHT-y,TFT_WHITE);
  }

  tft.setTextColor(TFT_WHITE,TFT_BLACK);

  for (i=0;i<6;i++) {
    for (j=0;j<6;j++) {
      //this is the ith, jth box
      uint16_t boxCenterX = 0+boxL*i + boxL/2;
      uint16_t boxCenterY = y+boxL*j + boxL/2;
      byte keyval = i+j*6;

      if (keyval < 32) fcnPrintTxtCenter((String) char(keyPage*32+33+keyval),2,boxCenterX,boxCenterY);
      else if (keyval==32) fcnPrintTxtCenter("last",1,boxCenterX,boxCenterY);
      else if (keyval==33) fcnPrintTxtCenter("next",1,boxCenterX,boxCenterY);
      else if (keyval==34) fcnPrintTxtCenter("space",1,boxCenterX,boxCenterY);
      else if (keyval==35) fcnPrintTxtCenter("del",1,boxCenterX,boxCenterY);
    }
  }

  y+=boxL*6+1;
  I.line_submit=y;

  tft.setCursor(0,y);
  tft.setTextColor(TFT_GREEN,TFT_DARKGRAY);
  tft.fillRect(0,y,TFT_WIDTH,TFT_HEIGHT-y,TFT_DARKGRAY);
  fh = setFont(4);
  if (WiFiSet<2)   {
  if (WiFiSet==0)  fcnPrintTxtCenter("SUBMIT SSID",4,TFT_WIDTH/2,y+2 + (TFT_HEIGHT - y+2)/2);
    else fcnPrintTxtCenter("SUBMIT PWD",4,TFT_WIDTH/2,y+2 + (TFT_HEIGHT - y+2)/2);
    
  }
  else fcnPrintTxtCenter("PLEASE WAIT!",4,TFT_WIDTH/2,y+2 + (TFT_HEIGHT - y+2)/2);
}

bool isTouchKey(int16_t* keyval, uint8_t* keypage) {
  //keyval
          //<0 means submit
          //300 means page down
          //301 means page up
          //257 means backspace
          //256 means clear
          //33-126 is an ascii value
          
  if (I.touchY<I.line_clear) {
    *keyval = 256;
    return true;
  }

  if (I.touchY>I.line_submit) {
    *keyval = -1;
    return true;
  }

  byte l = TFT_WIDTH/6;
  byte i,j;
  for (i=1;i<=6;i++) {
    if (I.touchX < l*i) break;
  }

  for (j=1;j<=6;j++) {
    if (I.touchY < I.line_keyboard+l*j) break;
  }

  if (i>6 || j>6) return false; //not sure how that could happen...

  byte pos = (i-1)+(j-1)*6;

  if (pos>=32) {
    if (pos==32) *keyval = 300;
    if (pos==33) *keyval = 301;
    if (pos==34) *keyval = 32;
    if (pos==35) *keyval = 257;
    return true;
  }

  *keyval = *keypage*32+33 + pos;
  
  if (*keyval>=0  && *keyval<128)   return true;
  return false;
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
  tft.printf("WiFi may be down.\nAP mode enabled.\nConnect to: %s\nIP: 192.168.4.1\nRebooting in 5 min...", WiFi.softAPSSID().c_str());
}
