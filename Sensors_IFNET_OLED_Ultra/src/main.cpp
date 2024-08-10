//#define _DEBUG
#define VERSION "4A"

#include "ArduinoOTA.h"

#include <Wire.h>

  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C

#include <ArduinoOTA.h>

 #include <Adafruit_BMP280.h>
  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>

  #include <MPU6500_WE.h>

#define MPU6500_ADDR 0x68
MPU6500_WE mpu = MPU6500_WE(MPU6500_ADDR);





SSD1306AsciiWire oled;

Adafruit_BMP280 bmp; // I2C

#define SUDDENDEATHROUND 6
#define MAXLEVELS 10

#define DELAYQUICK 1000
#define DELAYREGULAR 3000
#define DELAYLONG 10000

//Code to draw to the screen
  #define _OLEDTYPE &Adafruit128x64
//  #define _OLEDTYPE &Adafruit128x32
  #define _OLEDINVERT 0


typedef uint8_t u8;
typedef int16_t i16;
// typedef uint16_t u16;
typedef uint32_t u32;


  String MSG;
String HEADER2 = ""; //line 2 of header
u32 TIMESINCE = 0;

IPAddress SERVER(192,168,100,100);
uint8_t PLAYMODE = 0; //0 - setup, 1 = simple simon with acc, 2 = one on one simon with acc, 3 = quiz with acc, 4 = bmp


struct GameStruct {
  uint32_t nextMsgUpdate;
  byte stage;
  byte substage;
  byte tries;
  byte round; //2 to 20... but levels after SUDDENDEATHROUND are sudden death
  uint8_t game[MAXLEVELS]; //store up to MAXLEVEL entries
  uint8_t gameindex;
  uint8_t guess[MAXLEVELS];
  uint8_t guessindex;
  byte bestScore;
  bool isAlive; //have you failed out? if so quit simon
};

struct SensorStruct {
  byte MYID;
  bool  HASWIFI;
  bool HASMPU;
  bool HASBMP;
  u32 LASTSCREENDRAW;
  byte SCREENREFRESH;
  u32 LASTBUTTON;
  uint32_t LastSendTime;
  bool LastSendStatus;
  uint8_t QUIZSCORE;
  uint8_t QUIZSCOREBEST;
  uint8_t SIMONSCORE;
  uint8_t SIMONLEVEL;
  uint8_t SIMONSCOREBEST;
  uint8_t SIMONLEVELBEST;
  bool displaying;
};

#define _NUMREADINGS 6
struct MPUStruct {
  uint32_t LASTREAD;
  byte RATE_MS; //ms between readings
  uint8_t MPUREADING; //bit 0 = R, 1 = L, 2 = u, 3 = d
  //byte INDEX; //index to mpu values
  //float VALS_LR[_NUMREADINGS]; //store last 6 LR values
  //float VALS_UD[_NUMREADINGS];
  uint8_t LOCKOUT_MS; //10's of ms to not read after a value has been registered. This is needed because a jerk has both + and - accel, just want first one. Note that 3 is 30ms and 30 is 300ms
  float MPUACC_y ; //cut off for acceleration to register
  float MPUACC_x ;
  int TEMPERATURE;
};

SensorStruct INFO;
MPUStruct MPUINFO;
GameStruct GAME;

#define ESP_SSID "IFNET_ELECTRONICS" // Your network name here
#define ESP_PASS "IFNET2024" // Your network password here

bool NEWDATA = false;

bool sendData();
bool playSimon();
char index2char(byte dir);
bool playQuiz();
void printScreen(String s,bool forceredraw=false);

void printLine(String s) {
    oled.print(s.c_str());
    oled.clearToEOL();
    oled.println();
}

void printScreen(String s,bool forceredraw) {

  if (forceredraw==false && INFO.displaying==false) return; //nothing to draw
  INFO.displaying=false;
  //String s will be split at \n and each token will be printed on a line
  
  byte maxlines = 6; //max number of lines
  byte linecount =0; //line number we are on
  int16_t strOffset;
  String temp;
  do {
    strOffset = s.indexOf("|",0);
    if (strOffset>-1) {
      temp = s.substring(0,strOffset);
      s.remove(0,strOffset+1); //+1 because not inclusive of separator
    }
    else temp=s;
    printLine(temp);
    linecount++;
  } while (strOffset>-1);

  for (byte j=linecount;j<=6;j++) printLine(""); //clear the rest of the screen


}


