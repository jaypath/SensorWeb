#define DEBUG_

#include "ArduinoOTA.h"

#include <Wire.h>

  #include "SSD1306Ascii.h"
  #include "SSD1306AsciiWire.h"
  #define RST_PIN -1
  #define I2C_OLED 0x3C

#include <ArduinoOTA.h>


  #include <WiFi.h> //esp32
  #include <WebServer.h>
  #include <HTTPClient.h>

  #include <MPU6500_WE.h>

#include "painlessMesh.h"

#define   MESH_PREFIX     "whateverYouLike"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555

Scheduler userScheduler; // to control your personal task
painlessMesh  mesh;

#define MPU6500_ADDR 0x68
MPU6500_WE mpu = MPU6500_WE(MPU6500_ADDR);

SSD1306AsciiWire oled;


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


struct SensorStruct {
  byte MYID;
  bool  HASWIFI;
  bool HASMPU;
  bool HASBMP;
  u32 LASTSCREENDRAW;
  byte SCREENREFRESH;
  u32 LASTBUTTON;
  
};

#define _NUMREADINGS 6
struct MPUStruct {
  uint32_t LASTREAD_uS; //last reading microseconds
  xyzFloat gValue;
  double A_x;
  double A_y;
  float ACC_ZERO_Y; //acceleration 0 vector
  float ACC_ZERO_X;
  byte MIN_ACC;
  uint32_t ACC_ZERO_TIME;
  uint32_t ACC_LOCKOUT;
  byte ACC_CHECK_TIME;

  uint8_t MPUREADING; //bit 0 = R, 1 = L, 2 = u, 3 = d
  //byte INDEX; //index to mpu values
  //float VALS_LR[_NUMREADINGS]; //store last 6 LR values
  //float VALS_UD[_NUMREADINGS];

  int TEMPERATURE;
};

SensorStruct INFO;
MPUStruct MPUINFO;

constexpr double Accel_range = 16384/2;
constexpr double Accel_range_inv = 1/Accel_range;


#define ESP_SSID "CoronaRadiata_Guest" // Your network name here
#define ESP_PASS "snakesquirrel" // Your network password here


// declares
void sendMessage() ; // Prototype so PlatformIO doesn't complain

Task taskSendMessage( TASK_SECOND * 1 , TASK_FOREVER, &sendMessage );

void sendMessage() {
  String msg = "Hi from node1";
  msg += mesh.getNodeId();
  mesh.sendBroadcast( msg );
  taskSendMessage.setInterval( random( TASK_SECOND * 1, TASK_SECOND * 5 ));
}

// Needed for painless library
void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("startHere: Received from %u msg=%s\n", from, msg.c_str());
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(),offset);
}


void printLine(String s) {
    oled.print(s.c_str());
    oled.clearToEOL();
    oled.println();
}
void printHeader() {
  oled.setFont(System5x7);
  oled.set1X();
  oled.setCursor(0,0);

  if (INFO.HASWIFI)   {
    INFO.MYID = WiFi.localIP()[3];
    oled.print("ID:");
    oled.print(INFO.MYID);
  }  else {
    INFO.MYID=0;
    oled.print("Wifi-");
  }

  if (INFO.HASMPU) {
    oled.print(" Acc+");
    
  }  else {
    oled.print(" Acc-");    
  }

  if (INFO.HASBMP) {
    oled.print(" BMP+");
    
  }  else {
    oled.print(" BMP-");    
  }

  oled.clearToEOL();
  oled.println();

  oled.print(HEADER2.c_str());
  oled.clearToEOL();
  oled.println(); //now we are at the top of the print area


}

void  MPU_CHECK_VALUE(bool forceZero=false) {
  if (INFO.HASMPU == false) return;
  
  uint32_t n = micros();
  uint32_t m = millis();

  MPUINFO.gValue = mpu.getGValues();


  if (forceZero) {
    MPUINFO.ACC_ZERO_X = MPUINFO.gValue.x;
    MPUINFO.ACC_ZERO_Y = MPUINFO.gValue.y;
    MPUINFO.LASTREAD_uS = n;

    MPUINFO.ACC_ZERO_TIME = m;
    MPUINFO.ACC_LOCKOUT =0;
    MPUINFO.A_y=0;
    MPUINFO.A_x=0;

    return;
  }


  MPUINFO.gValue.x = 9.8*(MPUINFO.gValue.x - MPUINFO.ACC_ZERO_X)*100; //cm/s2
  MPUINFO.gValue.y = 9.8*(MPUINFO.gValue.y - MPUINFO.ACC_ZERO_Y)*100;

  if (abs(MPUINFO.gValue.x) > MPUINFO.MIN_ACC || abs(MPUINFO.gValue.y) > MPUINFO.MIN_ACC &&  m>MPUINFO.ACC_LOCKOUT) {
    
    if (m<MPUINFO.ACC_ZERO_TIME+MPUINFO.ACC_CHECK_TIME) {
      MPUINFO.A_x=0;
      MPUINFO.A_y=0;
      MPUINFO.MPUREADING=0;
      if (abs(MPUINFO.gValue.x)>abs(MPUINFO.A_x)) MPUINFO.A_x = MPUINFO.gValue.x; 
      if (abs(MPUINFO.gValue.y)>abs(MPUINFO.A_y)) MPUINFO.A_y = MPUINFO.gValue.y; 
      MPUINFO.ACC_ZERO_TIME=0;
    } else {
      MPUINFO.ACC_LOCKOUT = m+500;   
      if (abs(MPUINFO.A_y) > abs(MPUINFO.A_x)) {
        if (MPUINFO.A_y>0) bitWrite(MPUINFO.MPUREADING,1,1);//left
        else bitWrite(MPUINFO.MPUREADING,0,1);//right
      } else {
        if (MPUINFO.A_x>0) bitWrite(MPUINFO.MPUREADING,2,1);//up
        else bitWrite(MPUINFO.MPUREADING,3,1);//down
      }
      MPUINFO.A_y=0;
      MPUINFO.A_x=0;

    }
  } else {
    MPUINFO.A_y=0;
    MPUINFO.A_x=0;
    MPUINFO.ACC_ZERO_TIME = m;
    MPUINFO.ACC_LOCKOUT = 0;   
  }



//  float dT = (n - MPUINFO.LASTREAD_uS)/1e6;

  MPUINFO.LASTREAD_uS = n;

  return;

}

