#ifdef _ISCLOCK480X480


#include "Clock480X480.hpp"
#include "globals.hpp"
#include "utility.hpp"

LGFX tft;

struct Screen myScreen;

extern STRUCT_CORE I;

// Explicit RGB565 literals for black/white so we are not affected by macro redefs (LovyanGFX can #undef TFT_* when included after globals).
static constexpr uint16_t RGB565_BLACK = 0x0000;
static constexpr uint16_t RGB565_WHITE = 0xFFFF;

void initClock480X480Graphics() {
    //Init globals
    myScreen.BG_COLOR = RGB565_BLACK;
    myScreen.FG_COLOR = TFT_LIGHTGRAY;

    myScreen.lastX=0;
    myScreen.lastY=0;

    myScreen.int_Weather_MIN=30;
    myScreen.time_lastClock=0;
    myScreen.time_lastPic=0;
    myScreen.time_lastWeather=0;

    myScreen.screenNum=0;
    myScreen.oldScreenNum=0;
    myScreen.screenChangeTimer=0;
    myScreen.screenChangeTimerMax=15;
    myScreen.clockChangeTimer=0;
    myScreen.touchDebounceMS=400;
    myScreen.lastTouchMS=0;

    tft.init();
    tft.setColorDepth(16);  // set before any drawing so 16-bit RGB565 is used consistently
    tft.fillScreen(RGB565_BLACK);  // use literal so black is definitely 0x0000
    tft.setTextColor(myScreen.FG_COLOR);
    tft.setCursor(0,0);

    tft.setFont(&fonts::Font4);
    tft.printf("CLOCK SETUP...\n");
    tft.setFont(&fonts::Font2);

    SPI.begin(48,41,47); //SPI pins per https://homeding.github.io/boards/esp32s3/panel-4848S040.htm
    tft.printf("SD Card mounting...");

    if(!SD.begin(42)){  //CS=42 per https://homeding.github.io/boards/esp32s3/panel-4848S040.htm
      tft.setTextColor(TFT_RED);
      tft.println("SD Mount Failed. Halted!");
      while(true);
      #ifdef _DEBUG
        Serial.println("SD mount failed... ");
      #endif
  
    } 
    else {
      #ifdef _DEBUG
        Serial.println("SD mounted... ");
      #endif
  
      tft.setTextColor(TFT_GREEN);
      tft.println("OK.\n");
  
      #ifdef _DEBUG
        Serial.print("Sensor data loaded? ");
      #endif
  
    }
  
    myScreen.num_images = countFilesInDirectory("/PICS");
    myScreen.lastPic=-1;
  
  tft.setTextColor(myScreen.FG_COLOR);

  tft.printf("Total heap: %d\n", ESP.getHeapSize());
  tft.printf("Free heap: %d\n", ESP.getFreeHeap());
  tft.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  tft.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  delay(1000);

}


void clockLoop() {
    getWeather();

    fcnCheckScreen();
}

void fcnDrawPic(byte luminosity) {

//    if ((myScreen.lastPic>0 && myScreen.time_lastPic>0) && (myScreen.time_lastPic == myScreen.t || myScreen.t - myScreen.time_lastPic < myScreen.int_Pic_MIN*60)) return;
    
    
    //choose a new random picture
    myScreen.lastPic = random(0,myScreen.num_images-1);
    myScreen.time_lastPic=I.currentTime;

    String fn;
    fn= "/PICS/" + getNthFileName("/PICS",myScreen.lastPic);

    //if the hour is between 9p and 7a, use a luminosity of 50
    if (hour()>=22 || hour()<=7) {
      if (luminosity==0) luminosity = 50;
    } else if (luminosity==0) {
      luminosity = 100;
    }
    drawBmp(fn.c_str(),0,0,-1,luminosity);
  
}


    //add touchscreen support. If the screen is touched, first determine where, and then compare to the current screen number to do decide what to do.
    //screen 0 is the main clock screen
    //screen 1 shows the current status, including IP address, last weather update time, and screen brightness. A button on the bottom right of the screen triggers a broadcast of my presence to all sensors
    //screen 2 shows the values for the current weather, and has a button at the bottom right of the screen to request an update now.