void printHeader() {
  oled.setFont(System5x7);
  oled.set1X();
  oled.setCursor(0,0);

  if (INFO.HASWIFI)   {
    INFO.MYID = WiFi.localIP()[3];
    oled.print("ID:");
    oled.print(INFO.MYID);
    if (INFO.LastSendStatus) oled.print("+");
    else oled.print("-");
  }  else {
    INFO.MYID=0;
    oled.print("Wifi-");
  }



  if (INFO.HASMPU) {
    oled.print(" Acc+ ");
    
  }  else {
    if (INFO.HASBMP) {
      oled.print(" BMP+ ");  
    } else {
      oled.print(" noSns! ");
    }
    
  }
    oled.print(VERSION);    
    oled.print(" ");
    if (millis()-MPUINFO.LASTREAD<MPUINFO.LOCKOUT_MS*10) oled.print("X");



  oled.clearToEOL();
  oled.println();

  oled.print(HEADER2.c_str());
  oled.clearToEOL();
  oled.println(); //now we are at the top of the print area


}

void initGame() {
  GAME.nextMsgUpdate=0;
  GAME.round=1;
  GAME.stage=0;
  GAME.substage=1;
  GAME.tries=0;
  for (byte j=0;j<MAXLEVELS;j++) {
    GAME.guess[j]=0;
    GAME.game[j]=0;
  }
  GAME.gameindex=0;
  GAME.guessindex=0;
  GAME.bestScore=0;
  GAME.isAlive = true;

  INFO.SIMONSCORE=0;
  INFO.SIMONLEVEL=0;
}

void setup()
{  
  INFO.HASBMP = false;
  INFO.HASMPU = false;
  INFO.HASWIFI = false;
  INFO.SCREENREFRESH = 100; //ms for screen refresh
  INFO.SIMONSCORE=0;
  INFO.SIMONLEVEL=0;
  INFO.QUIZSCORE=0;
  INFO.LastSendTime=0;
  INFO.LastSendStatus = false;
  INFO.displaying = false;

  initGame();

  Wire.begin(); // Initalize Wire library
  Wire.setClock(400000L);

  #if RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_OLED, RST_PIN);
  #else // RST_PIN >= 0
    oled.begin(&Adafruit128x64, I2C_OLED);
  #endif // RST_PIN >= 0


  oled.setFont(System5x7);
  oled.set1X();
  oled.clear();
  oled.setCursor(0,0);



  #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("start");
  #endif

  #ifdef _DEBUG
    Serial.println("try wifi...");
  #endif

  #ifdef _DEBUG
    Serial.println("try screen...");

  #endif

  oled.println("NICE! Screen works!");
  delay(500);
  MSG = "Trying to|establish WIFI...";
  printScreen(MSG,true);
  byte r = oled.row();
  
  
  WiFi.begin(ESP_SSID, ESP_PASS);
  TIMESINCE = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-TIMESINCE<10000)
    {
    delay(200);
    
    #ifdef _DEBUG
    Serial.print(".");
    #endif
    oled.setCursor(r,0);

    oled.print("No WIFI...");
    oled.clearToEOL();
    oled.println();
  }

  if(WiFi.status() == WL_CONNECTED) {
    INFO.HASWIFI = true;
    INFO.MYID = WiFi.localIP()[3];
  } else {
    INFO.HASWIFI = false;
    INFO.MYID = 0;
  }

  if (!bmp.begin(0x76) ) {
    INFO.HASBMP=false;
  } else {
    INFO.HASBMP=true;
   /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  }

  oled.clear();
  oled.setCursor(0,0);

  
  if (INFO.MYID>0) {
//    oled.print("Setting up OTA");

    ArduinoOTA.setHostname(MSG.c_str());
    ArduinoOTA.onStart([]() {
      #ifdef _DEBUG
      Serial.println("OTA started");
      #endif
      HEADER2 = "OTA started";
      printHeader();
    });
    ArduinoOTA.onEnd([]() {
      #ifdef _DEBUG
      Serial.println("OTA End");
      #endif
      HEADER2 = "OTA End";
      printHeader();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      #ifdef _DEBUG
          Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
      #endif
      HEADER2 = "Progress: " + String (100*progress / total);
      printHeader();      
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
        if (error == OTA_AUTH_ERROR) HEADER2 = "Auth Failed";
        else if (error == OTA_BEGIN_ERROR) HEADER2 = "Begin Failed";
        else if (error == OTA_CONNECT_ERROR) HEADER2 = "Connect Failed";
        else if (error == OTA_RECEIVE_ERROR) HEADER2 = "Receive Failed";
        else if (error == OTA_END_ERROR) HEADER2 = "End Failed";

        printHeader();
    });
      ArduinoOTA.begin();
      oled.println("... OK");
  } else {
    oled.println(" failed");
  }


    INFO.HASMPU = false;
    if (!mpu.init()) {  // if failed no mpu
      INFO.HASMPU = false;
    } else {
      INFO.HASMPU = true;
    }

    if(INFO.HASMPU) {
      MPUINFO.LASTREAD = 0;
//      MPUINFO.INDEX=0;
      MPUINFO.LOCKOUT_MS = 55; //do not register another reading for this long*10 ms
      MPUINFO.MPUACC_x= 0.015; //in g's
      MPUINFO.MPUACC_y = 0.015;  //in g's
      MPUINFO.RATE_MS = 25;
      

      delay(1000);
      mpu.autoOffsets();

      mpu.enableGyrDLPF();
      mpu.setGyrDLPF(MPU6500_DLPF_6);
      mpu.setSampleRateDivider(5);
      mpu.setGyrRange(MPU6500_GYRO_RANGE_250);
      mpu.setAccRange(MPU6500_ACC_RANGE_2G); //16384 units/g in the +/- 2g range
      mpu.enableAccDLPF(true);
      mpu.setAccDLPF(MPU6500_DLPF_6);
      delay(200);
    }

  HEADER2 = "";
  printHeader();  

  #ifdef _DEBUG
    Serial.println("Set up TimeClient...");
  #endif

  INFO.LASTSCREENDRAW = millis();

  INFO.LASTBUTTON = 0;
  pinMode(0,INPUT_PULLUP);

  NEWDATA = true;
}