void setup()
{

  INFO.HASBMP = false;
  INFO.HASMPU = false;
  INFO.HASWIFI = false;
  INFO.SCREENREFRESH = 100; //ms for screen refresh
  
  //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

  mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT );
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask( taskSendMessage );
  taskSendMessage.enable();

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



  #ifdef DEBUG_
    Serial.begin(115200);
    Serial.println("start");
  #endif

  #ifdef DEBUG_
    Serial.println("try wifi...");
  #endif

  #ifdef DEBUG_
    Serial.println("try screen...");

  #endif

  oled.println("NICE! Screen works!");
  delay(500);
  MSG = "Trying to\nestablish WIFI...";
  oled.println(MSG.c_str());
  byte r = oled.row();
  
  
  WiFi.begin(ESP_SSID, ESP_PASS);
  TIMESINCE = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-TIMESINCE<5000)
    {
    delay(200);
    
    #ifdef DEBUG_
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
      #ifdef DEBUG_
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
      MPUINFO.LASTREAD_uS = 0;
      MPUINFO.MIN_ACC = 5;
      MPUINFO.ACC_ZERO_TIME = 0;
      MPUINFO.ACC_LOCKOUT=0;
      MPUINFO.ACC_CHECK_TIME=200;

      delay(200);
      mpu.autoOffsets();

      mpu.enableGyrDLPF();
      mpu.setGyrDLPF(MPU6500_DLPF_6);
      mpu.setSampleRateDivider(5);
      mpu.setGyrRange(MPU6500_GYRO_RANGE_250);
      mpu.setAccRange(MPU6500_ACC_RANGE_2G); //16384 units/g in the +/- 2g range
      mpu.enableAccDLPF(true);
      mpu.setAccDLPF(MPU6500_DLPF_6);
      delay(200);

      MPU_CHECK_VALUE();      

    }

  HEADER2 = "";
  printHeader();  

  #ifdef DEBUG_
    Serial.println("Set up TimeClient...");
  #endif

  INFO.LASTSCREENDRAW = millis();

  INFO.LASTBUTTON = 0;
  pinMode(0,INPUT_PULLUP);
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
  mesh.update();
  ArduinoOTA.handle();

  u32 m = millis();

  if (digitalRead(0) == LOW) {
    INFO.LASTBUTTON = m;
    MPU_CHECK_VALUE(true);    
  }
//  HEADER2 = "Button@" + (String) ((m-INFO.LASTBUTTON)/1000) + "s ago";
  
  if (INFO.HASMPU) {
    MPU_CHECK_VALUE();




    //MPUINFO.TEMPERATURE= ((int) temp*9/5+32);
  }

  
  if (m > INFO.LASTSCREENDRAW + INFO.SCREENREFRESH) {
    if (INFO.HASMPU) {

      HEADER2 = "T=" + (String) (int) MPUINFO.TEMPERATURE + "F";
      if (m<MPUINFO.ACC_LOCKOUT) {
        if (MPUINFO.MPUREADING == 1) HEADER2 += " R";
        if (MPUINFO.MPUREADING == 2) HEADER2 += " L";
        if (MPUINFO.MPUREADING == 4) HEADER2 += " U";
        if (MPUINFO.MPUREADING == 8) HEADER2 += " D";
        MPUINFO.ACC_LOCKOUT=0;
      }

      String pos;
      pos = "X=" + (String) (MPUINFO.A_x);
      printLine(pos);

      pos = "Y=" + (String) (MPUINFO.A_y);
      printLine(pos);

    }

      for (byte j=0;j<8;j++) printLine(""); //clear the rest of the screen

  printHeader();
  HEADER2 = "";
    
  }

}

