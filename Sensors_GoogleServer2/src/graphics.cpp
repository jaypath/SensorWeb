#include "graphics.hpp"

#define _USETFT
#define _USETOUCH
//#define _DEBUG

void fcnPressureTxt(char* tempPres, uint16_t* fg, uint16_t* bg) {
  // print air pressure

    if (ScreenInfo.CURRENT_hPA>ScreenInfo.LAST_hPA+.5) {
      snprintf(tempPres,10,"%dhPa^",(int) ScreenInfo.CURRENT_hPA);
      *fg=tft.color565(255,0,0);
      *bg=ScreenInfo.BG_COLOR;
    } else {
      if (ScreenInfo.CURRENT_hPA<ScreenInfo.LAST_hPA-.5) {
        snprintf(tempPres,10,"%dhPa_",(int) ScreenInfo.CURRENT_hPA);
        *fg=tft.color565(0,0,255);
        *bg=ScreenInfo.BG_COLOR;
      } else {
        snprintf(tempPres,10,"%dhPa-",(int) ScreenInfo.CURRENT_hPA);
        *fg=ScreenInfo.FG_COLOR;
        *bg=ScreenInfo.BG_COLOR;
      }
    }

    if (ScreenInfo.CURRENT_hPA>=1022) {
      *fg=tft.color565(255,0,0);
      *bg=tft.color565(255,255,255);
    }

    if (ScreenInfo.CURRENT_hPA<=1000) {
      *fg=tft.color565(0,0,255);
      *bg=tft.color565(255,0,0);
    }

  return;
}