void checkTouchScreen() {
  if (tft.getTouch(&myScreen.touchX, &myScreen.touchY)) {
    if (myScreen.touchX > 0 && myScreen.touchY > 0 && myScreen.touchX < 480 && myScreen.touchY < 480) {
      uint32_t now = millis();
      if (now - myScreen.lastTouchMS >= myScreen.touchDebounceMS) {
        myScreen.lastTouchMS = now;
        //touched the screen
        if (myScreen.screenNum==0) {
          //touched the main clock screen
          myScreen.screenNum=1;
          myScreen.screenChangeTimer=myScreen.screenChangeTimerMax + I.currentTime;
        }
        else if (myScreen.screenNum==1) {
          //touched the status screen
          myScreen.screenNum=2;
          myScreen.screenChangeTimer=myScreen.screenChangeTimerMax + I.currentTime;
        }
        else if (myScreen.screenNum==2) {
          //touched the weather screen
          myScreen.screenNum=0;
          myScreen.screenChangeTimer=myScreen.screenChangeTimerMax + I.currentTime;
        }
      }
    }
  }

  //change to unreasonable values
  myScreen.touchX = 1000;
  myScreen.touchY = 1000;



}


void fcnCheckScreen() {
  checkTouchScreen();

  // Re-assert 16-bit RGB565 so all drawing (fillScreen, text, BMP) is correct.
  // Something after init (e.g. shared tftPrint/clear/setTextFont) can alter panel state.
  tft.setColorDepth(16);

  if (myScreen.screenChangeTimer<I.currentTime && myScreen.screenNum!=0) myScreen.screenNum=0;

//decide what to draw now. Ignore screen 0, as it is the main clock screen and is the default.
  switch (myScreen.screenNum) {
    case 0:
      fcnDrawClock();
      return;
      break;
    case 1:
      fcnDrawStatusScreen();
      return;
      break;
    case 2:
      fcnDrawWeatherScreen();
      return;
      break;
  }
}