float averageArrayF(float* t,byte n,bool init) {
  float avgF = 0;
  for (byte j=0;j<n;j++) {
    avgF+=t[j];
    if (init) t[j] = 0;
  }
  return avgF/n;

}
void loop()
{
  ArduinoOTA.handle();
  randomSeed(micros());
  u32 m = millis();

  if (digitalRead(0) == LOW) {
    INFO.LASTBUTTON = m;    
    PLAYMODE++;
    initGame();
    delay(250); //to avoid multiple button detections
  }
//  HEADER2 = "Button@" + (String) ((m-INFO.LASTBUTTON)/1000) + "s ago";

  if (INFO.HASMPU) {
    if ((m-MPUINFO.LASTREAD) > MPUINFO.RATE_MS) {
      if (m-MPUINFO.LASTREAD > MPUINFO.LOCKOUT_MS*10) {       
        xyzFloat gValue = mpu.getGValues();
        xyzFloat gyr = mpu.getGyrValues();
        float temp = mpu.getTemperature();
        float resultantG = mpu.getResultantG(gValue);

        if ((float) abs(gValue.y) > MPUINFO.MPUACC_y || (float) abs(gValue.x) > MPUINFO.MPUACC_x) { 
          MPUINFO.LASTREAD = m;
          MPUINFO.MPUREADING=0;
          if ((float) abs(gValue.y) >  abs(gValue.x) ) { //LR movement or up move
            if (gValue.y>0) {
              bitWrite(MPUINFO.MPUREADING,1,1);
            }        else {
              bitWrite(MPUINFO.MPUREADING,0,1);
            }
          } else {
            if (gValue.x>0) {
              bitWrite(MPUINFO.MPUREADING,2,1);
            }        else {
              bitWrite(MPUINFO.MPUREADING,3,1);
            }
          }
        }
        
        MPUINFO.TEMPERATURE= ((int) temp*9/5+32);
    
      }
    }
  }

 
  if (m > INFO.LASTSCREENDRAW + INFO.SCREENREFRESH) {

    if (PLAYMODE==0) {
      initGame();
      if (INFO.HASBMP) {
        double alt = bmp.readAltitude();
        alt = alt*3.281;
        int lpart = (int) alt;
        int rpart = (int) ((double) 12*(alt-lpart));
        MSG = "Pressure: " + (String) (bmp.readPressure()/100) + "hPa|" "Alt: " + (String) (lpart) + "ft " + (String) (rpart) + "in |Temp: " + (String) ((bmp.readTemperature()*9/5+32) ) + "F" ; //in imperial units
        INFO.displaying=true;
      }
      if (INFO.HASMPU) {
        HEADER2 = (String) (int) MPUINFO.TEMPERATURE + "F Game:" + (String) INFO.SIMONSCORE + " QUIZ:" + (String) INFO.QUIZSCORE + " " + index2char(255);

        MSG = "Push Boot Button|to play Simon.";
        INFO.displaying=true;
      }
    
    } else {
      if (PLAYMODE==1) {
        if (INFO.HASMPU) {

          if (playSimon()==false) {
            MSG = "Push Boot Button|to take quiz.";
            INFO.displaying=true;
          }
          if (INFO.SIMONSCOREBEST<INFO.SIMONSCORE) {
            INFO.SIMONSCOREBEST=INFO.SIMONSCORE;
            INFO.SIMONLEVELBEST=INFO.SIMONLEVEL;  
            NEWDATA=true;
          }
        }
      } else {
        if (PLAYMODE==2) {
          if (INFO.HASMPU) {

            if (playQuiz()==false) {
              MSG = "Go to Main menu.";
              INFO.displaying=true;
            } 
          }
        } else PLAYMODE = 0;
      }
    }
    printHeader();
    HEADER2 = "";
        
    printScreen(MSG);
  }

  if (NEWDATA) {
    INFO.LastSendStatus=sendData();
    NEWDATA  =false;
  }
}

