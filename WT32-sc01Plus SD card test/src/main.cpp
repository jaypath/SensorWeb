#define ARDNAME "ESP32Server"

#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here
//#define ESP_SSID "kandy-hispeed" // Your network name here
//#define ESP_PASS "manath77" // Your network password here


#define SD_MISO 38
#define SD_MOSI 40
#define SD_SCLK 39
#define SD_CS 41

#define LGFX_USE_V1         // set to use new version of library

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LovyanGFX.hpp>    
#include <TimeLib.h>
#include <WiFi.h> //esp32
#include <WebServer.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "ArduinoOTA.h"


class LGFX : public lgfx::LGFX_Device
{

  lgfx::Panel_ST7796  _panel_instance;  // ST7796UI
  lgfx::Bus_Parallel8 _bus_instance;    // MCU8080 8B
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_FT5x06  _touch_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.freq_write = 20000000;    
      cfg.pin_wr = 47;             
      cfg.pin_rd = -1;             
      cfg.pin_rs = 0;              

      // LCD data interface, 8bit MCU (8080)
      cfg.pin_d0 = 9;              
      cfg.pin_d1 = 46;             
      cfg.pin_d2 = 3;              
      cfg.pin_d3 = 8;              
      cfg.pin_d4 = 18;             
      cfg.pin_d5 = 17;             
      cfg.pin_d6 = 16;             
      cfg.pin_d7 = 15;             

      _bus_instance.config(cfg);   
      _panel_instance.setBus(&_bus_instance);      
    }

    { 
      auto cfg = _panel_instance.config();    

      cfg.pin_cs           =    -1;  
      cfg.pin_rst          =    4;  
      cfg.pin_busy         =    -1; 

      cfg.panel_width      =   320;
      cfg.panel_height     =   480;
      cfg.offset_x         =     0;
      cfg.offset_y         =     0;
      cfg.offset_rotation  =     0;
      cfg.dummy_read_pixel =     8;
      cfg.dummy_read_bits  =     1;
      cfg.readable         =  true;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       =  true;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();    

      cfg.pin_bl = 45;              
      cfg.invert = false;           
      cfg.freq   = 44100;           
      cfg.pwm_channel = 7;          

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);  
    }

    { 
      auto cfg = _touch_instance.config();

      cfg.x_min      = 0;
      cfg.x_max      = 319;
      cfg.y_min      = 0;  
      cfg.y_max      = 479;
      cfg.pin_int    = 7;  
      cfg.bus_shared = true; 
      cfg.offset_rotation = 0;

      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda  = 6;   
      cfg.pin_scl  = 5;   
      cfg.freq = 400000;  

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);  
    }
    setPanel(&_panel_instance); 
  }
};

static LGFX lcd;            // declare display variable


//wifi

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"time.nist.gov");

  WebServer server(80);

struct __attribute__ ((packed)) SensorVal {
  uint8_t  snsType ;
  uint8_t snsID;
  uint8_t snsPin;
  char snsName[32];
  double snsValue;
  double limitUpper;
  double limitLower;
  uint16_t PollingInt;
  uint16_t SendingInt;
  uint32_t LastReadTime;
  uint32_t LastSendTime;  
  uint8_t Flags; //RMB0 = Flagged, RMB1 = Monitored, RMB2=outside, RMB3-derived/calculated  value, RMB4 =  predictive value
};

union convertSensorVal {
  SensorVal a;
  uint8_t b[sizeof(SensorVal)];
};

char DATESTRING[20]="";
int DSTOFFSET = 0;
byte OLDHOUR = 0;



//declare functions
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void deleteFile(fs::FS &fs, const char * path);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message, char MODE = "a", bool verbose = false);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
char* dateify(time_t = 0, String = "mm/dd/yyyy hh:nn:ss");
char* strPad(char* str, char* pad, byte L);


void setup() {
Serial.begin(115200);
lcd.init();


  // Setting display to landscape
  if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 1);

  lcd.setBaseColor(0x000000u); // Specify black as the background color
  lcd.clear();  
  lcd.setCursor(0,0);
  lcd.printf("Going through setup...\n");

SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, -1);
if(!SD.begin(SD_CS)){
  Serial.println("SD Mount Failed");
}
  lcd.printf("SD card is ok...\n");

uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  lcd.printf("SD Card Size: %lluMB\n", cardSize);


lcd.println("WiFi Starting.");
  WiFi.begin(ESP_SSID, ESP_PASS);
    Serial.print("Wifi Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    lcd.print(".");
  }
  lcd.print("Wifi OK. IP is ");
  lcd.println(WiFi.localIP().toString());

  lcd.println("Connecting ArduinoOTA...");
   ArduinoOTA.setHostname(ARDNAME);

  ArduinoOTA.onStart([]() {
  lcd.clear();  
  lcd.setCursor(0,0);
    lcd.println("OTA started");
  });

  ArduinoOTA.onEnd([]() {
  lcd.clear();  
  lcd.setCursor(0,0);
    lcd.println("OTA End");
    });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  lcd.clear();  
  lcd.setCursor(0,0);
  lcd.printf("Progress: %u%%\n", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    lcd.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) lcd.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) lcd.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) lcd.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) lcd.println("Receive Failed");
    else if (error == OTA_END_ERROR) lcd.println("End Failed");
    
  });
  ArduinoOTA.begin();


}



