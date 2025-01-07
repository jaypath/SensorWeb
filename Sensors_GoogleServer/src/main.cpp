//#define _DEBUG 99
#define ARDNAME "GoogleServer"
#define _USETFT
#define _USETOUCH
//#define NOISY true
#define _USEOTA
#define ALTSCREENTIME 30


#include <Arduino.h>
#include <String.h>

#include <WiFi.h>
#include <timesetup.hpp>


#include <String.h>
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



extern WebServer server;


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


int hourly_temp[12];
int hourly_weatherID[12];
int daily_weatherID[5];
int daily_tempMAX[5];
int daily_tempMIN[5];
uint32_t sunrise,sunset;

uint32_t ALTSCREEN = 0;
bool SHOWMAIN = true;

extern SensorVal Sensors[SENSORNUM]; //up to SENSORNUM sensors will be monitored - this is for isflagged sensors!
struct ScreenFlags I;


uint32_t lastUploadTime = 0;
bool lastUploadSuccess = true;
uint8_t UploadFailCount = 0;

byte uploadQ = 20; //upload every __ minutes
byte uploadQFailed = 2; //upload every __ minutes if the last one failed

extern long DSTOFFSET;
extern byte OldTime[5];
extern int Ypos ;
extern bool isFlagged ;
extern char tempbuf[70];     
extern time_t ALIVESINCE;
double bg_luminance; 

int fcn_write_sensor_data(byte i, int y);
void clearTFT();
void drawScreen_list();
void drawScreen_Clock();
void drawScreen_Weather();
void getWeather();

String file_findSpreadsheetIDByName(String sheetname, bool createfile);
bool file_deleteSpreadsheetByID(String fileID);
String file_createSpreadsheet(String sheetname);
bool file_createHeaders(String sheetname,String Headers);
void tokenStatusCallback(TokenInfo info);
void file_deleteSpreadsheetByName(String filename);

void handleLIST();
void handleNotFound();
void handlePost();
void handleRoot();
void handleCLEARSENSOR();
void handleTIMEUPDATE();

bool file_uploadSensorData(void);
void fcnChooseTxtColor(byte snsIndex);

#ifdef _USE24BITCOLOR
uint32_t temp2color24(int temp);
#endif
uint16_t temp2color16(int temp, uint16_t *BG);

void drawBmp(const char *filename, int16_t x, int16_t y, int32_t transparent = 0xFFFF);
int ID2Icon(int weatherID);
void fcnPrintTxtColor(int value,byte FNTSZ,int x=-1,int y=-1, String headstr="", String tailstr="");
void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x=-1,int y=-1, String headstr="", String tailstr="");


void fcnPrintTxtColor2(int value1,int value2,byte FNTSZ, int x,int y, String headstr, String tailstr) {
  //print two temperatures with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position

uint16_t bg = BG_COLOR;


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

tft.setTextColor(FG_COLOR,BG_COLOR);
if (headstr!="") tft.print(headstr.c_str());
tft.setTextColor(temp2color16(value1,&bg),bg);
tft.print(value1);
tft.setTextColor(FG_COLOR,BG_COLOR);
tft.print("/");
tft.setTextColor(temp2color16(value2,&bg),bg);
tft.print(value2);
tft.setTextColor(FG_COLOR,BG_COLOR);
if (tailstr!="") tft.print(tailstr.c_str());

return;
}


void fcnPrintTxtColor(int value,byte FNTSZ,int x,int y, String headstr, String tailstr) {
  //print  temperature with appropriate color, centered at x and y (which are optional)
//if x and y are not provided, just print at the current x/y position

uint16_t bg=BG_COLOR;
tft.setTextDatum(TL_DATUM);



tft.setCursor(x-tft.textWidth( headstr + (String) value + tailstr)/2,y-tft.fontHeight(FNTSZ)/2); //set the correct x and y to center text at this location

tft.setTextColor(FG_COLOR,BG_COLOR);
if (headstr!="") tft.print(headstr.c_str());
tft.setTextColor(temp2color16(value,&bg),bg);
tft.print(value);
tft.setTextColor(FG_COLOR,BG_COLOR);
if (tailstr!="") tft.print(tailstr.c_str());

//tft.drawString(headstr + (String) value + tailstr,x,y);


return;
}


