 # include "header.hpp"

#ifdef _USELEAK
  #define _LEAKPIN 12
  #define _LEAKDIO 13
#endif

#ifdef _USESOILRES
  #define SOILRESISTANCE 4700
  #define SOILR_MAX 2000
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0; use A4 or 32 for esp32 
  //const int SOILDIO = _USESOILRES;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USESOILCAP
  const int SOILPIN = A0;  // ESP8266 Analog Pin ADC0 = A0
#endif

#ifdef _USEHCSR04
  #define USONIC_DIV 58   //conversion for ultrasonic distance time to cm
  #define TRIGPIN 2
  #define ECHOPIN 3
#endif

#ifdef _USESSD1306
  //#define _OLEDTYPE &Adafruit128x64
  //#define _OLEDTYPE &Adafruit128x32
  //#define _OLEDINVERT 0
#endif


#ifdef _CHECKAIRCON 
  const uint8_t DIO_INPUTS=2; //two pins assigned
  const uint8_t DIOPINS[2] = {34,35}; //comp then fan

#endif

#ifdef _CHECKHEAT
  const uint8_t DIO_INPUTS=6; //6 sensors
  const uint8_t DIOPINS[6] = {36, 39, 34, 35,32,33}; //ADC bank 1, starting from pin next to EN
  extern const String HEATZONE[];
  extern uint8_t HEATPIN; //this will be used as index to heatzone names
  const String HEATZONE[6] = {"Office","MastBR","DinRm","Upstrs","FamRm","Den"};
  uint8_t HEATPIN = 0; //this will be used as index to heatzone names
#endif



#ifdef _USEDHT

  #define DHTTYPE    DHT11     // DHT11 or DHT22
  #define DHTPIN 2


#endif