bool sendData() {
  WiFiClient wfclient;
  HTTPClient http;
  if (INFO.SIMONSCOREBEST<INFO.SIMONSCORE) {
    INFO.SIMONSCOREBEST=INFO.SIMONSCORE;
    INFO.SIMONLEVELBEST=INFO.SIMONLEVEL;  
  }

  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?ID=" + String(INFO.MYID);
    tempstring = tempstring + "&HASOLED=1" ;
    tempstring = tempstring + "&HASBMP=" + String(INFO.HASBMP);
    tempstring = tempstring + "&HASMPU=" + String(INFO.HASMPU);
    
    tempstring = tempstring + "&QuizScore=" + String(INFO.QUIZSCORE);
    tempstring = tempstring + "&SimonLevel=" + String(INFO.SIMONLEVELBEST);
    tempstring = tempstring + "&SimonScore=" + String(INFO.SIMONSCOREBEST);

    
  
    URL = "http://" + SERVER.toString();
    URL = URL + tempstring;
    
      http.useHTTP10(true);
    //note that I'm coverting lastreadtime to GMT
  
    INFO.LastSendTime = millis();

    http.begin(wfclient,URL.c_str());
    httpCode = (int) http.GET();
    payload = http.getString();
    http.end();


    if (httpCode == 200) {
      return true;
    } 
  }
   return false;
}


char index2char(byte dir) {
  //set dir to 255 to use INFO.mpu
  char out = 0;
  if (dir==255) {
    switch (MPUINFO.MPUREADING) {
    case 0:
      break;
    case 1:
      out='R';
      break;
    case 2:
      out='L';
      break;
    case 4:
      out='U';
      break;
    case 8:
      out='D';
      break;
    default:
      break;
    }
  } else {
    switch (dir) {
      case 0:
        break;
      case 1:
        out='R';
        break;
      case 2:
        out='L';
        break;
      case 3:
        out='U';
        break;
      case 4:
        out='D';
        break;
      default:
        break;      
    }
  }
  return out;  

}