void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  time_t t = now();

  if (OLDHOUR!=hour()) {
    OLDHOUR = hour();
    //hour has changed
    timeClient.update();
  }



}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  lcd.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if(!root){
    lcd.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    lcd.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      lcd.print("  DIR : ");
      lcd.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      lcd.print("  FILE: ");
      lcd.print(file.name());
      lcd.print("  SIZE: ");
      lcd.println(file.size());
    }
    file = root.openNextFile();
  }
}


void writeFile(fs::FS &fs, const char * path, const char * message, char MODE, bool verbose){
  if (verbose) lcd.printf("Writing file: %s in mode %c\n", path, MODE);

//MODE can be FILE_WRITE, "w", FILE_APPEND, "a"
  File file = fs.open(path, MODE);
  if(!file){
    lcd.println("Failed to open file for writing");
    return;
  }
  if(file.print(message))
  {
    if (verbose) lcd.printf("File written: %s\n",path);
  } else {
    lcd.println("Write failed");
  }
  file.close();
}

void createDir(fs::FS &fs, const char * path){
  Serial.printf("Creating Dir: %s\n", path);
  if(fs.mkdir(path)){
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char * path){
  Serial.printf("Removing Dir: %s\n", path);
  if(fs.rmdir(path)){
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

void readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(fs::FS &fs, const char * path){
  Serial.printf("Deleting file: %s\n", path);
  if(fs.remove(path)){
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16bits(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32bits(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void SDwriteSensor(struct SensorVal *S) {
  //write the sensorval struct to the appropriate file
  //choose file to reflect the current day.
  
  convertSensorVal S2b;
  S2b.a = S;


  File myFile;
  char msg[13]; //see above. +1 for terminator
  bool pending = true;

  a.val = devID + 100; //+100 to avoid special ascii characters
  strPad(devname,10);
  sprintf(msg, "%c%s\n", a.str, devname);

  // open the file. note that only one file can be open at a time,

  //   if (SD.exists("Devices.txt")) SD.remove("devices.txt");

  myFile = SD.open("Devices.txt", FILE_WRITE);

  while (myFile.available() && pending) {
    a.val = myFile.read();
    if (a.val - 100 == devID) {
      //this is the line. update name
      myFile.print(devname);
      pending = false;
    } else {
      while (myFile.available() && myFile.read() != '\n');    //move to next line
      if (myFile.available() == 0) {
        //add the line
        myFile.print(msg);
        pending = false;
      }
    }
  }

  myFile.close();


}



char* SDreadDevName(uint8_t * devID) {
  //read the dev name given the devid


  SDOn();

  bool success = false;
  File myFile;

  convertBYTE a;
  myFile = SD.open("Devices.txt", FILE_READ);
  sprintf(CHARBUF, "");

  while (myFile.available() && success == false) {
    a.val = myFile.read();

    if (a.val == *devID + 100) { //+100 because the low numbers are special ascii characters, including eof
      while (myFile.available() && myFile.peek() != '\n') {
        sprintf(CHARBUF, "%s%c", CHARBUF, myFile.read());
        success = true;
      }
    } else
    {
      while (myFile.available() > 0 && myFile.read() != '\n');      //jump to next entry

    }
  }
  strUnpad(CHARBUF);

  return CHARBUF;

  SDOff();



}

bool SDreadVal(char* filename, unsigned long* sensorTime, unsigned short* sensorVal, unsigned long entry = 1) {
  //the filename is devName-sensorID.txt, ex 10-1.txt
  //read the entry that is n entries ago, entry 1 is most recent
  //note that the sensor values are encoded as sensor val + 100, but will be returned correctly
  //usage: SDread("10-1.txt",&stime, &sval,1);


  SDOn();

  bool success = false;
  File myFile;
  char units[6];
  
  myFile = SD.open(filename, FILE_READ);
  if (entry * LINESIZE >= myFile.size()) {
    entry = 0;
  } else {
    entry = myFile.size() - LINESIZE * entry; //note that each data line is therefore 4 bytes for time, 2 bytes for the int, and 1 byte for '\n' = 7 bytes
  }
  convertULONG msgtime;
  convertINT msgsensor;

  if (myFile) {
    myFile.seek(entry);
    myFile.read(msgtime.str, 4);
    myFile.read(msgsensor.str, 2); //this is the val
    sprintf(units, "     "); //fill units with spaces, so a termination character is at the end
    myFile.read(units, 5); //units, now the last element is guaranteed to be a terminator
    strUnpad(units, ' '); //end at the space
    *sensorTime = msgtime.val;
    *sensorVal = msgsensor.val - 100; //I added 100 on write to avoid 00, EOF
    success = true;
  } else {
    // if the file didn't open, print an error:
    success = false;
  }
  myFile.close();


  SDOff();
  return success;

}

bool SDwriteVal(char* filename, unsigned long* sensorTime, int sensor, uint8_t  intervalMinutes = 60) {
  //the filename is devName-sensorID.txt, ex 10-1.txt
  //I Will make a msg that is unixtime (uns long),sensorval (int)
  //saves as a new line only if the intervalMinutes is at least this many minutes (not seconds) older than the prior line. Default is save hourly values
  //  otherwise saves over the last line
  //
  //note that each data line is binary 4 bytes for time, 2 bytes for the int, 5 bytes for unit, and 1 byte for a new line = 9 bytes
  //ex: SDwrite("jay.txt",&stime, &sval,60);

  char units[6];
  SDOn();

  bool success = false;
  File myFile;

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  myFile = SD.open(filename, FILE_WRITE);

  // if the file opened okay, write to it:
  if (myFile) {
    char msg[LINESIZE];
    convertULONG msgtime;
    convertINT msgsensor;
    msgsensor.val = sensor + 100; //add 100 to avoid entering 00 eof
    if (myFile.size() > LINESIZE) {
      myFile.seek(myFile.size() - LINESIZE); //now I am at the LAST entry's time
      myFile.read(msgtime.str, 4); //this is the LAST entry time
      if (msgtime.val >= *sensorTime + intervalMinutes * 60) {
        myFile.seek(myFile.size());
      } else {
        myFile.seek(myFile.size() - LINESIZE);
      }
    }
    msgtime.val = *sensorTime;
    sprintf(units,"%s",getSnsUnit(sensor));
    strPad(units, 5, ' ');
    
    sprintf(msg, "%s%s%s\n", msgtime.str, msgsensor.str, units);
    myFile.print(msg);
    success = true;
  } else {
    // if the file didn't open, print an error:
    success = false;
  }
  myFile.close();


  SDOff();

  return success;
}

char* dateify(time_t t, String dateformat) {
  if (t==0) t = now();

  char holder[5] = "";

  sprintf(holder,"%02d",month(t));
  dateformat.replace("mm",holder);
  
  sprintf(holder,"%02d",day(t));
  dateformat.replace("dd",holder);
  
  sprintf(holder,"%02d",year(t));
  dateformat.replace("yyyy",holder);
  
  sprintf(holder,"%02d",year(t)-2000);
  dateformat.replace("yy",holder);
  
  sprintf(holder,"%02d",hour(t));
  dateformat.replace("hh",holder);

  sprintf(holder,"%02d",minute(t));
  dateformat.replace("nn",holder);

  sprintf(holder,"%02d",second(t));
  dateformat.replace("ss",holder);
  
  sprintf(DATESTRING,"%s",dateformat.c_str());
  
  return DATESTRING;  
}

char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}

bool SendData(struct SensorVal *snsreading) {

  WiFiClient wfclient;
  HTTPClient http;
    


    bool isGood = false;

  
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?IP=" + MYIP.IP.toString() + "," + "&varName=" + String(snsreading->snsName);
    tempstring = tempstring + "&varValue=";
    tempstring = tempstring + String(snsreading->snsValue, DEC);
    tempstring = tempstring + "&Flags=";
    tempstring = tempstring + String(snsreading->Flags, DEC);
    tempstring = tempstring + "&logID=";
    tempstring = tempstring + String(ARDID, DEC);
    tempstring = tempstring + "." + String(snsreading->snsType, DEC) + "." + String(snsreading->snsID, DEC) + "&timeLogged=" + String(snsreading->LastReadTime, DEC) + "&isFlagged=" + String(bitRead(snsreading->Flags,0), DEC);

    do {
      URL = "http://" + SERVERIP[ipindex].IP.toString();
      URL = URL + tempstring;
    
      http.useHTTP10(true);

      snsreading->LastSendTime = now();
        #ifdef DEBUG_
            Serial.print("sending this message: ");
            Serial.println(URL.c_str());
        #endif
    
      http.begin(wfclient,URL.c_str());
      httpCode = http.GET();
      payload = http.getString();
      http.end();
        #ifdef DEBUG_
          Serial.print("Received: ");
          Serial.println(payload);
          Serial.print("Code: ");
          Serial.println(httpCode);
          Serial.println("*****************");
          Serial.println(" ");
        #endif

        ipindex++;

        if (httpCode == 200) {
          isGood = true;
          SERVERIP[ipindex].server_status = httpCode;
        } 
    } while(ipindex<NUMSERVERS);
  
    
  }
     return isGood;
}