byte fcnDrawStatusScreen() {

  if ((myScreen.screenNum==1 && myScreen.oldScreenNum!=1 && myScreen.screenChangeTimer>I.currentTime) ) {
    myScreen.oldScreenNum=myScreen.screenNum;
  }   else return 0;

  //clear tft
  tft.fillScreen(myScreen.BG_COLOR);
  tft.setTextColor(myScreen.FG_COLOR);
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);
  byte y = 0;
  tft.setFont(&fonts::Font2);
  tft.drawString("Status",0,y);
  y += tft.fontHeight(&fonts::Font2)+2;
  tft.setFont(&fonts::Font0);
  tft.drawString("Device Name: " + (String) Prefs.DEVICENAME,0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Device IP: " + (String) WiFi.localIP().toString(),0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Report Time: " + (String) dateify(I.currentTime,"mm/dd/yyyy hh:nn:ss"),0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Alive Since: " + (String) dateify(I.ALIVESINCE,"mm/dd/yyyy hh:nn:ss"),0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Last Reset Time: " + (String) dateify(I.lastResetTime,"mm/dd/yyyy hh:nn:ss"),0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("-----------------------\n",0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("UDP Messages Received today: " + (String) I.UDP_RECEIVES,0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("UDP Messages Sent today: " + (String) I.UDP_SENDS,0,y);

  return 1;
}

byte fcnDrawWeatherScreen() {

  if ((myScreen.screenNum==2 && myScreen.oldScreenNum!=2 && myScreen.screenChangeTimer>I.currentTime) ) {
    myScreen.oldScreenNum=myScreen.screenNum;
  }   else return 0;

  //clear tft
  tft.fillScreen(myScreen.BG_COLOR);
  tft.setTextColor(myScreen.FG_COLOR);
  tft.setCursor(0,0);
  tft.setTextDatum(TL_DATUM);
  byte y = 0;
  tft.setFont(&fonts::Font2);
  tft.drawString("Weather",0,y);
  y += tft.fontHeight(&fonts::Font2)+2;
  tft.setFont(&fonts::Font0);
  tft.drawString("Current Temperature: " + (String) myScreen.wthr_currentTemp + "F",0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Current Weather: " + weatherID2string(myScreen.wthr_currentWeatherID),0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("High Temperature: " + (String) myScreen.wthr_DailyLow + " - " + (String) myScreen.wthr_DailyHigh + "F",0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Daily Precipitation: " + (String) myScreen.wthr_DailyPoP + "%",0,y);
  y += tft.fontHeight(&fonts::Font0)+2;
  tft.drawString("Weather read at: " + (String) dateify(myScreen.time_lastWeather,"hh:nn"),0,y);

  
  return 1;
}


byte fcnDrawClock() {
  //draw the clock if
  //1. the screen number is 0, but the last screen num was not
  //2. the screen number is 0, and we're in a new minute since last draw (avoids missing second()==0 if loop is busy)
  //3. the screen number is not 0, and the screen change timer has expired

  if ((myScreen.screenNum==0 && myScreen.oldScreenNum!=0) || (myScreen.screenNum==0 && (myScreen.lastclockChange==0 || minute(I.currentTime) != minute(myScreen.lastclockChange))) || (myScreen.screenNum!=0 && myScreen.screenChangeTimer<I.currentTime)) {
    myScreen.screenNum=0;
    myScreen.oldScreenNum=myScreen.screenNum;
    myScreen.screenChangeTimer=0;
    myScreen.lastclockChange=I.currentTime;    
  }   else return 0;

  fcnDrawPic();


  uint16_t Xpos,Ypos;
  String st;
  Xpos = 475;
  Ypos=475;
  tft.setFont(&fonts::Font8);

  tft.setCursor(Xpos,Ypos);
  tft.setTextDatum(BR_DATUM);
  tft.setTextColor(myScreen.FG_COLOR);
  tft.drawString(dateify(I.currentTime,"hh:nn"),Xpos,Ypos);

    
  byte fntsz = 4;
  Xpos=5;
  Ypos=5;
  tft.setTextDatum(TL_DATUM);    
  tft.setCursor(Xpos,Ypos);
  tft.setFont(&fonts::Font4);
  st = (String) dateify(I.currentTime,"dow") + ",  " + (String) fcnMMM(I.currentTime,false) + " " + (String) day();
  tft.drawString(st,Xpos,Ypos);
  Ypos += (tft.fontHeight(&fonts::Font4)+2);
  tft.setFont(&fonts::Font0);
  st=getNthFileName("/PICS",myScreen.lastPic);
  st.replace("_["," from ");
  st.replace("]","");
  st.replace(".bmp","");
  tft.drawString(st,Xpos,Ypos);


  Xpos=475;
  Ypos=5;
  tft.setTextDatum(TR_DATUM);    
  tft.setFont(&fonts::Font4);
  st = "Planetary Status";
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font4)+2);
  tft.setFont(&fonts::Font2);
  st = "PREDICTED HAZARDS";
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font2);
  st = (String) myScreen.wthr_DailyPoP + "% precipitation risk";
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font2);
  st = "Environment: " + weatherID2string(myScreen.wthr_DailyWeatherID);
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font2);
  st = "Temperature: " + (String) myScreen.wthr_DailyLow + " - " + (String) myScreen.wthr_DailyHigh + "F";
  tft.drawString(st,Xpos,Ypos);


  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font2);
  st = "CURRENT HAZARDS";
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font2);
  st = "Condition: " +  weatherID2string(myScreen.wthr_currentWeatherID);
  tft.drawString(st,Xpos,Ypos);

  Ypos += (tft.fontHeight(&fonts::Font2)+2);
  tft.setFont(&fonts::Font4);
  st = "" + (String) myScreen.wthr_currentTemp + "F";
  tft.drawString(st,Xpos,Ypos);
  

  tft.setTextDatum(TL_DATUM);
  myScreen.time_lastClock=I.currentTime;

  return 1;
}

    
String weatherID2string(uint16_t weatherID) {
  switch(weatherID) {
    case 504: {
      return (String) "STORM";
    }
    case 603: {
      return (String) "Blizzard";
    }
    case 611: {
      return (String) "Ice+Snow";
    }
    case 600: {
      return (String) "Light Snow";
    }
    case 601: {
      return (String) "Mod Snow";
    }
    case 602: {
      return (String) "Heavy Snow";
    }
    case 311: {
      return (String) "Sleet";
    }
    case 201: {
      return (String) "Thunderstorm";
    }
    case 200: {
      return (String) "Severe Thunderstorm";
    }
    case 301: {
      return (String) "Drizzle";
    }
    case 500: {
      return (String) "Light Rain";
    }
    case 501: {
      return (String) "Mod Rain";
    }
    case 502: {
      return (String) "Heavy Rain";
    }
    case 761: {
      return (String) "Dust";
    }
    case 800: {
      return (String) "Sunny";
    }
    case 801: {
      return (String) "Mostly Sunny";
    }
    case 802: {
      return (String) "Light Clouds";
    }
    case 803: {
      return (String) "Cloudy";
    }
    case 804: {
      return (String) "Overcast";
    }
    case 741: {
      return (String) "Fog";
    }

  }
    
  return (String) weatherID;
}
    


    void drawBmp(const char *filename, int16_t x, int16_t y,  int32_t alpha, uint8_t luminosity) {
    //alpha is the color that will "transparent" the image
    //luminosity is the percentage of the original luminosity to use, 100 is original, 0 is black, 255 is max
      if ((x >= tft.width()) || (y >= tft.height())) return;
    
      File bmpFS;
    
        // Open requested file on SD card
      bmpFS = SD.open(filename, "r");
    
      if (!bmpFS)       {
        tft.print(filename);
        return;
      }
    
      uint32_t seekOffset;
      uint16_t w, h, row;
      uint8_t  r, g, b;
    
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
          uint16_t outBuffer[w];  // separate 16-bit row; in-place overwrite left R byte and corrupted next pixels (red tint)

          for (row = 0; row < h; row++) {

            bmpFS.read(lineBuffer, sizeof(lineBuffer));
            uint8_t*  bptr = lineBuffer;
            uint16_t* tptr = outBuffer;
            for (uint16_t col = 0; col < w; col++)
            {
              b = *bptr++;
              g = *bptr++;
              r = *bptr++;
              uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
              if (alpha >= 0 && rgb565 == (uint16_t)alpha) {
                *tptr++ = myScreen.BG_COLOR;
                continue;
              }
              scaleColor(r, g, b, luminosity);
              *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            }

            tft.pushImage(x, y--, w, 1, outBuffer);
          }
          tft.setSwapBytes(oldSwapBytes); // restore panel default; must match initClock480X480Graphics()
          //Serial.print("Loaded in "); Serial.print(millis() - startTime);
          //Serial.println(" ms");
        }
        else tft.println("BMP format not recognized.");
      }
      bmpFS.close();
    }
    
    void scaleColor(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t luminosity) {
        //if the scaling factor increases the color value above 255, clamp it at 255
        r = min((uint16_t)r*luminosity/100, 255);
        g = min((uint16_t)g*luminosity/100, 255);
        b = min((uint16_t)b*luminosity/100, 255);
    }

        String getNthFileName(String dirName, uint8_t filenum) {
        int count = 0;
        File dir = SD.open(dirName);
      
        if (!dir) {
          return "ERROR -1"; // Return -1 to indicate an error - could not open dir
        }
      
        if (!dir.isDirectory()) {
          return "ERROR -2"; //Return -2 to indicate that the path is not a directory
        }
      
        while (true) {
          File entry = dir.openNextFile();
          if (!entry) {
            // no more files
            break;
          }
          if (!entry.isDirectory()) { // Count only files, not subdirectories
              if (count==filenum) {
                  String st = entry.name();
                  entry.close();
                  dir.close();
                  return st;
              }
            count++;
          }
      
          entry.close();
        }
        dir.close();
        return "";
      }
      
      
      int countFilesInDirectory(String dirName) {
        int count = 0;
        File dir = SD.open(dirName);
      
        if (!dir) {
          return -1; // Return -1 to indicate an error - could not open dir
        }
      
        if (!dir.isDirectory()) {
          return -2; //Return -2 to indicate that the path is not a directory
        }
      
        while (true) {
          File entry = dir.openNextFile();
          if (!entry) {
            // no more files
            break;
          }
          if (!entry.isDirectory()) { // Count only files, not subdirectories
            count++;
          }
      
          entry.close();
        }
        dir.close();
        return count;
      }
      