void fcnPredictionTxt(char* tempPred, uint16_t* fg, uint16_t* bg) {

    tempPred =0;
  double tempval = find_sensor_name("Outside",12);
  if (tempval!=255) tempval = (int) Sensors[(byte) tempval].snsValue;
  else tempval = 0; //currently holds predicted weather

  if ((int) tempval!=0) {
    
    switch ((int) tempval) {
      case 3:
        snprintf(tempPred,10,"GALE");
        *fg=tft.color565(255,0,0);*bg=tft.color565(0,0,0);
        break;
      case 2:
        snprintf(tempPred,10,"WIND");
        *fg=tft.color565(255,0,0);*bg=ScreenInfo.BG_COLOR;
        break;
      case 1:
        snprintf(tempPred,10,"POOR");
        *fg=ScreenInfo.FG_COLOR;*bg=ScreenInfo.BG_COLOR;
        break;
      case 0:
        snprintf(tempPred,10,"");
        *fg=ScreenInfo.FG_COLOR;*bg=ScreenInfo.BG_COLOR;
        break;
      case -1:
        snprintf(tempPred,10,"RAIN");
        *fg=ScreenInfo.FG_COLOR;*bg=ScreenInfo.BG_COLOR;
        break;
      case -2:
        snprintf(tempPred,10,"RAIN");
        *fg=tft.color565(0,0,255);*bg=ScreenInfo.BG_COLOR;

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

void fcnDrawClock() {

  bool forcedraw = false;

if (ScreenInfo.lastDrawClock==0) forcedraw=true;
  
  if (ScreenInfo.ForceRedraw==true || ScreenInfo.wasFlagged!=ScreenInfo.isFlagged) {
    //force full redraw
    if (ScreenInfo.isFlagged==false) ScreenInfo.ClockFlagScreen = 0; //stop showing flags
    ScreenInfo.lastDrawHeader=0; //redraw header
    forcedraw=true;
  }
  int Y = tft.height() - ScreenInfo.CLOCK_Y;

  //if isflagged, then show rooms with flags. Note that outside sensors and RH sensors alone do not trigger a flag.
  if (ScreenInfo.isFlagged || forcedraw) {
    if (ScreenInfo.t>ScreenInfo.lastFlagView+ScreenInfo.flagViewTimeSEC || forcedraw) {
      ScreenInfo.lastDrawHeader=0; //redraw header
      ScreenInfo.lastFlagView = ScreenInfo.t;
      if (ScreenInfo.ClockFlagScreen==1) {
        ScreenInfo.ClockFlagScreen = 0; //stop showing flags
        forcedraw=true;
      }
      else {
        ScreenInfo.ClockFlagScreen = 1;
        tft.fillRect(0,Y,tft.width(),ScreenInfo.CLOCK_Y,ScreenInfo.BG_COLOR); //clear the clock area
        fcnDrawSensors(Y);
        return;
      }
    } 
    else if (ScreenInfo.ClockFlagScreen==1) return; //don't do the rest of the clock stuff, just keep showing flags
  }
  
  if (forcedraw==false && ScreenInfo.t-ScreenInfo.lastDrawClock<ScreenInfo.intervalClockDraw*60 && ScreenInfo.lastDrawClock>0) return;

  
  ScreenInfo.lastDrawClock = ScreenInfo.t;


  //otherwise print time at bottom
  //break into three segments, will draw hour : min : sec
  int16_t X = tft.width()/3;
  uint8_t FNTSZ=6;
  uint32_t FH = setFont(FNTSZ);
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  
  #ifdef notdef
  //am I drawing the H, m, s or just s?
  if (second(t)==0 || forcedraw==true) { //I am redrawing everything, minute changed
    tft.fillRect(0,Y,tft.width()-X+tft.textWidth(":")/2+1,ScreenInfo.CLOCK_Y,ScreenInfo.BG_COLOR); //clear the "h:nn:"" clock area
    fcnPrintTxtCenter((String) ":",FNTSZ, X,Y+ScreenInfo.CLOCK_Y/2);      
    fcnPrintTxtCenter((String) ":",FNTSZ, 2*X,Y+ScreenInfo.CLOCK_Y/2);
    //print hour
    snprintf(ScreenInfo.tempbuf,5,"%s",dateify(t,"h1"));
    fcnPrintTxtCenter((String) ScreenInfo.tempbuf,FNTSZ, X/2,Y+ScreenInfo.CLOCK_Y/2);
    //print min
    snprintf(ScreenInfo.tempbuf,5,"%s",dateify(t,"nn"));
    fcnPrintTxtCenter((String) ScreenInfo.tempbuf,FNTSZ, X + X/2,Y+ScreenInfo.CLOCK_Y/2);
  }

  //now clear the second area
  tft.fillRect(2*X + tft.textWidth(":")/2+1,Y,X - tft.textWidth(":")/2-1,ScreenInfo.CLOCK_Y,ScreenInfo.BG_COLOR);
  //draw seconds
  snprintf(ScreenInfo.tempbuf,5,"%s",dateify(t,"ss"));
  fcnPrintTxtCenter((String) ScreenInfo.tempbuf,FNTSZ, X+X+X/2,Y+ScreenInfo.CLOCK_Y/2);
  #else
    if (second(t)==0 || forcedraw) {
      FH = setFont(8);
      tft.fillRect(0,Y,tft.width(),ScreenInfo.CLOCK_Y,ScreenInfo.BG_COLOR); //clear the "h:nn"" clock area
      snprintf(ScreenInfo.tempbuf,19,"%s",dateify(t,"h1:nn"));
      fcnPrintTxtCenter((String) ScreenInfo.tempbuf,8, tft.width()/2,Y+ScreenInfo.CLOCK_Y/2);
    }
  #endif

//these are set so that if things change since this draw, will know to redraw
    ScreenInfo.wasFlagged = ScreenInfo.isFlagged;
    ScreenInfo.wasAC = ScreenInfo.isAC;
    ScreenInfo.wasFan = ScreenInfo.isFan;
    ScreenInfo.wasHeat = ScreenInfo.isHeat;

  return;

}


void fcnDrawCurrentCondition() {

  bool islocal=false;

    if (ScreenInfo.ForceRedraw==false && (ScreenInfo.t-ScreenInfo.lastDrawCurrentCondition < (ScreenInfo.intervalCurrentCondition*60) && ScreenInfo.lastDrawCurrentCondition>0)) return; //not time to update cc

    if (ScreenInfo.localTempIndex>=SENSORNUM || isSensorInit(ScreenInfo.localTempIndex) || Sensors[ScreenInfo.localTempIndex].snsType!=4 || (Sensors[ScreenInfo.localTempIndex].Flags&0b100)==0 || (Sensors[ScreenInfo.localTempIndex].Flags&0b10)==0)     ScreenInfo.localTempIndex=find_sensor_name("Outside", 4);  //this is not the sensor we are looking for... find it
    if (ScreenInfo.localTempIndex<SENSORNUM   && ScreenInfo.t-ScreenInfo.localTempTime<600) islocal =true; //use local


  ScreenInfo.lastDrawCurrentCondition = ScreenInfo.t;

  int Y = ScreenInfo.HEADER_Y, Z = 10,   X=180+(tft.width()-180)/2; //middle of area on side of icon
  tft.fillRect(180,Y,tft.width()-180,180,ScreenInfo.BG_COLOR); //clear the cc area
  int FNTSZ = 8;
  byte section_spacer = 3;
  String st = "Local";

  int8_t tempMaxmin[2] = {WeatherData.daily_tempMAX[0],WeatherData.daily_tempMIN[0]};
  uint32_t FH = setFont(FNTSZ);
  
  if (islocal)  {
    fcnPrintTxtColor(ScreenInfo.localTemp,FNTSZ,X,Y+Z+FH/2);
    st+=" @" + (String) dateify(ScreenInfo.localTempTime,"h1:nn");
    byte tind = find_sensor_name("Outside",61);
    if (tind<255)       st += " Bat" + (String) ((int) Sensors[tind].snsValue) + "%";
    
    //adjust max/min to actual temp
    if (tempMaxmin[0]<ScreenInfo.localTemp) tempMaxmin[0]=ScreenInfo.localTemp;
    if (tempMaxmin[1]>ScreenInfo.localTemp) tempMaxmin[1]=ScreenInfo.localTemp;
    
    
    } else  fcnPrintTxtColor(WeatherData.hourly_temp[0],FNTSZ,X,Y+Z+FH/2);

    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
    Z+=FH+section_spacer;

  //print today max / min
    FNTSZ = 4;  
    FH = setFont(FNTSZ);
    //does local max/min trump reported?
    
    fcnPrintTxtColor2(tempMaxmin[0],tempMaxmin[1],FNTSZ,X,Y+Z+FH/2);

    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

    Z=Z+FH+section_spacer;

    if (islocal) {
    //print pressure info
        uint16_t fg,bg;
        FNTSZ = 4;
        FH = setFont(FNTSZ);
        
        ScreenInfo.tempbuf[0]=0;
        fcnPredictionTxt(ScreenInfo.tempbuf,&fg,&bg);
        tft.setTextColor(fg,bg);
        if (ScreenInfo.tempbuf[0]!=0) {// prediction made
        fcnPrintTxtCenter(ScreenInfo.tempbuf,FNTSZ,X,Y+Z+FH/2);
        Z=Z+FH+section_spacer;
        FNTSZ=2;
        }

        FH = setFont(FNTSZ);
        ScreenInfo.tempbuf[0]=0;
        fcnPressureTxt(ScreenInfo.tempbuf,&fg,&bg);
        tft.setTextColor(fg,bg);
        fcnPrintTxtCenter(ScreenInfo.tempbuf,FNTSZ,X,Y+Z+FH/2);
        Z=Z+FH+section_spacer;
    }

    FNTSZ = 4;
    FH = setFont(FNTSZ);
    
    if (WeatherData.daily_PoP[0]>30)  {
        tft.setTextColor(tft.color565(255,0,0),tft.color565(0,0,255));
        snprintf(ScreenInfo.tempbuf,40,"POP: %u%%",WeatherData.daily_PoP[0]);
        fcnPrintTxtCenter(ScreenInfo.tempbuf,FNTSZ,X,Y+Z+FH/2);
    }
    else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
    
    Z=Z+FH+section_spacer;

  //end current wthr
  
  if (islocal) {
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
    fcnPrintTxtCenter(st,1,X,Y+6);      
  }

  return;


}


void fcnDrawHeader() {
  //redraw header every  [flagged change, isheat, iscool] changed  or every new hour
  if (ScreenInfo.ForceRedraw==true || ((ScreenInfo.isFlagged!=ScreenInfo.wasFlagged || ScreenInfo.isAC != ScreenInfo.wasAC || ScreenInfo.isFan!=ScreenInfo.wasFan || ScreenInfo.isHeat != ScreenInfo.wasHeat) ) || ScreenInfo.lastDrawHeader == 0 || ScreenInfo.t>ScreenInfo.lastDrawHeader+3599)     ScreenInfo.lastDrawHeader = ScreenInfo.t;
  else return;

  tft.fillRect(0,0,tft.width(),ScreenInfo.HEADER_Y,ScreenInfo.BG_COLOR); //clear the header area

  //draw header    
  String st = "";
  uint16_t x = 0,y=0;
  uint32_t FH = setFont(4);
  tft.setCursor(0,0);
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background
  
  tft.setTextDatum(TL_DATUM);
  if (ScreenInfo.ClockFlagScreen==1) st = dateify(ScreenInfo.t,"hh:nn");
  else   st = dateify(ScreenInfo.t,"dow mm/dd");
  tft.drawString(st,x,y);
  x += tft.textWidth(st)+10;
  

  y=4;
  tft.setTextDatum(TL_DATUM);
  FH = setFont(2);


  if (ScreenInfo.isFlagged) {
    tft.setTextFont(2);
    tft.setTextColor(tft.color565(255,0,0),ScreenInfo.BG_COLOR); //without second arg it is transparent background
    tft.drawString("FLAG",x,y);
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background
  }
  x += tft.textWidth("FLAG")+10;

  setFont(0);
  fcnPrintTxtHeatingCooling(x,5);
  st = "XX XX XX "; //placeholder to find new X position
  x += tft.textWidth(st)+4;
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background


//add heat and AC icons to right 
  if ((ScreenInfo.isHeat&1)==1) {
    st="/Icons/HVAC/flame.bmp";
    drawBmp(st.c_str(),tft.width()-30,0,-1);
  }
  if ((ScreenInfo.isAC&1)==1) {
    st="/Icons/HVAC/ac30.bmp";
    drawBmp(st.c_str(),tft.width()-65,0,-1);
  }
   
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background
  

  FH = setFont(2);

  //draw a  bar below header
  tft.fillRect(0,25,tft.width(),5, ScreenInfo.FG_COLOR);

    ScreenInfo.wasFlagged = ScreenInfo.isFlagged;
    ScreenInfo.wasAC = ScreenInfo.isAC;
    ScreenInfo.wasFan=ScreenInfo.isFan;
    ScreenInfo.wasHeat = ScreenInfo.isHeat;

  return;

}

void fcnDrawWeather() {

if (ScreenInfo.ForceRedraw==false && (uint32_t) (ScreenInfo.t-ScreenInfo.lastDrawWeather<ScreenInfo.intervalWeatherDraw*60) && ScreenInfo.lastDrawWeather>0) return;
ScreenInfo.lastDrawWeather = ScreenInfo.t;

int X=0,Y = ScreenInfo.HEADER_Y,Z=0; //header ends at 30

tft.fillRect(0,ScreenInfo.HEADER_Y,180,180,ScreenInfo.BG_COLOR); //clear the bmp area
tft.fillRect(0,ScreenInfo.HEADER_Y+180,tft.width(),tft.height()-(180+ScreenInfo.HEADER_Y+ScreenInfo.CLOCK_Y),ScreenInfo.BG_COLOR); //clear the weather area


int i=0,j=0;
uint8_t FNTSZ = 0;

byte deltaY = setFont(FNTSZ);

byte section_spacer = 3;

//draw icon for NOW
int iconID = WeatherData.hourly_weatherID[0];
if (iconID==0) iconID=999;

if (ScreenInfo.t > WeatherData.sunrise  && ScreenInfo.t< WeatherData.sunset) snprintf(ScreenInfo.tempbuf,44,"/Icons/BMP180x180day/%d.bmp", iconID);
else snprintf(ScreenInfo.tempbuf,44,"/Icons/BMP180x180night/%d.bmp",iconID);

drawBmp(ScreenInfo.tempbuf,0,Y,-1);

//add info atop icon
FNTSZ=1;
deltaY = setFont(FNTSZ);
if (WeatherData.daily_PoP[0] > 35) {//greater than 50% cumulative precip prob in next 24 h
  
    snprintf(ScreenInfo.tempbuf,30,"Rain!");
    if (FNTSZ==0) deltaY = 8;
    else deltaY = setFont(FNTSZ);
  fcnPrintTxtCenter(ScreenInfo.tempbuf,FNTSZ,60,180-deltaY);
}
tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);


  Y = ScreenInfo.HEADER_Y+180+section_spacer;
  tft.setCursor(0,Y);

  FNTSZ = 4;
  deltaY = setFont(FNTSZ);
  
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  for (i=1;i<7;i++) { //draw 6 icons, with ScreenInfo.HourlyInterval hours between icons. Note that index 1 is  1 hour from now
    if (i*ScreenInfo.HourlyInterval>23) break; //safety check if user asked for too large an interval ... cannot display past 1 day
    
    Z=0;
    X = (i-1)*(tft.width()/6) + ((tft.width()/6)-30)/2; 
    iconID = WeatherData.hourly_weatherID[i*ScreenInfo.HourlyInterval];
    if (iconID==0) iconID=999;

    if (ScreenInfo.t+i*ScreenInfo.HourlyInterval*3600 > WeatherData.sunrise  && ScreenInfo.t+i*ScreenInfo.HourlyInterval*3600< WeatherData.sunset) snprintf(ScreenInfo.tempbuf,29,"/Icons/BMP30x30day/%d.bmp",iconID);
    else snprintf(ScreenInfo.tempbuf,29,"/Icons/BMP30x30night/%d.bmp",iconID);
    
    drawBmp(ScreenInfo.tempbuf,X,Y,-1);
    Z+=30;

     tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
    X = (i-1)*(tft.width()/6) + ((tft.width()/6))/2; 

    FNTSZ=1;
    deltaY = setFont(FNTSZ);
    snprintf(ScreenInfo.tempbuf,6,"%s:00",dateify(ScreenInfo.t+i*ScreenInfo.HourlyInterval*60*60,"hh"));
    tft.setTextFont(FNTSZ); //small font
    fcnPrintTxtCenter((String) ScreenInfo.tempbuf,FNTSZ, X,Y+Z+deltaY/2);

    Z+=deltaY+section_spacer;

    FNTSZ=4;
    deltaY = setFont(FNTSZ);
    tft.setTextFont(FNTSZ); //med font
    fcnPrintTxtColor(WeatherData.hourly_temp[i*ScreenInfo.HourlyInterval],FNTSZ,X,Y+Z+deltaY/2);
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
    Z+=deltaY+section_spacer;
  }
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  Y+=Z;


//now draw daily weather
  tft.setCursor(0,Y);
  int8_t tmm[2];
  for (i=1;i<WeatherData.daystoget;i++) {
    Z=0;
    X = (i-1)*(tft.width()/5) + ((tft.width()/5)-60)/2; 
    iconID = WeatherData.daily_weatherID[i];
    if (iconID==0) iconID=999;

    snprintf(ScreenInfo.tempbuf,29,"/Icons/BMP60x60day/%d.bmp",iconID); //alway print day icons for future days

    drawBmp(ScreenInfo.tempbuf,X,Y,-1);
    Z+=60;
    
    X = (i-1)*(tft.width()/5) + ((tft.width()/5))/2; 
    FNTSZ=2;
    deltaY = setFont(FNTSZ);
    snprintf(ScreenInfo.tempbuf,31,"%s",dateify(now()+i*60*60*24,"DOW"));
    fcnPrintTxtCenter((String) ScreenInfo.tempbuf,FNTSZ, X,Y+Z+deltaY/2);
    
    Z+=deltaY;    
    fcnPrintTxtColor2(WeatherData.daily_tempMAX[i],WeatherData.daily_tempMIN[i],FNTSZ,X,Y+Z+deltaY/2); 
    
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background
    
    Z+=deltaY+section_spacer;
    
  }

  Y+=Z;

 tft.setCursor(0,Y);
 
  
}




void fcnDrawSensors(int Y) {
  /*
  going to show 6x2 flagged sensors (so up to 12)
  will save the last leave off position and continue from that on each redraw
*/

  int X = 0;
  
  byte boxsize_x=50,boxsize_y=50;
  byte gapx = 4;
  byte gapy = 2;
  
  String roomname;

  X = 0;
 
  byte rows = 2, cols = 6;


  //fill an array with next 12 alarm entries
  int alarms[rows*cols];
  byte alarmArrayInd = 0;
  byte localInd = 0;

//init alarms
  for (byte k = 0;k<(rows*cols);k++) alarms[k]=-1;
  
//fill up to 12 alarm spots, and remember where we left off
  for (byte k = 0;k<SENSORNUM;k++) {
    localInd = (k+ScreenInfo.alarmIndex)%SENSORNUM; //this is the index to sensor array, which will loop around at SENSORNUM
    if (isSensorInit(localInd)==false) continue; //skip blanks
    if ((Sensors[localInd].snsType>=50 && Sensors[localInd].snsType<60) || Sensors[localInd].snsType==99) continue; //skip HVAC and other

    if (bitRead(Sensors[localInd].Flags,0)==1 || bitRead(Sensors[localInd].localFlags,1)==1) alarms[alarmArrayInd++] = localInd;
    if (alarmArrayInd>=(rows*cols)) break;
  }
  ScreenInfo.alarmIndex = (localInd+1)%SENSORNUM;

  alarmArrayInd = 0;

  for (byte r=0;r<rows;r++) {
    if (alarms[alarmArrayInd]==-1) break;
    for (byte c=0;c<cols;c++) {
      //draw each alarm index in a box
      if (alarms[alarmArrayInd]==-1) break;
      drawBox(alarms[alarmArrayInd++],  X,  Y,boxsize_x,boxsize_y);
        
      X=X+boxsize_x+gapx;
    }
    Y+=boxsize_y + gapy;
    X=0;
  }
    tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

}


uint16_t set_color(byte r, byte g, byte b) {
  #ifdef _USE24BITCOLOR
    return tft.color888(r,g, b);
   #endif

    return tft.color565(r,g, b);

}

void drawBox(byte sensorInd, int X, int Y, byte boxsize_x,byte boxsize_y) {
  /*uint8_t box_nl_border[3] = {0,125,0};
  uint8_t box_nl_fill[3] = {120,255,120};
  uint8_t box_high_border[3] = {150,20,20}; //r,g,b
  uint8_t box_high_fill[3] = {255,100,100};
  uint8_t box_low_border[3] = {20,20,150};
  uint8_t box_low_fill[3] = {150,150,255};
  uint8_t box_dry_border[3] = {65,45,20};
  uint8_t box_dry_fill[3] = {250,170,100};
  uint8_t box_wet_border[3] = {0,0,255};
  uint8_t box_wet_fill[3] = {0,190,255};
  */

  uint16_t box_border = set_color(200,175,200);
  uint16_t box_fill = set_color(75,50,75);
  uint16_t text_color = set_color(255,225,255);

  String box_text = "";
  

  box_text = (String) Sensors[sensorInd].snsName + (String) "_";

  if (bitRead(Sensors[sensorInd].localFlags,1)==1) {
    box_text += "EXP!_";
    box_border = set_color(150,150,150);
    box_fill = set_color(200,200,200);
    text_color = set_color(55,55,55);
  } else {

    //get sensor vals 1, 4, 3, 70, 61... if any are flagged then set box color
    byte snstype = Sensors[sensorInd].snsType;
    if (snstype==1 || snstype==4 || snstype==10 || snstype==14 || snstype==17) {
      //temperature
      box_text += (String) ((int) Sensors[sensorInd].snsValue) + "F_";
      if (bitRead(Sensors[sensorInd].Flags,5)==1) {        
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
      box_text += (String) "DRY_";
      box_border = set_color(65,45,20);
      box_fill = set_color(250,170,100);
      text_color = set_color(255-250,255-170,255-100);
    }

    if (snstype==2 || snstype==5 || snstype==15 || snstype==18) {
      //humidity
      box_text += (String) ((int) Sensors[sensorInd].snsValue) + "%RH_";
      if (bitRead(Sensors[sensorInd].Flags,5)==0) {
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
      box_text += (String) ((int) Sensors[sensorInd].snsValue) + "%(LOW)_";
      box_border = set_color(20,20,150);
      box_fill = set_color(150,150,255);
      text_color = set_color(255-150,255-150,255-255);
    }


    if (snstype==9 || snstype==13 || snstype==19) {
      //air pressure
      box_text += (String) ((int) Sensors[sensorInd].snsValue) + "hPA_";
      if (bitRead(Sensors[sensorInd].Flags,5)==0) { //low pressure    
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
//  tft.fillRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_fill);
  //tft.drawRoundRect(X,Y,boxsize_x-2,boxsize_y-2,8,box_border);
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

//returns 16 bit color
uint16_t temp2color(int temp, bool autoadjustluminance) {


  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }


  double r,g,b;
  if (autoadjustluminance ) {

    byte lumin;
    #ifdef _USE24BITCOLOR
      lumin =  color2luminance(tft.color888(temp_colors[j+1],temp_colors[j+2] ,temp_colors[j+3]),false); //this is the perceived luminance of the color
    #else
      lumin =  color2luminance(tft.color565(temp_colors[j+1],temp_colors[j+2] ,temp_colors[j+3]),true); //this is the perceived luminance of the color
    #endif

    //is the liminance within 20% (50 units) of BG luminance?
    double deltalum = ScreenInfo.BG_luminance - lumin;
    
    if (abs(deltalum) <50) {   //luminance is too close   
      if (deltalum<0) { //color is darker, but not dark enough
        //decrease luiminance by 50-deltalum
        r =  minmax((double) temp_colors[j+1] - (50+deltalum),0,255);
        g =  minmax((double) temp_colors[j+2] - (50+deltalum),0,255);
        b =  minmax((double) temp_colors[j+3] - (50+deltalum),0,255);        
        
      } else { //color is lighter, but not light enough
        //increase luminance by 50-deltalum
        r =  minmax((double) temp_colors[j+1] + (50-deltalum),0,255);
        g =  minmax((double) temp_colors[j+2] + (50-deltalum),0,255);
        b =  minmax((double) temp_colors[j+3] + (50-deltalum),0,255);        
      }  
      #ifdef _DEBUG
        Serial.printf("temp2color: Luminance: %u\ndelta_BG: %d\nR: %d\nG: %d\nB: %d\n", lumin,deltalum,r,g,b);
      #endif
    }
  } else {
    r =  temp_colors[j+1] ;
    g =  temp_colors[j+2] ;
    b =  temp_colors[j+3] ;        
  }

    #ifdef _USE24BITCOLOR
        return tft.color888((byte) r, (byte) g,(byte) b);
      #else
        return tft.color565((byte) r, (byte) g,(byte) b);
      #endif

  
 
}

void color2RGB(uint32_t color,byte* R, byte* G, byte *B, bool is24bit) {

if (is24bit) {

  *R = (color>>16)&0xff;
  *G = (color>>8)&0xff;
  *B = color&0xff;
  return;
}
  double c;

  c = 255*((color>>11)&0b11111)/0b11111;
  *R = (byte) c;
  
  c = 255*((color>>5)&0b111111)/0b111111;
  *G = (byte) c;

  c = 255*(color&0b11111)/0b11111;
  *B = (byte) c;  

}



void clearTFT() {
  tft.fillScreen(ScreenInfo.BG_COLOR);                 // Fill with black
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setCursor(0,0);
  ScreenInfo.Ypos = 0;
}


void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x,int y, String headstr, String tailstr) {
  //print two temperatures with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position


String temp = (String) value1;
temp+= (String) "/" + ((String) value2);

int X,Y;
X = x-tft.textWidth(headstr+(String) value1 + "/" + (String) value2 + tailstr)/2;
Y=y-tft.fontHeight(FNTSZ)/2;
if (X<0 || Y<0) {
  
  if (X<0) X=0;
  if (Y<0) Y=0;
}
tft.setTextFont(FNTSZ);

tft.setTextDatum(TL_DATUM);

tft.setCursor(X,Y);

tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
if (headstr!="") tft.print(headstr.c_str());
tft.setTextColor(temp2color(value1),ScreenInfo.BG_COLOR);
tft.print(value1);
tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
tft.print("/");
tft.setTextColor(temp2color(value2),ScreenInfo.BG_COLOR);
tft.print(value2);
tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
if (tailstr!="") tft.print(tailstr.c_str());

return;
}


void fcnPrintTxtColor(int value,byte FNTSZ,int x,int y, String headstr, String tailstr) {
  //print  temperature with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position

uint16_t bg=ScreenInfo.BG_COLOR;
tft.setTextDatum(TL_DATUM);



tft.setCursor(x-tft.textWidth( headstr + (String) value + tailstr)/2,y-tft.fontHeight(FNTSZ)/2); //set the correct x and y to center text at this location

tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
if (headstr!="") tft.print(headstr.c_str());
tft.setTextColor(temp2color(value),ScreenInfo.BG_COLOR);
tft.print(value);
tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
if (tailstr!="") tft.print(tailstr.c_str());

//tft.drawString(headstr + (String) value + tailstr,x,y);


return;
}




uint8_t color2luminance(uint32_t color, bool is16bit) {
//0.2126*R + 0.7152*G + 0.0722*B trying this luminance option
//false if this is a 24 bit color

byte R,G,B;

if (is16bit) {
  color2RGB(color,&R,&G,&B,false);
} else {
  color2RGB(color,&R,&G,&B,true);
}
  return  (byte) ((double) 0.2126*R + 0.7152*G + 0.0722*B);
 
}

void drawScreen()
{
  bool isTouch = false; //no touch

//  1. get touch info and update as appropriate
#ifdef _USETOUCH
  if (tft.getTouch(&ScreenInfo.touch_x, &ScreenInfo.touch_y)) {
    isTouch = true;
    
    byte buttonNum = getButton(ScreenInfo.touch_x, ScreenInfo.touch_y,ScreenInfo.ScreenNum);
    if (buttonNum>0) {
      tft.fillCircle(ScreenInfo.touch_x,ScreenInfo.touch_y,12,TFT_GOLD);
      delay(250);
    }
    

    if (ScreenInfo.ScreenNum == 0) {
      ScreenInfo.ScreenNum = 1; //on main screen, this means go to list screen
      ScreenInfo.ForceRedraw=1; //force a screen redraw
      ScreenInfo.lastDrawList=0;
      ScreenInfo.snsLastDisplayed=0;
      ScreenInfo.lastFlagTally=0;     
    } else {
      if (ScreenInfo.ScreenNum == 1) {
        //buttons on list screen are: 
        /*
          next page
          main
          upload
          show alarmed
          show all
          settings
        */

        switch (buttonNum) {
          case 0:
          {
            //no button
            break;
          }
          case 1:
          {
            ScreenInfo.ForceRedraw=1; //force a redraw. since we have not changed the screen, this will redraw the list and start at the next list position
            break;
          }
          case 2:
          {
            ScreenInfo.ScreenNum = 0; 
            ScreenInfo.lastDrawHeader=0; //redraw header
            ScreenInfo.lastDrawClock=0; //redraw clock
            ScreenInfo.lastDrawCurrentCondition=0; //redraw current condition
            ScreenInfo.lastDrawWeather=0;
            ScreenInfo.ForceRedraw=1; //force a screen redraw at main
            break;
          }
          case 3:
          {
            ScreenInfo.lastGsheetUploadTime=0; //force an upload
            ScreenInfo.lastDrawList=0;
            ScreenInfo.snsLastDisplayed=0;
            ScreenInfo.lastFlagTally=0;     
            uploadData(true);
            writeSensorsSD("/Data/SensorBackup.dat");
            storeScreenInfoSD();
            SendData(); //update server about our status
            ScreenInfo.ForceRedraw=1; //force a screen redraw

            break;
          }
          case 4:
          {
            ScreenInfo.showAlarmedOnly = 1;            
            ScreenInfo.lastDrawList=0;
            ScreenInfo.snsLastDisplayed=0;
            ScreenInfo.lastFlagTally=0;     
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 5:
          {
            ScreenInfo.showAlarmedOnly = 0;
            ScreenInfo.lastDrawList=0;
            ScreenInfo.snsLastDisplayed=0;
            ScreenInfo.lastFlagTally=0;     

            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 6:
          {
            ScreenInfo.ScreenNum=2;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
        }
      } else {
        if (ScreenInfo.ScreenNum == 2) { //settings screen
        byte buttonNum = getButton(ScreenInfo.touch_x, ScreenInfo.touch_y,ScreenInfo.ScreenNum);
        //buttons on setting screen are: 
        /*
          
          list
          main
          select
          reset
          ++
          --
        */
        switch (buttonNum) {
          case 0:
          {            
            break;
          }
          case 1:
          {
            ScreenInfo.ScreenNum = 1; // go to list screen
            ScreenInfo.lastDrawList=0;
            ScreenInfo.snsLastDisplayed=0;
            ScreenInfo.lastFlagTally=0;     
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 2:
          {
            ScreenInfo.ScreenNum = 0; 
            ScreenInfo.lastDrawHeader=0; //redraw header
            ScreenInfo.lastDrawClock=0; //redraw clock
            ScreenInfo.lastDrawCurrentCondition=0; //redraw current condition
            ScreenInfo.lastDrawWeather=0;
            ScreenInfo.ForceRedraw=1; //force a screen redraw at main
            break;
          }
          case 3:
          {
            ScreenInfo.settingsSelectableIndex=(ScreenInfo.settingsSelectableIndex+1)%10; //up to 10 lines are selectable
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 4: 
          {
            //clean reboot
            writeSensorsSD("/Data/SensorBackup.dat");
            storeScreenInfoSD();
            SendData(); //update server about our status
            ESP.restart();
            break;
          }
          case 5:
          {
            ScreenInfo.settingsActivated=1;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 6:
          {
            ScreenInfo.settingsActivated=-1;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
        
        }
      }
      }
    }


    #ifdef _DEBUG
      Serial.printf("Touch: X= %d, Y= %d\n",ScreenInfo.touch_x, ScreenInfo.touch_y);
    #endif
  }
  else {
    isTouch = false;
    ScreenInfo.touch_x = 0;
    ScreenInfo.touch_y = 0;
  }
#else
    isTouch = false;
    ScreenInfo.touch_x = 0;
    ScreenInfo.touch_y = 0;

#endif



if ((uint32_t) ScreenInfo.ForceRedraw==0 && ScreenInfo.ScreenNum>0 && (ScreenInfo.t-ScreenInfo.lastDrawList)>ScreenInfo.intervalListDraw*60 && ScreenInfo.lastDrawList>0) {
  ScreenInfo.ForceRedraw=1;
  ScreenInfo.ScreenNum=0;
}


switch (ScreenInfo.ScreenNum) {
  case 0:
  {
    if (ScreenInfo.ForceRedraw) {
      ScreenInfo.lastDrawClock=0;
      ScreenInfo.lastDrawWeather=0;
      ScreenInfo.lastDrawCurrentCondition=0;
      ScreenInfo.lastDrawHeader=0;
    }
       fcnDrawClock();
        fcnDrawCurrentCondition();
      fcnDrawWeather();
        fcnDrawHeader(); //this redraws only when needed


        
    break;
  }
  case 1:
  {
    if (ScreenInfo.ForceRedraw) {
      ScreenInfo.lastDrawList=0;
            
      fcnDrawList();     
      
    }
    else if ((ScreenInfo.t-ScreenInfo.lastDrawList)>ScreenInfo.intervalListDraw*60 && ScreenInfo.lastDrawList>0) ScreenInfo.ScreenNum=0;

    break;
  }
  case 2:
  {
    if (ScreenInfo.ForceRedraw)    {
      ScreenInfo.lastDrawList=0;
            
      fcnDrawSettings();
    }
    else if ((ScreenInfo.t-ScreenInfo.lastDrawList)>ScreenInfo.intervalListDraw*60 && ScreenInfo.lastDrawList>0) ScreenInfo.ScreenNum=0;
    break;
  }
}

ScreenInfo.ForceRedraw = 0;


}


void drawBmp(const char *filename, int16_t x, int16_t y,int32_t transparent) {

  if ((x >= TFT_WIDTH) || (y >= TFT_HEIGHT)) return;

  fs::File bmpFS;

  // Open requested file on SD card
  bmpFS = SD.open(filename, "r");

  if (!bmpFS)
  {
    tft.print(filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;

  uint32_t startTime = millis();

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
        
          *tptr = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
          if (transparent>=0 && transparent<=0xFFFF) {
            if (*tptr==transparent) *tptr = ScreenInfo.BG_COLOR; //convert specified pixels to background
          }
          *tptr++;
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      //Serial.print("Loaded in "); Serial.print(millis() - startTime);
      //Serial.println(" ms");
    }
    else tft.println("BMP format not recognized.");
  }
  bmpFS.close();
}


void fcnDrawList() {
  ScreenInfo.lastDrawList = ScreenInfo.t;

  clearTFT();
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
    
  ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(4)/2;
  tft.drawString(dateify(now(),"hh:nn mm/dd"), TFT_WIDTH / 2, ScreenInfo.Ypos);
  tft.setTextDatum(TL_DATUM);

  ScreenInfo.Ypos += tft.fontHeight(4)/2 + 1;

  tft.setTextFont(SMALLFONT);
  snprintf(ScreenInfo.tempbuf,60,"All:%u Dry:%u Hot:%u Cold:%u Exp:%u\n",countDev(),ScreenInfo.isSoilDry,ScreenInfo.isHot,ScreenInfo.isCold,ScreenInfo.isExpired);
  tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(1) + 1;

int xl=0;
  tft.fillRect(xl,ScreenInfo.Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
//            ardid time    name    min  cur   max    * for sent
xl+=50;

  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  xl+=35;
  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  xl+=80;
  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  xl+=34;
  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  xl+=36;
  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  xl+=36;
  tft.fillRect(xl,ScreenInfo.Ypos,1,TFT_HEIGHT-ScreenInfo.Ypos,TFT_DARKGRAY);
  ScreenInfo.Ypos += 4;

  tft.setTextFont(SMALLFONT);
  tft.setTextColor(TFT_DARKGRAY,ScreenInfo.BG_COLOR);

    xl=0;
    snprintf(ScreenInfo.tempbuf,60,"ID");
    tft.drawString(ScreenInfo.tempbuf, xl, ScreenInfo.Ypos);
    xl+=50;
    snprintf(ScreenInfo.tempbuf,60,"Time");
    tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);
xl+=35;
    snprintf(ScreenInfo.tempbuf,60,"Name");
    tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);
  xl+=80;
    snprintf(ScreenInfo.tempbuf,60,"MIN");
tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);
xl+=34;
    snprintf(ScreenInfo.tempbuf,60,"Val");
tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);
xl+=36;
    snprintf(ScreenInfo.tempbuf,60,"MAX");
tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);
xl+=36;
    snprintf(ScreenInfo.tempbuf,60,"Status");
tft.drawString(ScreenInfo.tempbuf, xl+2, ScreenInfo.Ypos);

  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  ScreenInfo.Ypos += tft.fontHeight(SMALLFONT) + 2;

  tft.setTextFont(SMALLFONT); //
  
            
  

  // display the list in order, starting from the last value displayed
    #ifdef _DEBUG
      Serial.printf("ListArray starting at %u\n",ScreenInfo.snsLastDisplayed);
    #endif

  int  k;
  byte counter = 0;
  byte minval = 1;
  if (ScreenInfo.showAlarmedOnly !=0) minval = 2;

  for (byte i = 0; i<SENSORNUM; i++) {
    //march through every value, display if it is the next one and exists and meets alarm criteria
    
    k = ScreenInfo.snsLastDisplayed+i;

    if (k>=SENSORNUM) {      
      k=-1;
      break;
    }

    //search for value k!
    
    byte j;
    for (j=0;j<SENSORNUM;j++)       if (ScreenInfo.snsListArray[1][j]==k) break;

    if (j<SENSORNUM && ScreenInfo.snsListArray[0][j] >= minval) {
      ScreenInfo.Ypos = fcn_write_sensor_data(j,ScreenInfo.Ypos);
      if (++counter==30) break;
    }

  }
  ScreenInfo.snsLastDisplayed=k+1;
  if (ScreenInfo.snsLastDisplayed>=SENSORNUM) ScreenInfo.snsLastDisplayed=0;


  //draw buttons on bottom 1/3 page
          //buttons on list screen are: 
        /*
          next page
          main
          upload
          show alarmed
          show all
          settings
        */
  drawButton((String) "Next",(String) "Main",(String) "Upload",(String) "Alarmed",(String) "ShowAll",(String) "Settings");
  
}

void drawButton(String b1, String b2,  String b3,  String b4,  String b5,  String b6) {
  int16_t Y =  TFT_HEIGHT*0.75;
  int16_t X = 0;

  tft.fillRect(X,Y,TFT_WIDTH,TFT_HEIGHT*0.25,ScreenInfo.BG_COLOR);

  tft.setTextFont(SMALLFONT);
  byte bspacer = 5;
  byte bX = (byte) ((double)(TFT_WIDTH - 6*bspacer)/3);
  byte bY = (byte) ((double) (TFT_HEIGHT*.25 - 4*bspacer)/2);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK,tft.color565(200,200,200));
  
  for (byte i=0;i<2;i++) {
    X = 0;
    for (byte j=0;j<3;j++) {
      tft.fillRoundRect(X+bspacer,Y+bspacer,bX,bY,4,tft.color565(200,200,200));
      tft.drawRoundRect(X+bspacer,Y+bspacer,bX,bY,4,tft.color565(125,125,125));
      if (j==0 && i==0) snprintf(ScreenInfo.tempbuf,60,b1.c_str());
      if (j==1 && i==0) snprintf(ScreenInfo.tempbuf,60,b2.c_str());
      if (j==2 && i==0) snprintf(ScreenInfo.tempbuf,60,b3.c_str());
      if (j==0 && i==1) snprintf(ScreenInfo.tempbuf,60,b4.c_str());
      if (j==1 && i==1) snprintf(ScreenInfo.tempbuf,60,b5.c_str());
      if (j==2 && i==1) snprintf(ScreenInfo.tempbuf,60,b6.c_str());
      
      tft.drawString(ScreenInfo.tempbuf, (int) ((double) X+bX/2),(int) ((double) Y + bY/2),SMALLFONT);
      X+=bX+bspacer+bspacer;     

    }
    Y+=bY+bspacer+bspacer;
  }

  tft.setTextDatum(TL_DATUM);

  
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
}


void fcnDrawSettings() {
  ScreenInfo.lastDrawList = ScreenInfo.t;
  byte isSelected = ScreenInfo.settingsSelectable[ScreenInfo.settingsSelectableIndex];
  if (isSelected>7) {
    ScreenInfo.settingsSelectableIndex = 0;
    ScreenInfo.settingsActivated=0;
    isSelected = ScreenInfo.settingsSelectable[ScreenInfo.settingsSelectableIndex];
  }

  //was a button pushed?
  if (ScreenInfo.settingsActivated!=0) {    
    switch(isSelected) {
      case 0:
      {
        ScreenInfo.lastDrawWeather=0;
        getWeather();
        break;
      }
      case 1:
      {
        //line 1: interval for weather screen  - is selectable
        ScreenInfo.intervalWeatherDraw += ScreenInfo.settingsActivated*5;
        if (ScreenInfo.intervalWeatherDraw>60) ScreenInfo.intervalWeatherDraw=5;
        if (ScreenInfo.intervalWeatherDraw<5) ScreenInfo.intervalWeatherDraw=60;
        break;

      }
      case 2:
      {
        //line 2: interval for hrly weather  - is selectable
        ScreenInfo.HourlyInterval += ScreenInfo.settingsActivated;        
        if (ScreenInfo.HourlyInterval>6) ScreenInfo.HourlyInterval=6;
        if (ScreenInfo.HourlyInterval==0) ScreenInfo.HourlyInterval=1;
        break;


      }

      case 3:
      {
        //line 3: show flags - is selectable
        ScreenInfo.flagViewTimeSEC += ScreenInfo.settingsActivated*5;
        if (ScreenInfo.flagViewTimeSEC>60) ScreenInfo.flagViewTimeSEC=5;
        if (ScreenInfo.flagViewTimeSEC<5) ScreenInfo.flagViewTimeSEC=60;
        break;
      }

      case 4:
      {
        //line 4: upload int - is selectable
        ScreenInfo.uploadGsheetInterval += ScreenInfo.settingsActivated*5; //increment by 5 minutes
        if (ScreenInfo.uploadGsheetInterval<5) ScreenInfo.uploadGsheetInterval=120;
        if (ScreenInfo.uploadGsheetInterval>120) ScreenInfo.uploadGsheetInterval=5;
        break;
      }
      case 5:
      {
//line 5: upload gsheet- is selectable
        ScreenInfo.lastGsheetUploadTime=0; //force an upload
        ScreenInfo.lastDrawList=0;
        ScreenInfo.ForceRedraw=1;
        ScreenInfo.snsLastDisplayed=0;
        uploadData(true);
        writeSensorsSD("/Data/SensorBackup.dat");
        storeScreenInfoSD();
        SendData(); //update server about our status
            
        break;

      }

      case 6:
      {
//line 6: delete settings - is selectable
        deleteFiles("SensorBackup.dat","/Data");
        deleteFiles("ScreenFlags.dat","/Data");
        break;
      }

      case 7:
      {
//line 7: delete SD data - is selectable
        deleteFiles("*.dat","/Data");
        break;
      }

    }

    ScreenInfo.settingsActivated=0;
  }

  clearTFT();
  ScreenInfo.Ypos = 0;
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);


  ScreenInfo.Ypos = ScreenInfo.Ypos + tft.fontHeight(4)/2;
  tft.drawString(dateify(now(),"hh:nn mm/dd"), TFT_WIDTH / 2, ScreenInfo.Ypos);
  tft.setTextDatum(TL_DATUM);
  ScreenInfo.Ypos += tft.fontHeight(4)/2 + 6;
  tft.fillRect(0,ScreenInfo.Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
  ScreenInfo.Ypos += 4;

  tft.setTextFont(2);
  //can show 15 lines, but only up to 10 can be selectable  - see button settings in drawscreen. ScreenInfo.settingsActivated has the current selection
  
  byte i=0;
//line 0: last weather [update] - is selectable
  if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"last weather: %s [update now]\n",dateify(ScreenInfo.lastDrawWeather,"mm/dd hh:nn"));
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 1: interval for weather screen  - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Weather check every %u minutes\n", ScreenInfo.intervalWeatherDraw);
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 2: interval for hrly weather  - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Display wthr with a %u hr gap\n",ScreenInfo.HourlyInterval);
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 3: show flags - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Display flag data for %u sec\n",ScreenInfo.flagViewTimeSEC);
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 4: upload int - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Gsheet Upload every %u min\n",ScreenInfo.uploadGsheetInterval);
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 5: upload gsheet- is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"last Gsheet was %s at %s [upload now]\n",(ScreenInfo.lastGsheetUploadSuccess==true)?"OK":"LOST",dateify(ScreenInfo.lastGsheetUploadTime,"mm/dd hh:nn"));
  tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 6: delete settings - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Delete Settings Data\n");
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line 7: delete SD data - is selectable
    if (isSelected==i++) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"DELETE Saved Sensor and Settings Data\n");
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line not selectable
    if (isSelected==i++) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Free/Lowest Heap: %u/%u\n",(uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000,(uint32_t) ESP.getMinFreeHeap()/1000);
  tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line not selectable
    if (isSelected==i++) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Alive since %s\n",dateify(ScreenInfo.ALIVESINCE));
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line not selectable
    if (isSelected==i++) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
tft.setTextFont(2);
  snprintf(ScreenInfo.tempbuf,70,"Current Gsheet: %s\n",GsheetName.c_str());
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line not selectable
    if (isSelected==i++) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"Last error (at %s): \n", dateify(ScreenInfo.lastErrorTime,"mm/dd hh:nn:ss"));
  tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;

//line not selectable
    if (isSelected==i++) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(ScreenInfo.tempbuf,70,"%s\n",lastError.c_str());
tft.drawString(ScreenInfo.tempbuf,0,ScreenInfo.Ypos);
  ScreenInfo.Ypos += tft.fontHeight(2) + 2;




        //buttons on setting screen are: 
        /*
          list
          main
          select
          reset
          ++
          --
        */
  drawButton((String) "List",(String) "Main",(String) "Select",(String) "Reset",(String) "++",(String) "--");
  
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
    case 3:
      tft.setFont(&fonts::FreeSans9pt7b);
    case 4:
      tft.setFont(&fonts::Font4);
    break;
    case 5:
      tft.setFont(&fonts::FreeSans24pt7b);
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
  }

  return tft.fontHeight();

}

int fcn_write_sensor_data(byte i, int y) {
    //i is the sensor number, y is ScreenInfo.Ypos
    //writes a line of data, then returns the new y pos
    uint16_t xpos = 0;

      tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
      tft.setTextDatum(TL_DATUM);
      
      tft.setTextFont(SMALLFONT);

      snprintf(ScreenInfo.tempbuf,60,"%u.%u.%u", Sensors[i].ardID,Sensors[i].snsType,Sensors[i].snsID);
      tft.drawString(ScreenInfo.tempbuf, 0, y);
      
      dateify(Sensors[i].timeLogged,"hh:nn");
      tft.drawString(DATESTRING, 52, ScreenInfo.Ypos);

      snprintf(ScreenInfo.tempbuf,59,"%s", Sensors[i].snsName);
      tft.drawString(ScreenInfo.tempbuf, 87, ScreenInfo.Ypos);
 
      snprintf(ScreenInfo.tempbuf,60,"%d",(int) Sensors[i].snsValue_MIN);
      tft.drawString(ScreenInfo.tempbuf, 168, ScreenInfo.Ypos);

      snprintf(ScreenInfo.tempbuf,60,"%d",(int) Sensors[i].snsValue);
      tft.drawString(ScreenInfo.tempbuf, 204, ScreenInfo.Ypos);
      
      snprintf(ScreenInfo.tempbuf,60,"%d",(int) Sensors[i].snsValue_MAX);
      tft.drawString(ScreenInfo.tempbuf, 240, ScreenInfo.Ypos);

      xpos=276;
      if (bitRead(Sensors[i].localFlags,1)==1) { //expired
        tft.setTextColor(TFT_YELLOW,ScreenInfo.BG_COLOR);  
        if (bitRead(Sensors[i].Flags,7)==1) xpos+=tft.drawString("X",xpos,ScreenInfo.Ypos);
        else xpos+=tft.drawString("x",xpos,ScreenInfo.Ypos);
      }
      if (bitRead(Sensors[i].Flags,0)==1) { //flagged
        tft.setTextColor(TFT_RED,ScreenInfo.BG_COLOR);  
        xpos+=tft.drawString("!",xpos,ScreenInfo.Ypos);
      }
      tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);  

      if (Sensors[i].localFlags&0b1) { //sent
        xpos+=tft.drawString("SENT",xpos,ScreenInfo.Ypos);        
      }

      return y + tft.fontHeight(SMALLFONT)+2;

}

void fcnPrintTxtCenter(String msg,byte FNTSZ, int x, int y) {
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
tft.print(msg.c_str());
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
  uint16_t c_FG=TFT_DARKGRAY, c_BG=ScreenInfo.BG_COLOR;
  
  
  int x = X;
  tft.setTextDatum(TL_DATUM);
  for (byte j=1;j<7;j++) {
    
    c_FG=TFT_DARKGRAY;
    c_BG=ScreenInfo.BG_COLOR;
  
    if (j==4) {
      x=X;
      Y+= FH+2; 
    }

    if (bitRead(ScreenInfo.isHeat,j) && bitRead(ScreenInfo.isAC,j)) {
      c_FG = c_errorfg;
      c_BG = c_errorbg;
    } else {
      if (bitRead(ScreenInfo.isFan,j)) {
          c_BG = c_fan; 
      }
      if (bitRead(ScreenInfo.isHeat,j)) {
        c_FG = c_heat;        
      }
      if (bitRead(ScreenInfo.isAC,j)) {
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
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR); //without second arg it is transparent background

  return;
}