bool playSimon() {

uint32_t m=millis();
byte score = 0;
byte errors = 0;
char i=0;


if (GAME.isAlive == false) {
  if (GAME.nextMsgUpdate>m) {
    printScreen(MSG);
    delay(GAME.nextMsgUpdate-m);  
    GAME.nextMsgUpdate=0;
  }

  if (INFO.SIMONSCOREBEST<INFO.SIMONSCORE) {
    INFO.SIMONSCOREBEST=INFO.SIMONSCORE;
    INFO.SIMONLEVELBEST=INFO.SIMONLEVEL;  
    NEWDATA=true;
  }

  return false; //do not play simon
}

if (GAME.nextMsgUpdate>m) {
  
  HEADER2 = (String) int((GAME.nextMsgUpdate-m)/1000) + "s left. Round " + (String) GAME.round;
  
}

if (GAME.tries>=3) {
  GAME.round++;
  GAME.tries=0;
  
  for (byte j=0;j<MAXLEVELS;j++) {
    GAME.guess[j]=0;
    GAME.game[j]=0;
  }
  GAME.gameindex=0;
  GAME.guessindex=0;

  GAME.bestScore=0;
  GAME.stage=1;
  GAME.substage=1;
  
}

if (INFO.SIMONSCOREBEST<INFO.SIMONSCORE) {
  INFO.SIMONSCOREBEST=INFO.SIMONSCORE;
  INFO.SIMONLEVELBEST=INFO.SIMONLEVEL;  
  NEWDATA=true;
}

if (GAME.stage==0) { //step 0 - display instructions
  MSG = "I will show you|a series of moves|to make.|Good luck!";
  INFO.displaying = true;
  GAME.nextMsgUpdate = m+DELAYREGULAR;
  GAME.stage++;


  return true;
} else {
  if (GAME.stage==1) {
    if (m>=GAME.nextMsgUpdate) {
      //wait stage
      GAME.stage++;
      MSG="";
      return true;
    }
  } else {
    if (GAME.stage==2) {
      //initiate simon stage
      for (byte j=0;j<GAME.round;j++) {
        if (j<MAXLEVELS) {
          GAME.game[j] = random(1,5); //max is exclusive
          GAME.guess[j]=0;
        }
      }
      GAME.gameindex=0;
      GAME.guessindex=0;
      GAME.stage++;
      GAME.nextMsgUpdate = m+DELAYQUICK;
      //display moves
      MSG = "Make these moves:|";            
      MSG+= (String) index2char(GAME.game[GAME.gameindex++]); //add the first element
      INFO.displaying = true;
      return true;
    } else {
      if (GAME.stage ==3) {
        //display each subsequent letter
        if (m>GAME.nextMsgUpdate) {
          GAME.nextMsgUpdate = m+DELAYQUICK;                        
          if (GAME.gameindex<GAME.round && GAME.gameindex<MAXLEVELS) {
            MSG+= (String) index2char(GAME.game[GAME.gameindex++]); //add the next element
            INFO.displaying = true;            
          } else {
            GAME.stage++;   
          }
          return true;
        }
      }  else {
        if (GAME.stage == 4) {
          //clear screen
          MSG = "";
          INFO.displaying = true;
          GAME.stage++;  
          delay(50);
          return true;          
        } else {
          if (GAME.stage == 5) {
            //tell user I am accepting input
            MPUINFO.MPUREADING = 0; //reset accel state
            MSG = "Your guess:| |";
            INFO.displaying = true;
            GAME.stage++;            
            GAME.nextMsgUpdate=m+250;
            return true;
          } else {
            if (GAME.stage == 6) {
              MPUINFO.MPUREADING = 0; //reset accel state
              GAME.guessindex=0;                
              if (m>GAME.nextMsgUpdate) {
                GAME.stage++;
                GAME.nextMsgUpdate=m+(GAME.round*DELAYREGULAR)+1000; //total time to accept input
              }
              return true;
            } else {
              if (GAME.stage == 7) {
                if (GAME.nextMsgUpdate<m || GAME.guessindex>= GAME.round || GAME.guessindex>=10) {
                  //time is up or I made the last guess!
                  GAME.stage++;
                } else {
                  INFO.displaying = true;
                  //accept input
                  switch (MPUINFO.MPUREADING) {
                    case 1:
                      MSG+="R";
                      GAME.guess[GAME.guessindex++] = 1;                      
                      break;
                    case 2:
                      MSG+="L";
                      GAME.guess[GAME.guessindex++] = 2;
                      break;
                    case 4:
                      MSG+="U";
                      GAME.guess[GAME.guessindex++] = 3;
                      break;
                    case 8:
                      MSG+="D";
                      GAME.guess[GAME.guessindex++] = 4;
                      break;
                    default:
                      break;
                  }
                  MPUINFO.MPUREADING = 0; //reset accel state
                }
                return true;
              } else {
                if (GAME.stage == 8) {
                  INFO.displaying = true;
                  GAME.stage++;
                  HEADER2 = "GRADING";
                  if (GAME.nextMsgUpdate<m) MSG+="|Time up";
                  else MSG+="|OK.";
                  GAME.nextMsgUpdate=m+DELAYQUICK;

                } else {
                  if (GAME.stage == 9) {
                    if (m>GAME.nextMsgUpdate) GAME.stage++;
                    return true;
                  } else {
                    if (GAME.stage == 10) {
                      //grde the input
                      GAME.guessindex=0;
                      GAME.gameindex=0;
                      INFO.displaying = true;
                      GAME.stage++;
                      for (byte j=0;j<GAME.round;j++) {
                        if (GAME.game[j]>0) {
                          if (GAME.game[j]==GAME.guess[j]) score++;                          
                          else errors++;
                        } 
                      }
                      if (GAME.bestScore<score) GAME.bestScore = score;
                      MSG="Score = " + (String) score + "|You: ";
                      for (byte j=0;j<GAME.round;j++) {
                        i=index2char(GAME.guess[j]);
                        if (i>0) MSG+= String ((char) i);
                      }
                      MSG+="|Simon: ";
                      for (byte j=0;j<GAME.round;j++) {
                        i=index2char(GAME.game[j]);
                        if (i>0) MSG+= String ((char) i);
                      }
                      
                      if (errors>0) {
                        if (GAME.round>=SUDDENDEATHROUND) {
                          //game over
                            MSG+= "|Game over!";
                            GAME.nextMsgUpdate=m+DELAYLONG;          
                            GAME.isAlive=false;        
                            INFO.SIMONSCORE+=GAME.bestScore;   
                            NEWDATA=true;                    
                        } else {
                          if (GAME.tries<3) {
                            MSG += "|Try again!";
                            GAME.tries++;
                            GAME.nextMsgUpdate=m+DELAYREGULAR;          
                          } else {
                            MSG += "|Next round";
                            GAME.nextMsgUpdate=m+DELAYREGULAR;          
                            GAME.tries=0;

                            INFO.SIMONSCORE+=GAME.bestScore;   
                            INFO.SIMONLEVEL=GAME.round++;    
                            NEWDATA=true;
                          }
                        }
                      } else { //errors = 0
                        if (GAME.round<SUDDENDEATHROUND-1) {
                          MSG+="|PERFECT!";                            
                          GAME.nextMsgUpdate=m+DELAYREGULAR;                           
                        } else {
                          MSG+="|Sudden death!";                          
                          GAME.nextMsgUpdate=m+DELAYREGULAR;          
                        }
                        GAME.tries=0;
                        INFO.SIMONSCORE+=GAME.bestScore;   
                        INFO.SIMONLEVEL=GAME.round++;
                        NEWDATA=true;

                      }
                      return true;
                    } else {
                      if (GAME.stage == 11) {
                        if (m>GAME.nextMsgUpdate) {
                          //go back to display and guess stage/substage
                          GAME.stage=1;
                          GAME.substage=1;
                        }
                        return true;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}



 return true;
}

char getTrueFalse(){
  char out = 0;

  switch (MPUINFO.MPUREADING) {
    case 0:
      break;
    case 1:
      out='F';
      break;
    case 2:
      out='T';
      break;
    case 4:
      out=0;
      break;
    case 8:
      out=0;
      break;
    default:
      break;
  }

  return out;
}

bool playQuiz() {
  
uint32_t m=millis();

char answer=0;

if (GAME.isAlive == false) {
  if (GAME.nextMsgUpdate>m) {
    printScreen(MSG);
    delay(GAME.nextMsgUpdate-m);  
    GAME.nextMsgUpdate=0;
  }
  NEWDATA=true;

  return false; //do not play quiz
}

if (GAME.nextMsgUpdate>m) {
  
  HEADER2 = (String) int((GAME.nextMsgUpdate-m)/1000) + "s left. Q:" + (String) GAME.round;
  
}
//Serial.println(GAME.stage);

if (GAME.stage==0) { //step 0 - display instructions
  initGame();
  MSG = "Move L for TRUE|Move R for FALSE|Only your first|score counts!";
  GAME.nextMsgUpdate=m+DELAYLONG;
  INFO.displaying=true;
  GAME.stage++;
  return true;
} else {
  if (GAME.stage==1) {//wait
    if (m<GAME.nextMsgUpdate) return true;
    GAME.stage++;     
  } else {
    if (GAME.stage==2) { //display Q1
      if (m<GAME.nextMsgUpdate) return true;
    GAME.round=1;
      MSG = "40% of tech CEOs|and founders are|of Indian descent.|<<True -- False>>";
      GAME.nextMsgUpdate=m+DELAYLONG;
      INFO.displaying=true;
      GAME.stage++;
      MPUINFO.MPUREADING = 0; //reset accel state     
    } else {
      if (GAME.stage==3) { //accept answer      
        answer=getTrueFalse();  
        if (answer>0 || m>GAME.nextMsgUpdate) {          
          GAME.stage++;
          if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++; //wrong answers
          else GAME.bestScore++;
          MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          
        }
        return true;
      } else {
        if (GAME.stage==4) { //display Q2
          if (m<GAME.nextMsgUpdate) return true;
          GAME.round++;
    
          MSG = "The CEO of Amazon|is of Indian descent.|<<True--False>>";
          GAME.nextMsgUpdate=m+DELAYLONG;
          INFO.displaying=true;
          GAME.stage++;
          MPUINFO.MPUREADING = 0; //reset accel state     
        } else {
          if (GAME.stage==5) { //accept answer
            answer=getTrueFalse();
            if (answer>0 || m>GAME.nextMsgUpdate) {          
              GAME.stage++;
              if (m>GAME.nextMsgUpdate || answer=='T') GAME.guessindex++;
              else GAME.bestScore++;
                        MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

            }
            return true;
          } else {
            if (GAME.stage==6) { //display Q3
                      if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
              MSG = "The CEO of Microsoft|is of Indian descent.|<<True--False>>";
              GAME.nextMsgUpdate=m+DELAYLONG;
              INFO.displaying=true;
              GAME.stage++;
              MPUINFO.MPUREADING = 0; //reset accel state     
            } else {
              if (GAME.stage==7) { //accept answer
                answer=getTrueFalse();
                if (answer>0 || m>GAME.nextMsgUpdate) {          
                  GAME.stage++;
                  if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++;
                  else GAME.bestScore++;
                            MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                }
                return true;
              } else {
                if (GAME.stage==8) { //display Q4
                          if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                  MSG = "The CEO of Google|is of Indian descent.|<<True--False>>";
                  GAME.nextMsgUpdate=m+DELAYLONG;
                  INFO.displaying=true;
                  GAME.stage++;
                  MPUINFO.MPUREADING = 0; //reset accel state     
                } else {
                  if (GAME.stage==9) { //accept answer
                    answer=getTrueFalse();
                    if (answer>0 || m>GAME.nextMsgUpdate) {          
                      GAME.stage++;
                      if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++;
                      else GAME.bestScore++;
                                MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                    }
                    return true;
                  } else {
                    if (GAME.stage==10) { //next Q
                      if (m<GAME.nextMsgUpdate) return true;
                      GAME.round++;
    
                      MSG = "The CEO of IBM|is of Indian descent.|<<True--False>>";
                      GAME.nextMsgUpdate=m+DELAYLONG;
                      INFO.displaying=true;
                      GAME.stage++;
                      MPUINFO.MPUREADING = 0; //reset accel state     
                    } else {
                      if (GAME.stage==11) { //accept answer
                        answer=getTrueFalse();
                        if (answer>0 || m>GAME.nextMsgUpdate) {          
                          GAME.stage++;
                          if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++;
                          else GAME.bestScore++;
                                    MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                        }
                      } else {
                        if (GAME.stage==12) { //next Q
                                  if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                          MSG = "The CEO of Apple|is of Indian descent.|<<True--False>>";
                          GAME.nextMsgUpdate=m+DELAYLONG;
                          INFO.displaying=true;
                          GAME.stage++;
                          MPUINFO.MPUREADING = 0; //reset accel state     
                        } else {
                          if (GAME.stage==13) { //accept answer
                            answer=getTrueFalse();
                            if (answer>0 || m>GAME.nextMsgUpdate) {          
                              GAME.stage++;
                              if (m>GAME.nextMsgUpdate || answer=='T') GAME.guessindex++;
                              else GAME.bestScore++;
                                        MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                            }
                          } else {
                            if (GAME.stage==14) { //next Q
                                      if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                              MSG = "Bluetooth was invented|by an Indian engineer.|<<True--False>>";
                              GAME.nextMsgUpdate=m+DELAYLONG;
                              INFO.displaying=true;
                              GAME.stage++;
                              MPUINFO.MPUREADING = 0; //reset accel state     
                            } else {
                              if (GAME.stage==15) { //accept answer
                                answer=getTrueFalse();
                                if (answer>0 || m>GAME.nextMsgUpdate) {          
                                  GAME.stage++;
                                  if (m>GAME.nextMsgUpdate || answer=='T') GAME.guessindex++;
                                  else GAME.bestScore++;
                                            MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                                }
                              } else {
                                if (GAME.stage==16) { //next Q
                                          if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                                  MSG = "USB was invented|by an Indian engineer.|<<True--False>>";
                                  GAME.nextMsgUpdate=m+DELAYLONG;
                                  INFO.displaying=true;
                                  GAME.stage++;
                                  MPUINFO.MPUREADING = 0; //reset accel state     
                                } else {
                                  if (GAME.stage==17) { //accept answer
                                    answer=getTrueFalse();
                                    if (answer>0 || m>GAME.nextMsgUpdate) {          
                                      GAME.stage++;
                                      if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++;
                                      else GAME.bestScore++;
                                                MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                                    }
                                  } else {
                                    if (GAME.stage==18) { //next Q
                                              if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                                      MSG = "Wireless comminucation|was discovered by|an Indian engineer.|<<True--False>>";
                                      GAME.nextMsgUpdate=m+DELAYLONG;
                                      INFO.displaying=true;
                                      GAME.stage++;
                                      MPUINFO.MPUREADING = 0; //reset accel state     
                                    } else {
                                      if (GAME.stage==19) { //accept answer
                                        answer=getTrueFalse();
                                        if (answer>0 || m>GAME.nextMsgUpdate) {          
                                          GAME.stage++;
                                          if (m>GAME.nextMsgUpdate || answer=='F') GAME.guessindex++;
                                          else GAME.bestScore++;
                                                    MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                                        }
                                      } else {
                                        if (GAME.stage==20) { //next Q
                                                  if (m<GAME.nextMsgUpdate) return true;
GAME.round++;
    
                                          MSG = "I2C allows chips|to talk to each other.|It requires 3 wires.|<<True--False>>";
                                          GAME.nextMsgUpdate=m+DELAYLONG;
                                          INFO.displaying=true;
                                          GAME.stage++;
                                          MPUINFO.MPUREADING = 0; //reset accel state     
                                        } else {
                                          if (GAME.stage==21) { //accept answer
                                            answer=getTrueFalse();
                                            if (answer>0 || m>GAME.nextMsgUpdate) {          
                                              GAME.stage++;
                                              if (m>GAME.nextMsgUpdate || answer=='T') GAME.guessindex++;
                                              else GAME.bestScore++;
                                                        MSG = "You said |" + (String) ((answer=='T')?"True":"False");
          GAME.nextMsgUpdate=m+DELAYREGULAR;
          INFO.displaying=true;          

                                            }
                                          } else {
                                            if (GAME.stage==22) { //show GAME.bestScore
                                                      if (m<GAME.nextMsgUpdate) return true;

                                              MSG = "You got " + (String) GAME.bestScore + " correct|You got " + (String) (GAME.guessindex) + " wrong";
                                              INFO.displaying=true;  
                                              GAME.nextMsgUpdate = m+DELAYLONG;
                                              if (INFO.QUIZSCORE==0) {
                                                INFO.QUIZSCORE=GAME.bestScore;
                                                NEWDATA = true;
                                              }
                                              GAME.stage++;
                                            } else {
                                                        if (m<GAME.nextMsgUpdate) return true;
                                              GAME.stage=0;
                                              GAME.isAlive=false;
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          } 
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
return true;
}