bool getWeather() {
    if (myScreen.time_lastWeather>0 && I.currentTime-myScreen.time_lastWeather < myScreen.int_Weather_MIN*60 ) return false; //not time to update

    int16_t weatherServerIndex = Sensors.nextServerIndex(0, true);
    if (weatherServerIndex == -1) return false;

    ArborysDevType* weatherServer = Sensors.getDeviceByDevIndex(weatherServerIndex);
    if (weatherServer == NULL) return false;
    if (weatherServer->IP == IPAddress(0,0,0,0)) return false;


    WiFiClient wfclient;
    HTTPClient http;

    if(WiFi.status()== WL_CONNECTED){
        String payload;
        String tempstring;
        int httpCode=404;
        

        tempstring = "http://" + weatherServer->IP.toString() + "/REQUESTWEATHER?hourly_temp=0&hourly_weatherID=0&daily_weatherID=0&daily_tempMax=0&daily_tempMin=0&daily_pop=0&sunrise=0&sunset=0";

        http.useHTTP10(true);    
        http.begin(wfclient,tempstring.c_str());
        httpCode = http.GET();
        payload = http.getString();
        http.end();

        if (!(httpCode >= 200 && httpCode < 300)) return false;
        
        myScreen.wthr_currentTemp = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
        
        myScreen.wthr_currentWeatherID = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        myScreen.wthr_DailyWeatherID = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
        
        myScreen.wthr_DailyHigh = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
        
        myScreen.wthr_DailyLow = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
        
        myScreen.wthr_DailyPoP = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
        
        myScreen.wthr_sunrise = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

        myScreen.wthr_sunset = payload.substring(0, payload.indexOf(";",0)).toInt(); 
        
        myScreen.time_lastWeather = I.currentTime;
        return true;
    }
    return false;
}


uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

String fcnMMM(time_t t, bool abb) {
  if (abb) {
    if (month(t) == 1) return "Jan";
    if (month(t) == 2) return "Feb";
    if (month(t) == 3) return "Mar";
    if (month(t) == 4) return "Apr";
    if (month(t) == 5) return "May";
    if (month(t) == 6) return "Jun";
    if (month(t) == 7) return "Jul";
    if (month(t) == 8) return "Aug";
    if (month(t) == 9) return "Sep";
    if (month(t) == 10) return "Oct";
    if (month(t) == 11) return "Nov";
    if (month(t) == 12) return "Dec";

  } else {
    if (month(t) == 1) return "January";
    if (month(t) == 2) return "February";
    if (month(t) == 3) return "March";
    if (month(t) == 4) return "April";
    if (month(t) == 5) return "May";
    if (month(t) == 6) return "June";
    if (month(t) == 7) return "July";
    if (month(t) == 8) return "August";
    if (month(t) == 9) return "September";
    if (month(t) == 10) return "October";
    if (month(t) == 11) return "November";
    if (month(t) == 12) return "December";
  }
    return "???";
}

#endif //ifdef _ISCLOCK480X480