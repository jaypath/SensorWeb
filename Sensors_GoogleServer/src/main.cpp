//#define _DEBUG 99
#define ARDNAME "GoogleServer"
#define _USETFT
#define _USETOUCH
//#define NOISY true
#define _USEOTA


#include <Arduino.h>
#include "esp32/spiram.h"
#include <String.h>

#include <WiFi.h>
#include <timesetup.hpp>
//#include <WebServer.h>
//#include <HTTPClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SDCard.hpp>
#include <utility.hpp>


#ifdef _USEOTA
  #include <ArduinoOTA.h>
#endif

#include <Wire.h>

#include <ESP_Google_Sheet_Client.h>

#include <globals.hpp>


// extern String GsheetID; //file ID for this month's spreadsheet
// extern String GsheetName; //file name for this month's spreadsheet
// extern String lastError;


extern WebServer server;

extern byte OldTime[5];
extern int Ypos ;
extern char tempbuf[70];     
extern time_t ALIVESINCE;  
extern String GsheetID; //file ID for this month's spreadsheet
extern String GsheetName; //file name for this month's spreadsheet
extern String lastError;
extern const uint8_t temp_colors[104];

extern LGFX tft;            // declare display variable

// Variables for touch x,y
extern int32_t touch_x,touch_y;


#define ESP_GOOGLE_SHEET_CLIENT_USE_PSRAM
#define PROJECT_ID "arduinodatalog-415401"
//Service Account's client email
#define CLIENT_EMAIL "espdatalogger@arduinodatalog-415401.iam.gserviceaccount.com"
//Service Account's private key
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC6e4DR7dGHAwnx\nFe9+B/+RyHGOgflQRVSu0G0xX50CQqLBA5rmVmGsqgLY4lZbFXwYF9NNQtjBMbw/\n5aoTYMRmFjzNETt5hOXRW7xE+VH/krnNZgqNqvOarj8dEEupK9LkknlA/8qql8qP\ng7XEIZLir06ZLDDKqrFiJOk5TPUUVit0Jz+SPRWv3UgAnVk5zgGqwIsil+Q7GXPq\nVmtDOieLL468cz7Bhbvm61q16+WsRf091kiP02BxNBc7Ygf/mea7l3b50GZahCmO\nBVD7LdejUDM8cpxs4lJhVm9OXagQwn64FW1pmBXLaw4FuM4gl/wZsqV60qV8kqo1\nDHZw0NIRAgMBAAECggEAB4ytUHpkdpb9N5wV6vQVkPQkokKBapIy+WSNXh/HAEbc\ntkwQQOHXgJUtqjSADt4P3P0WcCjcE/awroTMic4rp08gSSRI5EyYvfz88k0maF6L\nHH1UgSueAvmpyxI8QOoZ9qhWJbcxUA6W/BBGyxzZmKkkmUOCamsGhUDVqwziOV9p\nqpcpmZKnxlbLtWUaa5YNZCGaVG48KN5WqvUqACrGvQag2tprY/DUvNOPFd+ulAki\nOEFBK/q00Ru3A5l8c+yZRhy5U86nOTZKeDMBIePiEd91rfXHUIwspJ6bRO+zE2UO\nIg3A6TStb6daxyHLD2Dj0SL2exJEpnDp/NeTu+lkYQKBgQDw3Tr+/Z0JhJaIa0je\nTVNWErAJL7Oa87V12L0iJJHR9c0F5Zo++5EF1+uBm+l+raYoaTtEmi/vORE0lOPg\nS/W0l7Z7WPa9JfJCZBNpqIHOSaP+6UP0dUHk/pz21u8+bP1Z01RBvi7Ew21xXEM1\n+JTPhDtE6fEW8jrIqMbZBn9fsQKBgQDGM2+49+9FynTJWKthEJq7A6q9rvt1bhtB\nW8KAgHzKkeHKCVzWj/e+0VhlZrcnDnnjFdJXFEduRSpCycJQBKNKTt1EmBoU5g06\nFweit+FOwSivw7ux1PPUHFSp6ARzK70iCWN9ji3cQVGLdtzfOJU82f5ssKAqPUR8\n5H9nWNqQYQKBgBl835xSDAcQz7kZ2Tkk55epHJWsRY41EdOpnsH5KrEUGKDyHfNi\nPYNnyNULQZcVGwsVr57fzgi7ejWdN8vpXdPBZh8BWALF/C/IVUGOAkZpBoCYAIfi\nzJlF1ChOsDxj3h9ePIFEdcB+iZtATyBr8JtQ+9CcDNYHxe6r5XbbuCjRAoGBAIh5\nBmawoZrGqt+xJGBzlHdNMRXnFNJo/G9mhWkCD+tTw8rf44MCIq7Lazh3H4nPF/Jb\nJjg7iGvPSCgw0JFUgDM8VnNS4DKfrV/gV6udPZCCxEcyWV07qqDU2R8c2WOMLHDx\nUgY0DjPo7gM/1xoE1g3OdLfWbpJnGW99zpQUxHpBAoGAeOoiU25hHxpuuyGZ8d6l\nm/AQAz+ORklGyxOi1HnvZr1y1yVK8+f1sNSTsxe3ZFk4lmeUQdhJWk6/75ma86Fx\nPhV42ri06DSXvU/XUFL8IexqFGrxqet3v104tsUY2PBhyzW/aoIDh2lct2GePjx2\nNyCunqbb3vTNqNcIKOCfLXQ=\n-----END PRIVATE KEY-----\n";
#define USER_EMAIL "jaypath@gmail.com"


int8_t hourly_temp[24];
int hourly_weatherID[24];
int daily_weatherID[5];
int daily_tempMAX[5];
int daily_tempMIN[5];

byte clockHeight = 0;

extern SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
extern struct ScreenFlags ScreenInfo;

extern byte OldTime[5];
extern int Ypos ;
extern char tempbuf[70];     
extern time_t ALIVESINCE;

int fcn_write_sensor_data(byte i, int y);
void clearTFT();
void drawScreen();
void drawScreen_List();
void drawScreen_Clock();
void drawScreen_Weather();
void drawScreen_Settings();
void drawButton(String b1, String b2,  String b3,  String b4,  String b5,  String b6);

void getWeather();

String file_findSpreadsheetIDByName(String sheetname, bool createfile);
bool file_deleteSpreadsheetByID(String fileID);
String file_createSpreadsheet(String sheetname, bool checkfile);
bool file_createHeaders(String sheetname,String Headers);
void tokenStatusCallback(TokenInfo info);
void file_deleteSpreadsheetByName(String filename);

void handleNotFound();
void handlePost();
void handleRoot();
void handleCLEARSENSOR();
void handleTIMEUPDATE();
bool SendData();
bool uploadData(bool gReady);


bool file_uploadSensorData(void);

uint16_t temp2color(int temp, bool autoadjustluminance = false);

time_t t=0;

void color2RGB(uint32_t color,byte* R, byte* G, byte *B, bool is24bit=false);
void drawBmp(const char *filename, int16_t x, int16_t y, int32_t transparent = 0xFFFF);
uint8_t color2luminance(uint32_t color, bool is16bit=true);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1, String headstr="", String tailstr="");
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x=-1,int y=-1, String headstr="", String tailstr="");


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