#ifdef _USE24BITCOLOR
//returns 24 bit color
uint32_t temp2color24(int temp) { 
  uint8_t temp_colors[104] = {
  20, 255, 0, 255, //20
  24, 200, 0, 200, //21 - 24
  27, 200, 0, 100, //25 - 27
  29, 200, 100, 100, //28 - 29
  32, 200, 200, 200, //30 - 32
  34, 150, 150, 200, //33 - 34
  37, 0, 0, 150, //35 - 37
  39, 0, 50, 200, //38 - 39
  42, 0, 100, 200, //40 - 42
  44, 0, 150, 150, //43 - 44
  47, 0, 200, 150, //45 - 47
  51, 0, 200, 100, //48 - 51
  54, 0, 150, 100, //52 - 54
  59, 0, 100, 100, //55 - 59
  64, 0, 100, 50, //60 - 64
  69, 0, 100, 0, //65 - 69
  72, 0, 150, 0, //70 - 72
  75, 0, 200, 0, //73 - 75
  79, 50, 200, 0, //76 - 79
  82, 100, 200, 0, //80 - 82
  84, 150, 200, 0, //83 - 84
  87, 200, 100, 50, //85 - 87
  89, 200, 100, 100, //88 - 89
  92, 200, 50, 50, //90 - 92
  94, 250, 50, 50, //93 - 94
  100, 250, 0, 0 //95 - 100
};
  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }

  return tft.color888(temp_colors[j+1],temp_colors[j+2],temp_colors[j+3]);
}
#endif


//returns 16 bit color
uint16_t temp2color16(int temp, uint16_t *BG) {
  uint8_t temp_colors[104] = {
  20, 255, 0, 255, //20
  24, 200, 0, 200, //21 - 24
  27, 200, 0, 100, //25 - 27
  29, 200, 100, 100, //28 - 29
  32, 255, 255, 255, //30 - 32
  34, 150, 150, 255, //33 - 34
  37, 0, 0, 150, //35 - 37
  39, 0, 50, 200, //38 - 39
  42, 0, 100, 200, //40 - 42
  44, 0, 150, 150, //43 - 44
  47, 0, 200, 150, //45 - 47
  51, 0, 200, 100, //48 - 51
  54, 0, 150, 100, //52 - 54
  59, 0, 100, 100, //55 - 59
  64, 0, 100, 50, //60 - 64
  69, 0, 100, 0, //65 - 69
  72, 0, 150, 0, //70 - 72
  75, 0, 200, 0, //73 - 75
  79, 50, 200, 0, //76 - 79
  82, 100, 200, 0, //80 - 82
  84, 150, 200, 0, //83 - 84
  87, 200, 100, 50, //85 - 87
  89, 200, 100, 100, //88 - 89
  92, 200, 50, 50, //90 - 92
  94, 250, 50, 50, //93 - 94
  100, 250, 0, 0 //95 - 100
};
  byte j = 0;
  while (temp>temp_colors[j]) {
    j=j+4;
  }

*BG = BG_COLOR;
double  lumin; //luminance, from 0 to 255

//0.2126*R + 0.7152*G + 0.0722*B trying this luminance option
 lumin =  ((double) 0.2126 * temp_colors[j+1] + 0.7152 * temp_colors[j+2] + 0.0722 * temp_colors[j+3]); //this is the perceived luminance of the color

  if ( abs( lumin-bg_luminance)<25) { //if luminance is similar to bg luminance then...
    byte l = lumin + 128; //note that this wraps around at 255
    *BG = tft.color565(l,l,l); 
  } 

  return tft.color565(temp_colors[j+1],temp_colors[j+2],temp_colors[j+3]);
}