//returns 16 bit color
uint16_t temp2color(int temp, bool autoadjustluminance) {


  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }



  double r,g,b;
  if (autoadjustluminance && false) {

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




void clearTFT() {
  tft.fillScreen(ScreenInfo.BG_COLOR);                 // Fill with black
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setCursor(0,0);
  Ypos = 0;
}

void setup()
{

  SPI.begin(39, 38, 40, -1); //sck, MISO, MOSI

 tft.init();

  // Setting display to landscape
  tft.setRotation(2);

  clearTFT();
  tft.setTextFont(2);

  tft.printf("Running setup\n");



  tft.printf("Mounting SD: ");

  if(!SD.begin(41)){  //CS=41
      tft.setTextColor(TFT_RED);
      tft.printf("SD Mount Failed\n");      
      while(false);
  } 
  else {
    tft.setTextColor(TFT_GREEN);
    tft.printf("SD Mount OK\n");
  }
  tft.setTextColor(ScreenInfo.FG_COLOR);

  tft.printf("Reading Variables:\n");
  if (!readSensorsSD("/Data/SensorBackup.dat")) {
      
      tft.setTextColor(TFT_RED);
      tft.printf("-Failed to read Sensor Data\n");
      
  } else {
     tft.setTextColor(TFT_GREEN);
      tft.printf("-Read Sensor Data\n");
  }


  if (!readScreenInfoSD()) {      
      tft.setTextColor(TFT_RED);
      tft.printf("-Failed to read settings\n");
      
  } else {
     tft.setTextColor(TFT_GREEN);
      tft.printf("-Read settings\n");
  }

  tft.setTextColor(ScreenInfo.FG_COLOR, ScreenInfo.BG_COLOR);


  tft.printf("Specs:\n");
  // Get flash size and display
  tft.printf("-FLASH size: %d bytes\n",ESP.getMinFreeHeap());
  // Initialize PSRAM (if available) and show the total size
  if (psramFound()) {
     tft.setTextColor(TFT_GREEN);
    tft.printf("-PSRAM size: %d bytes\n",heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  } else {
    tft.setTextColor(TFT_RED);
    tft.printf("-No PSRAM found.\n");
  }

  tft.setTextColor(ScreenInfo.FG_COLOR);


  tft.printf("Connecting WiFi");

#ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Serial started.");
#endif

    // Set auto reconnect WiFi or network connection
#if defined(ESP32) || defined(ESP8266)
    WiFi.setAutoReconnect(true);
#endif

// Connect to WiFi or network
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    multi.addAP(WIFI_SSID, WIFI_PASSWORD);
    multi.run();
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

#ifdef _DEBUG
    Serial.print("Connecting to Wi-Fi");
#endif
    unsigned long ms = millis();
    tft.setTextColor(TFT_RED);
    while (WiFi.status() != WL_CONNECTED)
    {
      #ifdef _DEBUG
              Serial.print(".");
      #endif
      tft.printf(".");
      delay(500);
      #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        if (millis() - ms > 10000)
        break;
      #endif
    }
    tft.setTextColor(ScreenInfo.FG_COLOR);    

     tft.setTextColor(TFT_GREEN);
    tft.printf("\n%s\n",WiFi.localIP().toString().c_str());
    tft.setTextColor(ScreenInfo.FG_COLOR);

    #ifdef _DEBUG
        Serial.println();
        Serial.print("Connected with IP: ");
        Serial.println(WiFi.localIP());
        Serial.println();
    #endif


    // The WiFi credentials are required for Pico W
    // due to it does not have reconnect feature.
    #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        GSheet.clearAP();
        GSheet.addAP(WIFI_SSID, WIFI_PASSWORD);
        // You can add many WiFi credentials here
    #endif


    tft.printf("Init GSheets... ");

    // Set the callback for Google API access token generation status (for debug only)
    GSheet.setTokenCallback(tokenStatusCallback);
    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(300);

    //Begin the access token generation for Google API authentication
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    // In case SD/SD_MMC storage file access, mount the SD/SD_MMC card.
    // SD_Card_Mounting(); // See src/GS_SDHelper.h

    // Or begin with the Service Account JSON file that uploaded to the Filesystem image or stored in SD memory card.
    // GSheet.begin("path/to/serviceaccount/json/file", esp_google_sheet_file_storage_type_flash /* or esp_google_sheet_file_storage_type_sd */);
    tft.setTextColor(TFT_GREEN);
    tft.printf("OK\n");
    tft.setTextColor(ScreenInfo.FG_COLOR);
    
    tft.printf("Connect to GSheets... ");
    if (!GSheet.ready()) {
      tft.setTextColor(TFT_RED);
      tft.printf("Failed!\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);
    } else {
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
    }

  #ifdef _USEOTA
    tft.printf("Setting OTA... ");

    ArduinoOTA.setHostname(ARDNAME);
    ArduinoOTA.onStart([]() {
      #ifdef _DEBUG
      Serial.println("OTA started");
      #endif
      #ifdef _USETFT
        clearTFT();
        tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
        tft.setCursor(0,0);
        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(4);
        tft.println("Receiving OTA:");
        tft.setTextDatum(TL_DATUM);
      
      tft.drawRect(5,tft.height()/2-5,tft.width()-10,10,ScreenInfo.FG_COLOR);
      #endif  
    });
    ArduinoOTA.onEnd([]() {
      #ifdef _DEBUG
      Serial.println("OTA End");
      #endif
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      #ifdef _DEBUG
          Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
      #endif
      #ifdef _USETFT
        //String strbuff = "Progress: " + (100*progress / total);
        if (progress % 5 == 0) tft.fillRect(5,tft.height()/2-5,(int) (double (tft.width()-10)*progress / total),10,ScreenInfo.FG_COLOR);
        

        #endif

    });
    ArduinoOTA.onError([](ota_error_t error) {
      #ifdef _DEBUG
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      #endif
      #ifdef _USETFT
        clearTFT();
        tft.setCursor(0,0);
        String strbuff;
        strbuff = (String) "Error[%u]: " + (String) error + " ";
        tft.print(strbuff);
        if (error == OTA_AUTH_ERROR) tft.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) tft.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) tft.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) tft.println("Receive Failed");
        else if (error == OTA_END_ERROR) tft.println("End Failed");
      #endif
    });
    ArduinoOTA.begin();
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
      
  #endif




    tft.printf("Start Server... ");

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/POST", handlePost);   
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.onNotFound(handleNotFound);
    server.begin();

      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    
    

    tft.printf("Set up TimeClient... ");

    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST or EDT
    
    OldTime[0] = 61; //force sec update.
    OldTime[2] = 61;
    OldTime[2] = 61;
    OldTime[3] = weekday(); //do not force daily update

    t = now();
      tft.setTextColor(TFT_GREEN);
      tft.printf("OK\n");
      tft.setTextColor(ScreenInfo.FG_COLOR);    

    tft.printf("Init globals... ");


    //init globals
    ScreenInfo.BG_luminance = color2luminance(ScreenInfo.BG_COLOR,true);
    clockHeight = tft.fontHeight(8)+6+tft.fontHeight(4)+1;
    
    //redraw everything
    ScreenInfo.RedrawClock = 0; //seconds left before redraw
    ScreenInfo.RedrawWeather = 0; //seconds left before redraw
    ScreenInfo.RedrawList=0; //SECONDS left for this screen (then  goes to main)
    

    getWeather();
    ALIVESINCE = t;

    tft.setTextColor(TFT_GREEN);
    tft.printf("OK\n");
    tft.setTextColor(ScreenInfo.FG_COLOR);    


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
          if (transparent>0 && transparent<=0xFFFF) {
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

void drawScreen()
{
  bool isTouch = false; //no touch

//  1. get touch info and update as appropriate
#ifdef _USETOUCH
  if (tft.getTouch(&touch_x, &touch_y)) {
    isTouch = true;
    
    byte buttonNum = getButton(touch_x, touch_y,ScreenInfo.ScreenNum);
    if (buttonNum>0) {
      tft.fillCircle(touch_x,touch_y,12,TFT_GOLD);
      delay(250);
    }
    

    if (ScreenInfo.ScreenNum == 0) {
      ScreenInfo.ScreenNum = 1; //on main screen, this means go to list screen
      ScreenInfo.ForceRedraw=1; //force a screen redraw
      ScreenInfo.snsListLastTime=0; //reset the list
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
            ScreenInfo.ForceRedraw=1; //force a screen redraw at main
            break;
          }
          case 3:
          {
            ScreenInfo.lastUploadTime=0; //force an upload
            ScreenInfo.snsListLastTime=0;
            ScreenInfo.snsLastDisplayed=0;
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
            ScreenInfo.snsListLastTime=0;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 5:
          {
            ScreenInfo.showAlarmedOnly = 0;
            ScreenInfo.snsListLastTime=0;
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
        byte buttonNum = getButton(touch_x, touch_y,ScreenInfo.ScreenNum);
        //buttons on setting screen are: 
        /*
          main
          list
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
            ScreenInfo.ScreenNum=0; //force a screen redraw at main
            ScreenInfo.ForceRedraw=1;
            break;
          }
          case 2:
          {
            ScreenInfo.ScreenNum = 1; //on main screen, this means go to list screen
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            ScreenInfo.snsListLastTime=0; //reset the list
            break;
          }
          case 3:
          {
            ScreenInfo.settingsLine=(ScreenInfo.settingsLine+1)%10; //up to 10 lines are selectable
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
            ScreenInfo.settingsSelected=1;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
          case 6:
          {
            ScreenInfo.settingsSelected=-1;
            ScreenInfo.ForceRedraw=1; //force a screen redraw
            break;
          }
        
        }
      }
      }
    }


    #ifdef _DEBUG
      Serial.printf("Touch: X= %d, Y= %d\n",touch_x, touch_y);
    #endif
  }
  else {
    isTouch = false;
    touch_x = 0;
    touch_y = 0;
  }
#elif
    isTouch = false;
    touch_x = 0;
    touch_y = 0;

#endif



if (ScreenInfo.ForceRedraw==0 && ScreenInfo.RedrawList==0 && ScreenInfo.ScreenNum>0) {
  ScreenInfo.ForceRedraw=1;
  ScreenInfo.ScreenNum=0;
  ScreenInfo.RedrawClock=60;
  ScreenInfo.RedrawWeather=ScreenInfo.WeatherTime*60;;
}


switch (ScreenInfo.ScreenNum) {
  case 0:
  {
    if (ScreenInfo.ForceRedraw) {
      drawScreen_Clock();
      drawScreen_Weather();
     
    } else {
      if (ScreenInfo.RedrawClock==0)  drawScreen_Clock();
      if (ScreenInfo.RedrawWeather==0) drawScreen_Weather();
    }

        
    break;
  }
  case 1:
  {
    if (ScreenInfo.ForceRedraw) {
      #ifdef _DEBUG
        Serial.printf("Redraw list...\n");
      #endif
      drawScreen_List();     
      #ifdef _DEBUG
        Serial.printf(" ok.\n");
      #endif

    }
    else if (ScreenInfo.RedrawList==0) ScreenInfo.ScreenNum=0;

    break;
  }
  case 2:
  {
    if (ScreenInfo.ForceRedraw)    drawScreen_Settings();
    else if (ScreenInfo.RedrawList==0) ScreenInfo.ScreenNum=0;
    break;
  }
}

ScreenInfo.ForceRedraw = 0;

}


bool uploadData(bool gReady) {
    //check upload status here.

  if (ScreenInfo.lastUploadSuccess && t-ScreenInfo.lastUploadTime<(ScreenInfo.uploadInterval*60)) return true; //everything is ok, just not time to upload!

  if (gReady==false) return false; //gsheets not ready!

  #ifdef _DEBUG
    Serial.println("uploading to sheets!");
  #endif

  if (file_uploadSensorData()==false) {
    ScreenInfo.lastUploadSuccess=false;
    ScreenInfo.uploadFailCount++; //an actual error prevented upload!
  }
  else {
    ScreenInfo.lastUploadSuccess=true;
    ScreenInfo.lastUploadTime = t;
    ScreenInfo.uploadFailCount=0; //upload success!
  }

  return true;
}

void loop()
{
    //Call ready() repeatedly in loop for authentication checking and processing

  t = now();
  updateTime(1,0); //just try once
  timeClient.update();
  
  #ifdef _USEOTA
    ArduinoOTA.handle();
  #endif
  
  server.handleClient();

  drawScreen();

  bool ready = GSheet.ready(); //maintains authentication
  uploadData(ready);
  
  

  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second

    if (ScreenInfo.RedrawClock>0) ScreenInfo.RedrawClock--;
    if (ScreenInfo.RedrawWeather >0) ScreenInfo.RedrawWeather--;
    if (ScreenInfo.RedrawList >0) ScreenInfo.RedrawList--;

      
      tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  }      
      
  


  if (OldTime[1] != minute()) {
  OldTime[1] = minute();
  //do stuff every minute
    ScreenInfo.RedrawClock=0; //synchronize to time

    //expire old sensors and characterize summary stats
    ScreenInfo.isFlagged = 0;
    ScreenInfo.isExpired=0;
    ScreenInfo.isSoilDry=0;
    ScreenInfo.isHeat=0;
    ScreenInfo.isCold=0;

    for (byte j=0;j<SENSORNUM;j++) {
      if (isSensorInit(j)==false) continue;
      bitWrite(Sensors[j].localFlags,1,0); //not expired... will change it back if needed.

      if (t-Sensors[j].timeLogged>3*Sensors[j].SendingInt) {
        bitWrite(Sensors[j].localFlags,1,1); //is expired
        ScreenInfo.isExpired++;
      } else {
        if (Sensors[j].Flags&1) {
          ScreenInfo.isFlagged++;
          if (Sensors[j].Flags&0b10000000) ScreenInfo.isCritical++; //a critical sensor is flagged!
          if (Sensors[j].snsType ==1 || Sensors[j].snsType ==4 || Sensors[j].snsType ==10 || Sensors[j].snsType ==14 ||  Sensors[j].snsType ==17) {
            if (Sensors[j].Flags&0b100000) ScreenInfo.isHot++;
            else ScreenInfo.isCold++;
            continue;
          }
          if (Sensors[j].snsType ==3) {
            ScreenInfo.isSoilDry++;
            continue;
          }
        }
      }
    }
    
  }

  if (OldTime[2] != hour()) {
    OldTime[2] = hour(); 
    getWeather();
    writeSensorsSD("/Data/SensorBackup.dat");
    storeScreenInfoSD();

    SendData(); //update server about our status
  }

  if (OldTime[3] != weekday()) {
    
    ScreenInfo.uploadFailCount = 0;

    OldTime[3] = weekday();
    //daily

    if (day()==1) {
      GsheetName = ""; //reset the sheet name on the first of the month!
      GsheetID = "";
    }

    for (byte j=0;j<SENSORNUM;j++) {
      Sensors[j].snsValue_MAX = -10000;
      Sensors[j].snsValue_MIN = 10000;
      
      if (t-Sensors[j].timeLogged>86400 && bitRead(Sensors[j].localFlags,1)==0) initSensor(j); //expire one day old sensors that are not critical

    }
  }
}




void getWeather() {
  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    int httpCode=404;

    tempstring = "http://192.168.68.93/REQUESTWEATHER?";
    for (byte j=0;j<24;j++) tempstring += "hourly_temp=" + (String) j + "&";
    for (byte j=0;j<24;j++) tempstring += "hourly_weatherID=" + (String) j + "&";
    tempstring += "daily_weatherID=0&daily_weatherID=1&daily_weatherID=2&daily_weatherID=3&daily_weatherID=4&daily_tempMax=0&daily_tempMax=1&daily_tempMax=3&daily_tempMax=2&daily_tempMax=4&daily_tempMin=0&daily_tempMin=1&daily_tempMin=2&daily_tempMin=3&daily_tempMin=4&isFlagged=1&sunrise=1&sunset=1";
    http.useHTTP10(true);    
    http.begin(wfclient,tempstring.c_str());
    httpCode = http.GET();
    payload = http.getString();
    http.end();



    for (byte j=0;j<24;j++) {
      hourly_temp[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<24;j++) {
      hourly_weatherID[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<5;j++) {
      daily_weatherID[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<5;j++) {
      daily_tempMAX[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of ";"
    }

    for (byte j=0;j<5;j++) {
      daily_tempMIN[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    //ScreenInfo.isFlagged=payload.substring(0, payload.indexOf(";",0)).toInt(); //do not store isflagged here, just remove it from the list
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    ScreenInfo.sunrise=payload.substring(0, payload.indexOf(";",0)).toInt();
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    ScreenInfo.sunset = payload.substring(0, payload.indexOf(";",0)).toInt();

    ScreenInfo.lastWeatherUpdate = t;

  }

}


void drawScreen_Clock() {
  
  int Y = 0, X=0 ;
  
  //if isdry, draw a box around the entire screen the color of the border...
  
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  //clear clock area and draw clock

  if (ScreenInfo.isSoilDry) {
    tft.fillRect(0,0,TFT_WIDTH,clockHeight,tft.color565(200,200,145));
  }
  else tft.fillRect(0,0,TFT_WIDTH,clockHeight,ScreenInfo.BG_COLOR);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(8);
  X = TFT_WIDTH/2;
  Y=tft.fontHeight(8)/2+1;
  snprintf(tempbuf,69,"%d:%02d",hour(),minute());
  tft.drawString(tempbuf,X,Y);
  Y+=tft.fontHeight(8)/2+5;
  
  Y+=tft.fontHeight(4)/2;
  tft.setTextFont(4);
  snprintf(tempbuf,69,"%s, %s %d",fcnDOW(now()),fcnMONTH(now()),day());
  tft.drawString(tempbuf,X,Y);

  tft.setTextDatum(TL_DATUM);

  ScreenInfo.RedrawClock = 60; //next redraw in 1 minute

}

void drawScreen_Weather() {
  int Y = 0, X=0 ;
  

  Y+=clockHeight+1;

  if (ScreenInfo.isSoilDry) {
    tft.fillRect(0,clockHeight,TFT_WIDTH,TFT_HEIGHT-clockHeight,tft.color565(200,200,145));
  } else   tft.fillRect(0,clockHeight,TFT_WIDTH,TFT_HEIGHT-clockHeight,ScreenInfo.BG_COLOR);


  tft.setTextColor(TFT_BLACK,ScreenInfo.BG_COLOR);
  tft.setTextDatum(MC_DATUM);
    
  uint32_t n=now();
  if ((n<ScreenInfo.sunrise && n<ScreenInfo.sunset) || ( n>ScreenInfo.sunset)) snprintf(tempbuf,66,"/Icons/BMP180x180night/%d.bmp",daily_weatherID[0]);  //it is the night 
  else snprintf(tempbuf,66,"/Icons/BMP180x180day/%d.bmp",daily_weatherID[0]);

  uint8_t bmpsz = 180;
  drawBmp(tempbuf,15,Y,-1); //no transparent, as -1 will prevent transparency (could also just specify BG color)
   
  byte FNTSZ=8;
  tft.setTextFont(8);
  Y+=bmpsz/2;
  X = (bmpsz+15) + (tft.width() - (bmpsz + 15))/2 ;
  if (ScreenInfo.localTemp>-100)   fcnPrintTxtColor(ScreenInfo.localTemp,FNTSZ,X,Y-5); //adjust midpoint by 5 px for visual appeal. Use local temp
  else fcnPrintTxtColor(hourly_temp[0],FNTSZ,X,Y-5); //adjust midpoint by 5 px for visual appeal


  FNTSZ = 6;
  tft.setTextFont(FNTSZ);
  fcnPrintTxtColor(daily_tempMAX[0],FNTSZ,X,Y-bmpsz/2 + tft.fontHeight(FNTSZ)/2+5); 
  fcnPrintTxtColor(daily_tempMIN[0],FNTSZ,X,Y+bmpsz/2 - tft.fontHeight(FNTSZ)/2+5); //these fonts are offset too high, so adjust lower



  //draw server info
  //draw flag info
  

//  fcnPrintTxtColor(daily_tempMAX[0],4,X,Y-tft.fontHeight(4)/2-3);
  //fcnPrintTxtColor(daily_tempMIN[0],4,X,Y+tft.fontHeight(4)/2+3);

  Y+=bmpsz/2+2;
  
 
//draw 60x60 hourly bmps
  X=TFT_WIDTH/4;
    FNTSZ = 2;
  for (byte j=1;j<5;j++) {    
    if (j*ScreenInfo.HourlyInterval>=24) break; //stop if we pass 24 hours
    if (((n+j*ScreenInfo.HourlyInterval*3600)<ScreenInfo.sunrise && (n+j*ScreenInfo.HourlyInterval*3600)<ScreenInfo.sunset) || ( (n+j*ScreenInfo.HourlyInterval*3600)>ScreenInfo.sunset)) snprintf(tempbuf,66,"/Icons/BMP60x60night/%d.bmp",hourly_weatherID[j*ScreenInfo.HourlyInterval]);  //it is the night 
    else     snprintf(tempbuf,55,"/Icons/BMP60x60day/%d.bmp",hourly_weatherID[j*ScreenInfo.HourlyInterval]);
    drawBmp(tempbuf,(j-1)*X+X/2-30,Y,-1);
 
    tft.setTextFont(FNTSZ);
    tft.setTextDatum(MC_DATUM);

    snprintf(tempbuf,55," @%s",dateify((n+j*ScreenInfo.HourlyInterval*3600),"hh"));  
    fcnPrintTxtColor(hourly_temp[j*ScreenInfo.HourlyInterval],4,(j-1)*X+X/2,Y+60+tft.fontHeight(FNTSZ),"",(String) tempbuf);

//    tft.drawString(tempbuf,(j-1)*X+X/2,Y+2+60+tft.fontHeight(4)/2);

  }

//  Y += 60+2+2*(tft.fontHeight(4)+2);
  Y += 60+(tft.fontHeight(FNTSZ)+2);
  
  
  X=TFT_WIDTH/3;
  for (byte j=1;j<4;j++) {
    snprintf(tempbuf,65,"/Icons/BMP60x60day/%d.bmp",daily_weatherID[j]);
    drawBmp(tempbuf,(j-1)*X+X/2-30,Y,-1);

    FNTSZ=4;
    tft.setTextFont(FNTSZ);
    fcnPrintTxtColor2(daily_tempMAX[j],daily_tempMIN[j],FNTSZ,(j-1)*X+X/2,Y+2+60+tft.fontHeight(FNTSZ)/2);
    snprintf(tempbuf,55,"%s",fcnDOW(n+j*86400));
    tft.setTextDatum(MC_DATUM);    
    tft.drawString(tempbuf,(j-1)*X+X/2,Y+2+60+tft.fontHeight(FNTSZ)+2,2);
  }

  Y += 60+5+tft.fontHeight(FNTSZ)+5;
  tft.setTextDatum(TL_DATUM);

  ScreenInfo.RedrawWeather = ScreenInfo.WeatherTime*60;
}


int fcn_write_sensor_data(byte i, int y) {
    //i is the sensor number, y is Ypos
    //writes a line of data, then returns the new y pos
    uint16_t xpos = 0;

      tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
      tft.setTextDatum(TL_DATUM);
      
      tft.setTextFont(SMALLFONT);

      snprintf(tempbuf,60,"%u.%u.%u", Sensors[i].ardID,Sensors[i].snsType,Sensors[i].snsID);
      tft.drawString(tempbuf, 0, y);
      
      dateify(Sensors[i].timeLogged,"hh:nn");
      tft.drawString(DATESTRING, 47, Ypos);

      snprintf(tempbuf,60,"%s", Sensors[i].snsName);
      tft.drawString(tempbuf, 87, Ypos);
 
      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MIN);
      tft.drawString(tempbuf, 168, Ypos);

      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue);
      tft.drawString(tempbuf, 204, Ypos);
      
      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MAX);
      tft.drawString(tempbuf, 240, Ypos);

      xpos=276;
      if (bitRead(Sensors[i].localFlags,1)==1) { //expired
        tft.setTextColor(TFT_YELLOW,ScreenInfo.BG_COLOR);  
        if (bitRead(Sensors[i].Flags,7)==1) xpos+=tft.drawString("X",xpos,Ypos);
        else xpos+=tft.drawString("x",xpos,Ypos);
      }
      if (bitRead(Sensors[i].Flags,0)==1) { //flagged
        tft.setTextColor(TFT_RED,ScreenInfo.BG_COLOR);  
        xpos+=tft.drawString("!",xpos,Ypos);
      }
      tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);  

      if (Sensors[i].localFlags&0b1) { //sent
        xpos+=tft.drawString("SENT",xpos,Ypos);        
      }

      return y + tft.fontHeight(SMALLFONT)+2;

}

void drawScreen_Settings() {
  
  byte isSelected = ScreenInfo.settingsSelectable[ScreenInfo.settingsLine];
  if (isSelected>15) {
    ScreenInfo.settingsLine = 0;
    ScreenInfo.settingsSelected=0;
    isSelected = ScreenInfo.settingsSelectable[ScreenInfo.settingsLine];
  }

  //was a button pushed?
  if (ScreenInfo.settingsSelected!=0) {    
    switch(isSelected) {
      case 3:
      {
        ScreenInfo.lastWeatherUpdate=0;
        getWeather();
        break;
      }
      case 4:
      {
        ScreenInfo.HourlyInterval += ScreenInfo.settingsSelected;        
        if (ScreenInfo.HourlyInterval>6) ScreenInfo.HourlyInterval=6;
        if (ScreenInfo.HourlyInterval==0) ScreenInfo.HourlyInterval=1;
        break;
      }
      case 5:
      {
        ScreenInfo.WeatherTime += ScreenInfo.settingsSelected*5;
        if (ScreenInfo.WeatherTime>60) ScreenInfo.WeatherTime=60;
        if (ScreenInfo.WeatherTime<5) ScreenInfo.WeatherTime=5;
        break;
      }
      case 8:
      {
        ScreenInfo.uploadInterval += ScreenInfo.settingsSelected*5; //increment by 5 minutes
        if (ScreenInfo.uploadInterval<5) ScreenInfo.uploadInterval=5;
        if (ScreenInfo.uploadInterval>120) ScreenInfo.uploadInterval=120;
        break;
      }
      case 9:
      {
            ScreenInfo.lastUploadTime=0; //force an upload
            ScreenInfo.snsListLastTime=0;
            ScreenInfo.snsLastDisplayed=0;
            uploadData(true);
            writeSensorsSD("/Data/SensorBackup.dat");
            storeScreenInfoSD();
            SendData(); //update server about our status
            
        break;
      }
      case 13:
      {
        deleteFiles("SensorBackup.dat","/Data");
        deleteFiles("ScreenFlags.dat","/Data");
        break;
      }

      case 14:
      {
        deleteFiles("*.dat","/Data");
        break;
      }
      
    }

    ScreenInfo.settingsSelected=0;
  }

  clearTFT();
  Ypos = 0;
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);


  Ypos = Ypos + tft.fontHeight(4)/2;
  tft.drawString(dateify(now(),"hh:nn mm/dd"), TFT_WIDTH / 2, Ypos);
  tft.setTextDatum(TL_DATUM);
  Ypos += tft.fontHeight(4)/2 + 6;
  tft.fillRect(0,Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
  Ypos += 4;

  tft.setTextFont(2);
  //can show 15 lines, but only up to 10 can be selectable  
  

  if (isSelected==0) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"isExpired: %s\n", (ScreenInfo.isExpired!=0)?"Y":"N");
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;


  if (isSelected==1) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"isFlagged: %s\n",(ScreenInfo.isFlagged!=0)?"Y":"N");
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==2) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"localTemp: %d at %s\n",ScreenInfo.localTemp, dateify(ScreenInfo.localTempTime, "mm/dd hh:nn"));
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==3) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"last weather: %s\n",dateify(ScreenInfo.lastWeatherUpdate,"mm/dd hh:nn"));
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

    if (isSelected==4) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Hourly weather interval: %u\n",ScreenInfo.HourlyInterval);
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==5) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Weather redraw: %u\n", ScreenInfo.WeatherTime);
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;


  if (isSelected==6) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Free/Lowest Heap: %u, %u\n",(uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000,(uint32_t) ESP.getMinFreeHeap()/1000);
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  
  if (isSelected==7) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Alive since %s\n",dateify(ALIVESINCE));
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==8) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Upload interval (minutes): %u\n",ScreenInfo.uploadInterval);
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==9) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"last Upload: %s %s\n",dateify(ScreenInfo.lastUploadTime,"mm/dd/yy hh:nn"), (ScreenInfo.lastUploadSuccess==true)?"Good":"Failed");
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==10) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
tft.setTextFont(2);
  snprintf(tempbuf,70,"Gsheet Name: %s\n",GsheetName.c_str());
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==11) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Last error (at %s): \n", dateify(ScreenInfo.lastErrorTime,"mm/dd hh:nn:ss"));
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==12) tft.setTextColor(TFT_BLACK,TFT_LIGHTGRAY);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"%s\n",lastError.c_str());
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;


  if (isSelected==13) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Delete Settings Data\n");
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;

  if (isSelected==14) tft.setTextColor(TFT_RED,TFT_SKYBLUE);
  else tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  snprintf(tempbuf,70,"Flush SD Data\n");
tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(2) + 2;


        //buttons on setting screen are: 
        /*
          main
          list
          select
          reset
          ++
          --
        */
  drawButton((String) "Main",(String) "List",(String) "Select",(String) "Reset",(String) "++",(String) "--");
  ScreenInfo.RedrawList = 60;

}


void drawScreen_List() {
  clearTFT();
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
    
  Ypos = Ypos + tft.fontHeight(4)/2;
  tft.drawString(dateify(now(),"hh:nn mm/dd"), TFT_WIDTH / 2, Ypos);
  tft.setTextDatum(TL_DATUM);

  Ypos += tft.fontHeight(4)/2 + 1;

  tft.setTextFont(SMALLFONT);
  snprintf(tempbuf,60,"All:%u Dry:%u Hot:%u Cold:%u Exp:%u\n",countDev(),ScreenInfo.isSoilDry,ScreenInfo.isHot,ScreenInfo.isCold,ScreenInfo.isExpired);
  tft.drawString(tempbuf,0,Ypos);
  Ypos += tft.fontHeight(1) + 1;


  tft.fillRect(0,Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
//            ardid time    name    min  cur   max    * for sent

  tft.fillRect(45,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(85,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(166,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(200,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(236,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(272,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  Ypos += 4;

  tft.setTextFont(SMALLFONT);
  tft.setTextColor(TFT_DARKGRAY,ScreenInfo.BG_COLOR);

    snprintf(tempbuf,60,"ID");
    tft.drawString(tempbuf, 0, Ypos);

    snprintf(tempbuf,60,"Time");
    tft.drawString(tempbuf, 47, Ypos);

    snprintf(tempbuf,60,"Name");
    tft.drawString(tempbuf, 87, Ypos);

    snprintf(tempbuf,60,"MIN");
    tft.drawString(tempbuf, 168, Ypos);

    snprintf(tempbuf,60,"Val");
    tft.drawString(tempbuf, 204, Ypos);

    snprintf(tempbuf,60,"MAX");
    tft.drawString(tempbuf, 240, Ypos);

    snprintf(tempbuf,60,"Status");
    tft.drawString(tempbuf, 276, Ypos);

  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);

  Ypos += tft.fontHeight(SMALLFONT) + 2;

  tft.setTextFont(SMALLFONT); //
  

  //initialize ScreenInfo.snsListArray
  if (t-ScreenInfo.snsListLastTime > 30) {
    ScreenInfo.snsListLastTime = t;
    ScreenInfo.snsLastDisplayed=0;

    for (byte i=0;i<SENSORNUM;i++) {
      //march through sensors and list specified number of  ArdIDs
      //decide if this should be shown or not.
      if (isSensorInit(i)==true) {
        ScreenInfo.snsListArray[0][i] = bitRead(Sensors[i].Flags,0) +1; //1 if not flagged, 2 if [flagged ]
        if (bitRead(Sensors[i].localFlags,1) && bitRead(Sensors[i].Flags,7)) ScreenInfo.snsListArray[0][i]++; //also count critical expred sensors as flagged
      } 
      else ScreenInfo.snsListArray[0][i] = 0;
      ScreenInfo.snsListArray[1][i] = 255; //not in order yet
    }

    //now put them in order...
    for (byte i=0;i<SENSORNUM;i++) {

      if (ScreenInfo.snsListArray[0][i] > 0 && ScreenInfo.snsListArray[1][i] == 255) {
        ScreenInfo.snsListArray[1][i] = ScreenInfo.snsLastDisplayed++;

        //now put all related sensors next in order...
        for (byte j=i+1;j<SENSORNUM;j++) {
          if (ScreenInfo.snsListArray[0][j] > 0 && Sensors[j].snsType == Sensors[i].snsType && ScreenInfo.snsListArray[1][j] == 255) ScreenInfo.snsListArray[1][j] = ScreenInfo.snsLastDisplayed++;
        }
      }
    }
    ScreenInfo.snsLastDisplayed = 0;

  }


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
      Ypos = fcn_write_sensor_data(j,Ypos);
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
  ScreenInfo.RedrawList = 60;
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
  char tempbuf[20] = {0};
  for (byte i=0;i<2;i++) {
    X = 0;
    for (byte j=0;j<3;j++) {
      tft.fillRoundRect(X+bspacer,Y+bspacer,bX,bY,4,tft.color565(200,200,200));
      tft.drawRoundRect(X+bspacer,Y+bspacer,bX,bY,4,tft.color565(125,125,125));
      if (j==0 && i==0) snprintf(tempbuf,60,b1.c_str());
      if (j==1 && i==0) snprintf(tempbuf,60,b2.c_str());
      if (j==2 && i==0) snprintf(tempbuf,60,b3.c_str());
      if (j==0 && i==1) snprintf(tempbuf,60,b4.c_str());
      if (j==1 && i==1) snprintf(tempbuf,60,b5.c_str());
      if (j==2 && i==1) snprintf(tempbuf,60,b6.c_str());
      
      tft.drawString(tempbuf, (int) ((double) X+bX/2),(int) ((double) Y + bY/2),SMALLFONT);
      X+=bX+bspacer+bspacer;     

    }
    Y+=bY+bspacer+bspacer;
  }

  tft.setTextDatum(TL_DATUM);

  
  tft.setTextColor(ScreenInfo.FG_COLOR,ScreenInfo.BG_COLOR);
}



void handleCLEARSENSOR() {
  byte j=0;
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i)=="SensorNum") j=server.arg(i).toInt();
  }

  initSensor(j);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}



void handleTIMEUPDATE() {


  updateTime(10,20);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

  return;
}


void handleRoot() {

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Sensor Server</title></head><body>";
  
  currentLine = currentLine + "<h2>" + (String) dateify(0,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br><p>";
  currentLine += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  currentLine += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  currentLine += "<button type=\"submit\">Update Time</button><br>";
  currentLine += "</FORM><br>";

  
  currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
  currentLine = currentLine + "isFlagged: " + (String) ScreenInfo.isFlagged + "<br>";
  currentLine = currentLine + "Sensor Data: <br>";

  currentLine = currentLine + "----------------------------------<br>";      

  byte used[SENSORNUM];
  for (byte j=0;j<SENSORNUM-1;j++)  {
    used[j] = 255;
  }

  byte usedINDEX = 0;  
  char padder[2] = ".";
  char tempchar[9] = "";
  char temp[26];
        
  for (byte j=0;j<SENSORNUM;j++)  {
//    currentLine += "inIndex: " + (String) j + " " + (String) inIndex(j,used,SENSORNUM) + "<br>";
    if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,used,SENSORNUM) == false)  {
      used[usedINDEX++] = j;
      currentLine += "<FORM action=\"/REQUESTUPDATE\" method=\"get\">";
      currentLine += "<p style=\"font-family:monospace\">";

      currentLine += "<input type=\"hidden\" name=\"SensorNum\" value=\"" + (String) j + "\"><br>";  

      currentLine = currentLine + "Arduino: " + Sensors[j].ardID+"<br>";
      sprintf(temp,"%s","IP Address");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + "</label>";
      currentLine += " <a href=\"http://" + IP2String(Sensors[j].IP) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + IP2String(Sensors[j].IP) + "</a><br>";
      
      currentLine = currentLine +  String("..............................") + String("<br>");

      sprintf(temp,"%s","Sensor Name");
      strPad(temp,padder,25);
      currentLine += (String) "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsName + "</label><br>";

      sprintf(temp,"%s","Value");
      strPad(temp,padder,25);
      currentLine += (String)"<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsValue + "</label><br>";

      sprintf(temp,"%s","Flagged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[j].Flags,0) + "</label><br>";
      
      sprintf(temp,"%s","Sns Type");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";

      sprintf(temp,"%s","Last Recvd");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeRead + " " + (String) dateify(Sensors[j].timeRead,"mm/dd hh:nn:ss") +  "</label><br>";
      
      sprintf(temp,"%s","Last Logged");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[j].timeLogged + " " + (String) dateify(Sensors[j].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";

      Byte2Bin(Sensors[j].Flags,tempchar,true);
      sprintf(temp,"%s","Flags");
      strPad(temp,padder,25);
      currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";
      currentLine += "<br>";
      
      for (byte jj=j+1;jj<SENSORNUM;jj++) {
        if (Sensors[jj].snsID>0 && Sensors[jj].snsType>0 && inIndex(jj,used,SENSORNUM) == false && Sensors[jj].ardID==Sensors[j].ardID) {
          used[usedINDEX++] = jj;
 
    
          sprintf(temp,"%s","Sensor Name");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsName + "</label><br>";
    
          sprintf(temp,"%s","Value");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsValue + "</label><br>";
    
          sprintf(temp,"%s","Flagged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) bitRead(Sensors[jj].Flags,0) + "</label><br>";
          
          sprintf(temp,"%s","Sns Type");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].snsType+"."+ (String) Sensors[j].snsID + "</label><br>";
    
          sprintf(temp,"%s","Last Recvd");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeRead + " " + (String) dateify(Sensors[jj].timeRead,"mm/dd hh:nn:ss") + "</label><br>";
          
          sprintf(temp,"%s","Last Logged");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) Sensors[jj].timeLogged + " " + (String) dateify(Sensors[jj].timeLogged,"mm/dd hh:nn:ss") + "</label><br>";
    
          Byte2Bin(Sensors[jj].Flags,tempchar,true);
          sprintf(temp,"%s","Flags");
          strPad(temp,padder,25);
          currentLine += "<label for=\"L1\">" + (String) temp + " " + (String) tempchar  + "</label><br>";      
          currentLine += "<br>";
              
        }

      }

      currentLine += "<button type=\"submit\" formaction=\"/CLEARSENSOR\">Clear ArdID Sensors</button><br>";
    
      currentLine += "<button type=\"submit\">Request Update</button><br>";

      currentLine += "</p></form><br>----------------------------------<br>";   
    }

  }
  
      currentLine += "</p></body></html>";   

   #ifdef _DEBUG
      Serial.println(currentLine);
    #endif
    
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void handlePost() {

        #ifdef _DEBUG
          Serial.println("handlePOST hit.");
        #endif

SensorVal S;
uint8_t tempIP[4] = {0,0,0,0};

  for (byte k=0;k<server.args();k++) {   

      #ifdef _DEBUG
      Serial.print("Received this argname/arg: ");
      Serial.printf("%s --> %s\n",server.argName(k),server.arg(k).c_str());
      #endif

      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
      if ((String)server.argName(k) == (String)"IP") {
        IPString2ByteArray(String(server.arg(k)),S.IP);        
      }

      if ((String)server.argName(k) == (String)"varName") snprintf(S.snsName,30,"%s", server.arg(k).c_str());
      if ((String)server.argName(k) == (String)"varValue") S.snsValue = server.arg(k).toDouble();
      if ((String)server.argName(k) == (String)"timeLogged") S.timeRead = server.arg(k).toDouble();      //time logged at sensor is time read by me
      if ((String)server.argName(k) == (String)"Flags") S.Flags = server.arg(k).toInt();
      if ((String)server.argName(k) == (String)"SendingInt") S.SendingInt = server.arg(k).toInt();
  }
  

          #ifdef _DEBUG
          Serial.println("handlePOST parsed input.");
        #endif

  S.timeLogged = t; //time logged by me is when I received this.
  if (S.timeRead == 0)     S.timeRead = t;
  
  if (S.Flags&0b1)        ScreenInfo.isFlagged=1; //any sensor is flagged!
    

  bitWrite(S.localFlags,0,0); //is sent?
  bitWrite(S.localFlags,1,0); //is expired?

  storeSensorSD(&S);

  int16_t sn = findDev(&S, true); //true makes sure finddev always returns a valid entry


  if (sn<0) {


    server.send(401, "text/plain", "Critical failure... I could not find the sensor or enter a new value. This should not be possible, so this error is undefined."); // Send HTTP status massive error
    lastError = "Out of sensor space";
    ScreenInfo.lastErrorTime = t;

    return;
  }

    String stemp = S.snsName;
    if ((stemp.indexOf("Outside")>-1 && S.snsType==4 ) ) { //outside temp
      ScreenInfo.localTemp = S.snsValue;
      ScreenInfo.localTempIndex = sn;
      ScreenInfo.localTempTime = t;
    }


  S.snsValue_MIN= Sensors[sn].snsValue_MIN;
  S.snsValue_MAX= Sensors[sn].snsValue_MAX;

  if ((double) S.snsValue_MAX<S.snsValue) S.snsValue_MAX=S.snsValue;
  if ((double) S.snsValue_MIN>S.snsValue) S.snsValue_MIN=S.snsValue;

    
        #ifdef _DEBUG
          Serial.println("handlePOST about to enter finddev.");
        #endif

  if (sn<0 || sn >= SENSORNUM) return;
  Sensors[sn] = S;

        #ifdef _DEBUG
          Serial.println("handlePOST assigned val.");
        #endif

  server.send(200, "text/plain", "Received Data"); // Send HTTP status 200 (OK) when there's no handler for the URI in the request


}


bool SendData() {
  
  //special case for senddata... just send info relative to google server (ie, was last upload successful)

#ifdef  ARDID
   byte arduinoID = ARDID;
#else
byte arduinoID = WiFi.localIP()[3];
#endif

//set flags value
byte Flags=0;

if (ScreenInfo.lastUploadSuccess==false)  bitWrite(Flags,0,1);
bitWrite(Flags,1,1); //monitored
bitWrite(Flags,7,1); //critical


WiFiClient wfclient;
HTTPClient http;

if(WiFi.status() == WL_CONNECTED){
  String payload;
  String URL;
  String tempstring;
  int httpCode=404;
  tempstring = "/POST?IP=" + WiFi.localIP().toString() + "," + "&varName=GSheets";
  tempstring = tempstring + "&varValue=";
  tempstring = tempstring + String(ScreenInfo.uploadFailCount, DEC);
  tempstring = tempstring + "&Flags=";
  tempstring = tempstring + String(Flags, DEC);
  tempstring = tempstring + "&logID=";
  tempstring = tempstring + String(arduinoID, DEC);
  tempstring = tempstring + ".99.1" + "&timeLogged=" + String(ScreenInfo.lastUploadTime, DEC) + "&isFlagged=" + String(bitRead(Flags,0), DEC) + "&SendingInt=" + String(3600, DEC);

  URL = "http://192.168.68.93";
  URL = URL + tempstring;

  http.useHTTP10(true);
//note that I'm coverting lastreadtime to GMT

  #ifdef _DEBUG
      Serial.print("sending this message: ");
      Serial.println(URL.c_str());
  #endif

  http.begin(wfclient,URL.c_str());
  httpCode = (int) http.GET();
  payload = http.getString();
  http.end();


  if (httpCode == 200) {
    ScreenInfo.LastServerUpdate = t;
    return true;
  }
}

return false;

}


//--------------------------------------
//--------------------------------------
//--------------------------------------
//--------------------------------------
//SPREADSHEET FUNCTIONS

bool file_deleteSpreadsheetByID(String fileID) {
  FirebaseJson response;
  
   bool success= GSheet.deleteFile(&response /* returned response */, fileID /* spreadsheet Id to delete */);
   //String tmp;
   //response.toString(tmp, true);
//  tft.printf("Delete result: %s",tmp.c_str());
  return success;
}

void file_deleteSpreadsheetByName(String filename){
  FirebaseJson response;
  
  String fileID;


  do {
    fileID = file_findSpreadsheetIDByName(filename,false);
    if (fileID!="" && fileID.substring(0,5)!="ERROR") {
      tft.setTextFont(SMALLFONT);
      sprintf(tempbuf,"Deletion of %s status: %s\n",fileID.c_str(),file_deleteSpreadsheetByID(fileID) ? "OK" : "FAIL");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    }
  } while (fileID!="" && fileID.substring(0,5)!="ERROR");
  return;
}


String file_findSpreadsheetIDByName(String sheetname, bool createfile) {
  //returns the file ID. Note that sheetname is NOT unique, so multiple spreadsheets could have the same name. Only ID is unique.
  //returns "" if no such filename
          
  FirebaseJson filelist;

  String resultstring = "ERROR:";  
  String thisFileID = "";
  
  String tmp;
  bool success = GSheet.listFiles(&filelist /* returned list of all files */);

    #ifdef NOISY
      tft.setTextFont(SMALLFONT);
      snprintf(tempbuf,60,"%s search result: %s\n",sheetname.c_str(),success?"OK":"FAIL");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    #endif
  
  if (success) {
    #ifdef _DEBUG
      Serial.println("Got the obj list!");
    #endif

    if (sheetname == "***") {
      //special case, return the entire file index!
      filelist.toString(resultstring,true);
      tft.printf("%s\n",resultstring.c_str());
      return "";
    }

    //put file array into data object
    FirebaseJsonData result;
    filelist.get(result,"files");

    if (result.success) {
      #ifdef _DEBUG
        Serial.println("File list in firebase!");
      #endif

      FirebaseJsonArray thisfile;
      result.get<FirebaseJsonArray /* type e.g. FirebaseJson or FirebaseJsonArray */>(thisfile /* object that used to store value */);
            
      int fileIndex=0;
      bool foundit=false;
  
      do {
        thisfile.get(result,fileIndex++); //iterate through each file
        if (result.success) {
          FirebaseJson fileinfo;

          //Get FirebaseJson data
          result.get<FirebaseJson>(fileinfo);

//          fileinfo.toString(tmp,true);

          size_t count = fileinfo.iteratorBegin();
          
          for (size_t i = 0; i < count; i++) {
            
            FirebaseJson::IteratorValue value = fileinfo.valueAt(i);
            String s1(value.key);
            String s2(value.value);
            s2=s2.substring(1,s2.length()-1);

            if (s1 == "id") thisFileID = s2;
            if (s1 == "name" && (s2 == sheetname || sheetname == "*")) foundit=true; //* is a special case where any file returned
          }
          fileinfo.iteratorEnd();
          if (foundit) {
                #ifdef _DEBUG
                  Serial.println("Found file of interest!");
                  Serial.printf("FileID is %s\n", thisFileID.c_str());

                #endif

              #ifdef NOISY
                tft.setTextFont(SMALLFONT);
                snprintf(tempbuf,60,"FileID is %s\n",thisFileID.c_str());
                tft.drawString(tempbuf, 0, Ypos);
                Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
              #endif
           return thisFileID;
          }
        }
      } while (result.success);

    }

    resultstring= "ERROR: [" + sheetname + "] not  found,\nwith createfile=" + (String) createfile;
                #ifdef _DEBUG
                  Serial.println("Did not find the file.");
                #endif

    lastError = resultstring;
  
    if (createfile) {
                  #ifdef _DEBUG
                  Serial.printf("Trying to create file %s\n", GsheetName.c_str());

                #endif

      resultstring = file_createSpreadsheet(sheetname,false);
                  #ifdef _DEBUG
                  Serial.printf("result: %s\n", resultstring.c_str());

                #endif

      resultstring = file_createHeaders(resultstring,"SnsID,IP Add,SnsName,Time Logged,Time Read,HumanTime,Flags,Reading");
      #ifdef NOISY
        tft.setTextFont(SMALLFONT);
        snprintf(tempbuf,60,"new fileid: %s\n",resultstring.c_str());
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      #endif
    }         
       
    
  } else { //gsheet error
      #ifdef NOISY
        tft.setTextFont(SMALLFONT);
        snprintf(tempbuf,60,"Gsheet error:%s\n",GSheet.errorReason().c_str());
        tft.drawString(tempbuf, 0, Ypos);
        Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
      #endif
    resultstring = "ERROR: gsheet error";
    lastError = resultstring;
    ScreenInfo.lastErrorTime = t;
  }

  return resultstring; 

}


String file_createSpreadsheet(String sheetname, bool checkFile=true) {


  String fileID = "";
  if (checkFile) {
    file_findSpreadsheetIDByName(sheetname,false);
  
    if (fileID.substring(0,5)!="ERROR") {
      lastError = "Tried to create a file that exists!";   
      ScreenInfo.lastErrorTime = t;
      return fileID;
    }
  } 


  FirebaseJson spreadsheet;
  spreadsheet.set("properties/title", sheetname);
  spreadsheet.set("sheets/properties/title", "Sheet1");
  spreadsheet.set("sheets/properties/sheetId", 1); //readonly
  spreadsheet.set("sheets/properties/sheetType", "GRID");
  spreadsheet.set("sheets/properties/sheetType", "GRID");
  spreadsheet.set("sheets/properties/gridProperties/rowCount", 200000);
  spreadsheet.set("sheets/properties/gridProperties/columnCount", 10);

  spreadsheet.set("sheets/developerMetadata/[0]/metadataValue", "Jaypath");
  spreadsheet.set("sheets/developerMetadata/[0]/metadataKey", "Creator");
  spreadsheet.set("sheets/developerMetadata/[0]/visibility", "DOCUMENT");

  FirebaseJson response;

  bool success = GSheet.create(&response /* returned response */, &spreadsheet /* spreadsheet object */, USER_EMAIL /* your email that this spreadsheet shared to */);

  if (success==false) {
    #ifdef NOISY
      tft.setTextFont(SMALLFONT);
      snprintf(tempbuf,60,"CREATE: gsheet failed to create object\n");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    #endif

    lastError = "ERROR: Gsheet failed to create";
    ScreenInfo.lastErrorTime = t;
    
  
    return "ERROR: Gsheet failed to create";
  }

  return fileID;
}

bool file_createHeaders(String fileID,String Headers) {

  /* only accept file ID
  String fileID = file_findSpreadsheetIDByName(sheetname,true);
  fileID = file_findSpreadsheetIDByName(sheetname);
  */

  if (fileID.substring(0,5)=="ERROR") {
    lastError = fileID;
    ScreenInfo.lastErrorTime = t;
     return false;
  }

  uint8_t cnt = 0;
  int strOffset=-1;
  String temp;
  FirebaseJson valueRange;
  FirebaseJson response;
  valueRange.add("majorDimension", "ROWS");
  while (Headers.length()>0 && Headers!=",") {
    strOffset = Headers.indexOf(",", 0);
    if (strOffset == -1) { //did not find the "," so the remains are the last header.
      temp=Headers;
    } else {
      temp = Headers.substring(0, strOffset);
      Headers.remove(0, strOffset + 1);
    }

    if (temp.length()>0) valueRange.set("values/[0]/[" + (String) (cnt++) + "]", temp);
  }


  bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);

  return success;
  
} 
  

bool file_uploadSensorData(void) {

  FirebaseJson valueRange;
  FirebaseJson response;

  //time_t t = now(); now a global!


  if (GsheetID=="" || ScreenInfo.lastUploadSuccess==0) {
    //need to reset file ID
    GsheetName = "ArduinoLog" + (String) dateify(t,"yyyy-mm");
    GsheetID = file_findSpreadsheetIDByName(GsheetName,true);
    if (GsheetID=="" || GsheetID.substring(0,5)=="ERROR") {
      lastError = GsheetID;
      ScreenInfo.lastErrorTime = t;
      return false;
    }

  }
  

  String logid="";
  byte rowInd = 0;
  byte snsArray[SENSORNUM] = {255};
  for (byte j=0;j<SENSORNUM;j++) snsArray[j]=255;

  for (byte j=0;j<SENSORNUM;j++) {
    if (isSensorInit(j) && Sensors[j].timeLogged>0 && bitRead(Sensors[j].localFlags,0)==0) {
      logid = (String) Sensors[j].ardID + "." + (String)  Sensors[j].snsType + "." + (String)  Sensors[j].snsID;

      valueRange.add("majorDimension", "ROWS");
      valueRange.set("values/[" + (String) rowInd + "]/[0]", logid);
      valueRange.set("values/[" + (String) rowInd + "]/[1]", IP2String(Sensors[j].IP));
      valueRange.set("values/[" + (String) rowInd + "]/[2]", (String) Sensors[j].snsName);
      valueRange.set("values/[" + (String) rowInd + "]/[3]", (String) Sensors[j].timeLogged);
      valueRange.set("values/[" + (String) rowInd + "]/[4]", (String) Sensors[j].timeRead);
      valueRange.set("values/[" + (String) rowInd + "]/[5]", (String) dateify(Sensors[j].timeLogged,"mm/dd/yy hh:nn:ss")); //use timelogged, since some sensors have no clock
      valueRange.set("values/[" + (String) rowInd + "]/[6]", (String) bitRead(Sensors[j].Flags,0));
      valueRange.set("values/[" + (String) rowInd + "]/[7]", (String) Sensors[j].snsValue);
  
      snsArray[rowInd++] = j;
    }
  }

        // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append

  bool success = GSheet.values.append(&response /* returned response */, GsheetID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
  if (success) {
//            response.toString(Serial, true);
    for (byte j=0;j<SENSORNUM;j++) {
      if (snsArray[j]!=255)     bitWrite(Sensors[snsArray[j]].localFlags,0,1);
    }
    ScreenInfo.lastUploadTime = now(); 
  } else {
    lastError = "ERROR Failed to update: " + GsheetID;
    ScreenInfo.lastErrorTime = t;
    return false;
  }

return success;

        
}

void tokenStatusCallback(TokenInfo info)
{
    if (info.status == token_status_error)
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
    }
    else
    {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
    }
}