void clearTFT() {
  tft.fillScreen(BG_COLOR);                 // Fill with black
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

  //set bg_luminance value 1 time
  
//0.2126*R + 0.7152*G + 0.0722*B trying this luminance option
  uint32_t bg24 = tft.color16to24(BG_COLOR);
  bg_luminance = 0.2126*((bg24>>16)&0xff) + 0.7152*((bg24>>8)&0xff) + 0.0722*(bg24&0xff);



  tft.printf("Running setup\n");

  if(!SD.begin(41)){  //CS=41
      tft.setTextColor(TFT_RED);
      tft.println("SD Mount Failed");
      tft.setTextColor(FG_COLOR);
      while(false);

  } 
  else {
    tft.println("SD Mount Succeeded");
  }

  bool sdread = readSensorsSD("/Data/SensorBackup.dat");
  if (!sdread) {
      
      tft.setTextColor(TFT_RED);
      tft.println("FAILED SD READ\n");
      tft.setTextColor(FG_COLOR);
      
     }

  // Get flash size and display
  tft.printf("FLASH size: %d bytes",ESP.getMinFreeHeap());

  // Initialize PSRAM (if available) and show the total size
  if (psramFound()) {
    tft.printf("PSRAM size: %d bytes",heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    
  } else {
    tft.printf("No PSRAM found.");
  }
delay(2000);


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
    while (WiFi.status() != WL_CONNECTED)
    {
#ifdef _DEBUG
        Serial.print(".");
#endif
        delay(300);
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        if (millis() - ms > 10000)
            break;
#endif
    }
#ifdef _DEBUG
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
#endif

  tft.setCursor(0,10);
    tft.print("Connected with IP: ");
    tft.println(WiFi.localIP());


    // The WiFi credentials are required for Pico W
    // due to it does not have reconnect feature.
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    GSheet.clearAP();
    GSheet.addAP(WIFI_SSID, WIFI_PASSWORD);
    // You can add many WiFi credentials here
#endif

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


#ifdef _USEOTA

   ArduinoOTA.setHostname(ARDNAME);
  ArduinoOTA.onStart([]() {
    #ifdef _DEBUG
    Serial.println("OTA started");
    #endif
    #ifdef _USETFT
      clearTFT();
      tft.setTextColor(FG_COLOR,BG_COLOR);
      tft.setCursor(0,0);
      tft.setTextDatum(TL_DATUM);
      tft.setTextFont(4);
      tft.println("Receiving OTA:");
      tft.setTextDatum(TL_DATUM);
     
    tft.drawRect(5,tft.height()/2-5,tft.width()-10,10,TFT_BLACK);
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
      if (progress % 5 == 0) tft.fillRect(5,tft.height()/2-5,(int) (double (tft.width()-10)*progress / total),10,TFT_BLACK);
       

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
    #ifdef _USETFT
      clearTFT();
      tft.setCursor(0,0);
      tft.println("OTA OK.");      
    #endif
#endif




    #ifdef _USETFT
      tft.println("start Server.");
    #endif

    server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"
    server.on("/LIST", handleLIST);   
    server.on("/POST", handlePost);   
    server.on("/CLEARSENSOR",handleCLEARSENSOR);
    server.on("/TIMEUPDATE",handleTIMEUPDATE);
    server.onNotFound(handleNotFound);
    server.begin();
    //init globals
    for (byte i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }
    tft.println("Set up TimeClient...");

    timeClient.begin(); //time is in UTC
    updateTime(10,250); //check if DST and set time to EST or EDT
    

    OldTime[0] = 61; //force sec update.
    OldTime[2] = 61;
    OldTime[2] = 61;
    OldTime[3] = weekday(); //do not force daily update

    tft.print("TimeClient OK.  ");

    //init globals
    for ( byte i=0;i<SENSORNUM;i++) {
      initSensor(i);
    }

  #ifdef _USETFT
    clearTFT();
    tft.setCursor(0,0);
    tft.println(WiFi.localIP().toString());
    tft.print(hour());
    tft.print(":");
    tft.println(minute());
  #endif

    getWeather();
    ALIVESINCE = now();
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
            if (*tptr==transparent) *tptr = BG_COLOR; //convert white pixels to background
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




void loop()
{
    //Call ready() repeatedly in loop for authentication checking and processing
  updateTime(1,0); //just try once

  bool ready = GSheet.ready(); //maintains authentication
  time_t t = now();
  

#ifdef _USEOTA
  ArduinoOTA.handle();
#endif
#ifdef _USETOUCH
  if (tft.getTouch(&touch_x, &touch_y)) {
      #ifdef _DEBUG
        Serial.printf("Touch: X= %d, Y= %d\n",touch_x, touch_y);
      #endif

    if (SHOWMAIN==false && (touch_x > TFT_WIDTH-40 && touch_y > TFT_HEIGHT-40)) {
      //upload to gsheet
      #ifdef _DEBUG
        Serial.printf("Upload to gsheets requested\n");
      #endif
      lastUploadSuccess=file_uploadSensorData();
      if (lastUploadSuccess==false) UploadFailCount++;

      if (UploadFailCount>10) ESP.restart();

      #ifdef _DEBUG
        Serial.printf("Uploaded to google sheets %s.\n", lastUploadSuccess ? "succeeded" : "failed");
      #endif
      
      drawScreen_list();
      return;
    } else {

      ALTSCREEN = t;
      SHOWMAIN = !SHOWMAIN;
      if (SHOWMAIN) {
        drawScreen_Clock();
        drawScreen_Weather();
      }      else     drawScreen_list();
      delay(100);
    }    
  }

#endif

  server.handleClient();
  timeClient.update();

  if (OldTime[0] != second()) {
    OldTime[0] = second();
    //do stuff every second

    if (t-ALTSCREEN<ALTSCREENTIME && SHOWMAIN==false) {

      uint32_t b = millis()/1000;
      
      tft.setTextColor(FG_COLOR,BG_COLOR);

      tft.setTextFont(SMALLFONT);
      tft.setCursor(0,440);
      tft.print(dateify(0,"hh:nn:ss"));
      tft.print("  ");
      tft.print((uint32_t) heap_caps_get_free_size(MALLOC_CAP_8BIT)/1000);
      tft.print("k  ");
      tft.print((uint32_t) ESP.getMinFreeHeap()/1000);
      tft.print("k");
      
      tft.setCursor(0,440+tft.fontHeight(SMALLFONT)+2);
      
      tft.printf("Alive since %s",dateify(ALIVESINCE));
      /*
      if (b > 86400) {
        tft.print((int) (b)/86400);
        tft.print("d");
      } else {
        if (b > 3600) {
          tft.print((int) (b)/3600);
          tft.print("h");
        } else {
          if (b > 120) {
            tft.print((int) (b)/60);
            tft.print("m");  
          } else {
            tft.print((int) (b));
            tft.print("s");
          } 
          tft.print("             ");
        }
      }
      */
      
      tft.setCursor(0,440+2*(tft.fontHeight(SMALLFONT)+2));
      if (lastUploadTime>0) {
      tft.printf("Last upload: %s @ %d min ago",lastUploadSuccess ? "success" : "fail", (int) ((now()-lastUploadTime)/60) );
      tft.setCursor(0,440+3*(tft.fontHeight(SMALLFONT)+2));
      }

      tft.printf("Next Upload in: %d min, #Failed: %u min",uploadQ - minute(t)%uploadQ,UploadFailCount);
 

      tft.setCursor(0,Ypos);
    }      
  }
      
  


  if (OldTime[1] != minute()) {
  OldTime[1] = minute();
  //do stuff every minute
  
    if (t-ALTSCREEN<ALTSCREENTIME && SHOWMAIN==false) drawScreen_list;
    else {
      if (SHOWMAIN==false) {
        SHOWMAIN=true;
        drawScreen_Weather();
      }
      drawScreen_Clock();

    }
    
    if (ready && ((lastUploadSuccess && (OldTime[1]%uploadQ)==0) || (lastUploadSuccess==false && (OldTime[1]%uploadQFailed)==0))) {
      #ifdef _DEBUG
        Serial.println("uploading to sheets!");
      #endif
      if (file_uploadSensorData()==false) {
        //tft.setTextFont(SMALLFONT);
        //sprintf(tempbuf,"Failed to upload!");
        //tft.drawString(tempbuf, 0, Ypos);
        //Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
        lastUploadSuccess=false;
      }
      else {
        lastUploadSuccess=true;
      }
    }

    if (OldTime[2] != hour()) {
      OldTime[2] = hour(); 
      getWeather();
      drawScreen_Weather();
      writeSensorsSD("/Data/SensorBackup.dat");
    }

    if (OldTime[3] != weekday()) {
      
      UploadFailCount = 0;

      OldTime[3] = weekday();
      //daily


      for (byte j=0;j<SENSORNUM;j++) {
        Sensors[j].snsValue_MAX = -10000;
        Sensors[j].snsValue_MIN = 10000;
        
        if (t>86400 && Sensors[j].timeLogged<(t-86400)) initSensor(j);

      }

      

    }
  }
}



void fcnChooseTxtColor(byte snsIndex) {

  if (bitRead(Sensors[snsIndex].Flags,0)) {
    tft.setTextColor(TFT_RED,TFT_BLACK);
  }
  else {
    tft.setTextColor(FG_COLOR,BG_COLOR);
  }
  return;
}



void getWeather() {
  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    int httpCode=404;

    tempstring = "http://192.168.68.93/REQUESTWEATHER?hourly_temp=00&hourly_temp=1&hourly_temp=2&hourly_temp=3&hourly_temp=4&hourly_temp=5&hourly_temp=6&hourly_temp=7&hourly_temp=8&hourly_temp=9&hourly_temp=10&hourly_temp=11&hourly_weatherID=0&hourly_weatherID=1&hourly_weatherID=2&hourly_weatherID=3&hourly_weatherID=4&hourly_weatherID=5&hourly_weatherID=6&hourly_weatherID=7&hourly_weatherID=8&hourly_weatherID=9&hourly_weatherID=10&hourly_weatherID=11&daily_weatherID=0&daily_weatherID=1&daily_weatherID=2&daily_weatherID=3&daily_weatherID=4&daily_tempMax=0&daily_tempMax=1&daily_tempMax=3&daily_tempMax=2&daily_tempMax=4&daily_tempMin=0&daily_tempMin=1&daily_tempMin=2&daily_tempMin=3&daily_tempMin=4&isFlagged=1&sunrise=1&sunset=1";
    http.useHTTP10(true);    
    http.begin(wfclient,tempstring.c_str());
    httpCode = http.GET();
    payload = http.getString();
    http.end();



    for (byte j=0;j<12;j++) {
      hourly_temp[j] = payload.substring(0, payload.indexOf(";",0)).toInt(); 
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    }

    for (byte j=0;j<12;j++) {
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

    if (payload.substring(0, payload.indexOf(";",0)).toInt()==0) isFlagged=false;
    else isFlagged=true;
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    sunrise=payload.substring(0, payload.indexOf(";",0)).toInt();
      payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    sunset = payload.substring(0, payload.indexOf(";",0)).toInt();

  }

}


void drawScreen_Clock() {
  int Y = 0, X=0 ;
  uint16_t clockHeight = tft.fontHeight(8)+6+tft.fontHeight(4)+1;

  tft.setTextColor(FG_COLOR,BG_COLOR);

  //clear clock area and draw clock
  tft.fillRect(0,0,tft.width(),clockHeight,BG_COLOR);
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
}

void drawScreen_Weather() {
  int Y = 0, X=0 ;
  uint16_t clockHeight = tft.fontHeight(8)+6+tft.fontHeight(4)+1;

  Y+=clockHeight+1;

  tft.fillRect(0,clockHeight,tft.width(),tft.height()-clockHeight,BG_COLOR);

  tft.setTextColor(TFT_BLACK,BG_COLOR);
  tft.setTextDatum(MC_DATUM);
    
  uint32_t n=now();
  if ((n<sunrise && n<sunset) || ( n>sunset)) snprintf(tempbuf,66,"/Icons/BMP180x180night/%d.bmp",ID2Icon(daily_weatherID[0]));  //it is the night 
  else snprintf(tempbuf,66,"/Icons/BMP180x180day/%d.bmp",ID2Icon(daily_weatherID[0]));

  uint8_t bmpsz = 180;
  drawBmp(tempbuf,15,Y,-1); //no transparent, as -1 will prevent transparency
   
  byte FNTSZ=8;
  tft.setTextFont(8);
  Y+=bmpsz/2;
  X = (bmpsz+15) + (tft.width() - (bmpsz + 15))/2 ;
  fcnPrintTxtColor(hourly_temp[0],FNTSZ,X,Y-5); //adjust midpoint by 5 px for visual appeal


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
    if (((n+j*2*3600)<sunrise && (n+j*2*3600)<sunset) || ( (n+j*2*3600)>sunset)) snprintf(tempbuf,66,"/Icons/BMP60x60night/%d.bmp",ID2Icon(hourly_weatherID[j*2]));  //it is the night 
    else     snprintf(tempbuf,55,"/Icons/BMP60x60day/%d.bmp",ID2Icon(hourly_weatherID[j*2]));
    drawBmp(tempbuf,(j-1)*X+X/2-30,Y);
 
    tft.setTextFont(FNTSZ);
    tft.setTextDatum(MC_DATUM);

    snprintf(tempbuf,55," @%s",dateify((n+j*2*3600),"hh"));  
    fcnPrintTxtColor(hourly_temp[j*2],4,(j-1)*X+X/2,Y+60+tft.fontHeight(FNTSZ),"",(String) tempbuf);

//    tft.drawString(tempbuf,(j-1)*X+X/2,Y+2+60+tft.fontHeight(4)/2);

  }

//  Y += 60+2+2*(tft.fontHeight(4)+2);
  Y += 60+(tft.fontHeight(FNTSZ)+2);
  
  
  X=TFT_WIDTH/3;
  for (byte j=1;j<4;j++) {
    snprintf(tempbuf,65,"/Icons/BMP60x60/%d.bmp",ID2Icon(daily_weatherID[j]));
    drawBmp(tempbuf,(j-1)*X+X/2-30,Y);

    FNTSZ=4;
    tft.setTextFont(FNTSZ);
    fcnPrintTxtColor2(daily_tempMAX[j],daily_tempMIN[j],FNTSZ,(j-1)*X+X/2,Y+2+60+tft.fontHeight(FNTSZ)/2);
    snprintf(tempbuf,55,"%s",fcnDOW(n+j*86400));
    tft.setTextDatum(MC_DATUM);    
    tft.drawString(tempbuf,(j-1)*X+X/2,Y+2+60+tft.fontHeight(FNTSZ)+2,2);
  }

  Y += 60+5+tft.fontHeight(FNTSZ)+5;
  tft.setTextDatum(TL_DATUM);
}

int ID2Icon(int weatherID) {
  //provide a weather ID, obtain an icon ID

  if (weatherID>=200 && weatherID<300) { //t-storm group
    if (weatherID==200 || weatherID==201 || weatherID==210 || weatherID==211 ||  weatherID>229) {
      return 200;
    } else {
      return 201;
    }
  }
    
  if (weatherID>=300 && weatherID<400) { //drizzle group
    return 301;
  }

  if (weatherID>=500 && weatherID<600) { //rain group
    if (weatherID==500 || weatherID==520 ) return 500;
    if (weatherID==501 || weatherID==521) return 501;
    if (weatherID==502 || weatherID==522) return 502;
    if (weatherID==503 || weatherID == 504 || weatherID==531) return 503;
    if (weatherID==511) return 511;
  }
    
  if (weatherID>=600 && weatherID<700) { //snow group
    if (weatherID==600 || weatherID==615 || weatherID==620 ) return 600;
    if (weatherID==601 || weatherID==616|| weatherID==621) return 601;
    if (weatherID==602 || weatherID==622) return 602;
    return 611; //sleet category
    
  }

  if (weatherID>=700 && weatherID<800) { //atm group
    return 700; //sleet category
  }

  if (weatherID>=800) { //clear group
  
    if (weatherID>=801 && weatherID<803) return 801;
    return weatherID; //otherwise weatherID matches iconID!
    
  }

  return 999;
}


int fcn_write_sensor_data(byte i, int y) {
    //i is the sensor number, y is Ypos
    //writes a line of data, then returns the new y pos
      tft.setTextColor(FG_COLOR,BG_COLOR);
      tft.setTextDatum(TL_DATUM);
      fcnChooseTxtColor(i);

      tft.setTextFont(SMALLFONT);

      snprintf(tempbuf,60,"%d.%d", Sensors[i].ardID,Sensors[i].snsType);
      tft.drawString(tempbuf, 0, y);
      
      dateify(Sensors[i].timeLogged,"hh:nn");
      tft.drawString(tempbuf, 47, Ypos);

      snprintf(tempbuf,60,"%s", Sensors[i].snsName);
      tft.drawString(tempbuf, 87, Ypos);
 
      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MIN);
      tft.drawString(tempbuf, 172, Ypos);

      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue);
      tft.drawString(tempbuf, 212, Ypos);
      
      snprintf(tempbuf,60,"%d",(int) Sensors[i].snsValue_MAX);
      tft.drawString(tempbuf, 252, Ypos);

      snprintf(tempbuf,60,"%s",Sensors[i].isSent ? "**" : "");
      tft.drawString(tempbuf, 292, Ypos);

      tft.setTextColor(FG_COLOR,BG_COLOR);

      return y + tft.fontHeight(SMALLFONT)+2;

}

void drawScreen_list() {
  clearTFT();
  tft.setTextColor(FG_COLOR,BG_COLOR);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
    
  Ypos = Ypos + tft.fontHeight(4)/2;
  tft.drawString(dateify(now(),"hh:nn mm/dd"), TFT_WIDTH / 2, Ypos);
  tft.setTextDatum(TL_DATUM);

  Ypos += tft.fontHeight(4)/2 + 6;
  tft.fillRect(0,Ypos,TFT_WIDTH,2,TFT_DARKGRAY);
//            ardid time    name    min  cur   max    * for sent

  tft.fillRect(45,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(85,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(170,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(210,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(250,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  tft.fillRect(290,Ypos,1,TFT_HEIGHT-Ypos,TFT_DARKGRAY);
  Ypos += 4;

  tft.setTextFont(SMALLFONT);
  tft.setTextColor(TFT_DARKGRAY,BG_COLOR);

    snprintf(tempbuf,60,"ID");
    tft.drawString(tempbuf, 0, Ypos);

    snprintf(tempbuf,60,"Time");
    tft.drawString(tempbuf, 47, Ypos);

    snprintf(tempbuf,60,"Name");
    tft.drawString(tempbuf, 87, Ypos);

    snprintf(tempbuf,60,"MIN");
    tft.drawString(tempbuf, 172, Ypos);

    snprintf(tempbuf,60,"Val");
    tft.drawString(tempbuf, 212, Ypos);

    snprintf(tempbuf,60,"MAX");
    tft.drawString(tempbuf, 252, Ypos);

    snprintf(tempbuf,60,"SNT");
    tft.drawString(tempbuf, 292, Ypos);

  tft.setTextColor(FG_COLOR,BG_COLOR);

  Ypos += tft.fontHeight(SMALLFONT) + 2;

  tft.setTextFont(SMALLFONT); //
  byte usedINDEX = 0;  
  byte snsindex[SENSORNUM];

//initialize index to Sensors 
  for (byte i=0;i<SENSORNUM;i++) {
    snsindex[i]=255;
  }

  for(byte i=0;i<SENSORNUM;i++) {
    //march through sensors and list by ArdID
    
    if (Sensors[i].snsID>0 && Sensors[i].snsType>0 && inIndex(i,snsindex,SENSORNUM) == false && Sensors[i].timeLogged>0 ) {
      snsindex[usedINDEX++] = i;
      Ypos = fcn_write_sensor_data(i,Ypos);

      // now find all the remaining sensors for this ardid 
      for(byte j=i+1;j<SENSORNUM;j++) {
        if (Sensors[j].snsID>0 && Sensors[j].snsType>0 && inIndex(j,snsindex,SENSORNUM) == false && Sensors[j].ardID==Sensors[i].ardID) {
          snsindex[usedINDEX++] = j;

          Ypos = fcn_write_sensor_data(j,Ypos);
        }
      }
    }
  }

  //draw rectangle representing button to immediately upload
  
  tft.drawRoundRect(TFT_WIDTH-40,TFT_HEIGHT-40,39,39,5,TFT_BLUE);
  
  tft.fillRoundRect(TFT_WIDTH-40,TFT_HEIGHT-40,39,39,5,TFT_GOLD);
  snprintf(tempbuf,60,"Upload");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLUE,TFT_GOLD);
  
  tft.drawString(tempbuf, TFT_WIDTH-20,TFT_HEIGHT-20);
  tft.setTextColor(FG_COLOR,BG_COLOR);
  
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

void handleLIST() {

  
  char tempchar[9] = "";

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Sensor Server LIST</title></head><body><p>";

  if (server.args()>0) {
    for (byte j=0;j<SENSORNUM;j++)  {
        currentLine += (String) Sensors[j].ardID + "<br>";
        currentLine += IP2String(Sensors[j].IP) + "<br>";
        currentLine += (String) Sensors[j].snsID + "<br>";
        currentLine += (String) Sensors[j].snsType + "<br>";
        currentLine += (String) Sensors[j].snsName + "<br>";
        currentLine += (String) Sensors[j].snsValue + "<br>";
        currentLine += (String) Sensors[j].snsValue_MAX + "<br>";
        currentLine += (String) Sensors[j].snsValue_MIN + "<br>";
        currentLine += (String) Sensors[j].timeRead + "<br>";
        currentLine += (String) Sensors[j].timeLogged + "<br>";
        Byte2Bin(Sensors[j].Flags,tempchar,true);
        currentLine += (String) tempchar + "<br>";
        currentLine += "<br>";      
    }
  } else {
    String logID; //#.#.# [as string], ARDID, SNStype, SNSID
    SensorVal S;

    for (byte k=0;k<server.args();k++) {   
      if ((String)server.argName(k) == (String)"logID")  breakLOGID(String(server.arg(k)),&S.ardID,&S.snsType,&S.snsID);
    }
    int16_t j = findDev(&S, true); //true makes sure finddev always returns a valid entry


    if (j>=0) { 
      currentLine += (String) Sensors[j].ardID + "<br>";
      currentLine += IP2String(Sensors[j].IP) + "<br>";
      currentLine += (String) Sensors[j].snsID + "<br>";
      currentLine += (String) Sensors[j].snsType + "<br>";
      currentLine += (String) Sensors[j].snsName + "<br>";
      currentLine += (String) Sensors[j].snsValue + "<br>";
      currentLine += (String) Sensors[j].snsValue_MAX + "<br>";
      currentLine += (String) Sensors[j].snsValue_MIN + "<br>";
      currentLine += (String) Sensors[j].timeRead + "<br>";
      currentLine += (String) Sensors[j].timeLogged + "<br>";
      Byte2Bin(Sensors[j].Flags,tempchar,true);
      currentLine += (String) tempchar + "<br>";
      currentLine += "<br>";      
    }
  }
currentLine += "</p></body></html>";
  
  server.send(200, "text/html", currentLine.c_str());   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleRoot() {

  String currentLine = "<!DOCTYPE html><html><head><title>Pleasant Sensor Server</title></head><body>";
  
  currentLine = currentLine + "<h2>" + (String) dateify(0,"DOW mm/dd/yyyy hh:nn:ss") + "</h2><br><p>";
  currentLine += "<FORM action=\"/TIMEUPDATE\" method=\"get\">";
  currentLine += "<input type=\"text\" name=\"NTPSERVER\" value=\"time.nist.gov\"><br>";  
  currentLine += "<button type=\"submit\">Update Time</button><br>";
  currentLine += "</FORM><br>";

  
  currentLine += "<a href=\"/LIST\">List all sensors</a><br>";
  currentLine = currentLine + "isFlagged: " + (String) isFlagged + "<br>";
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

  S.timeLogged = now(); //time logged by me is when I received this.
  if (S.timeRead == 0)     S.timeRead = now();
  
  
  S.isSent = false;

  storeSensorSD(&S);

  int16_t sn = findDev(&S, true); //true makes sure finddev always returns a valid entry
  if (sn<0) {
    server.send(401, "text/plain", "Critical failure... I could not find the sensor or enter a new value. This should not be possible, so this error is undefined."); // Send HTTP status massive error
    return;
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




//--------------------------------------
//--------------------------------------
//--------------------------------------
//--------------------------------------
//SPREADSHEET FUNCTIONS

bool file_deleteSpreadsheetByID(String fileID){
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
  bool success = GSheet.listFiles(&filelist /* returned response */);
    #ifdef NOISY
      tft.setTextFont(SMALLFONT);
      snprintf(tempbuf,60,"%s search result: %s\n",sheetname.c_str(),success?"OK":"FAIL");
      tft.drawString(tempbuf, 0, Ypos);
      Ypos = Ypos + tft.fontHeight(SMALLFONT)+2;
    #endif
  
  if (success) {
    #ifdef _DEBUG
      filelist.toString(Serial, true);
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

    resultstring= "ERROR: no files found";
  
    if (createfile) {
      resultstring = file_createSpreadsheet(sheetname);
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
    
  }
  return resultstring; 

}


String file_createSpreadsheet(String sheetname) {


  String fileID = file_findSpreadsheetIDByName(sheetname,true);
  if (fileID.substring(0,5)!="ERROR") return fileID;

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

  
    return "ERROR: Gsheet failed to create";
  }

  return fileID;
}

bool file_createHeaders(String fileID,String Headers) {

  /* only accept file ID
  String fileID = file_findSpreadsheetIDByName(sheetname,true);
  fileID = file_findSpreadsheetIDByName(sheetname);
  */

  if (fileID.substring(0,5)=="ERROR") return false;

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

  time_t t = now();

  String logid;
  String fileID = (String) "ArduinoLog" + (String) dateify(t,"yyyy-mm");
  fileID = file_findSpreadsheetIDByName(fileID,false);

  if (fileID=="" || fileID.substring(0,5)=="ERROR") return false;

  byte rowInd = 0;
  byte snsArray[SENSORNUM] = {255};
  for (byte j=0;j<SENSORNUM;j++) snsArray[j]=255;

  for (byte j=0;j<SENSORNUM;j++) {
    if (Sensors[j].timeLogged>0 && Sensors[j].isSent == false) {
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

  bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
  if (success) {
//            response.toString(Serial, true);
    for (byte j=0;j<SENSORNUM;j++) {
      if (snsArray[j]!=255)     Sensors[snsArray[j]].isSent = true;
    }
    lastUploadTime = now(); 
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


