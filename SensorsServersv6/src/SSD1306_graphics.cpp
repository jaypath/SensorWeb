#ifdef _USESSD1306


#include "SSD1306_graphics.hpp"

extern MY_DEVICE_INDEX;

void redrawOled() {


    oled.clear();
    oled.setCursor(0,0);
    oled.printf("[%u] %d:%02d\n",WiFi.localIP()[3],hour(),minute());
  
    byte j=0;
    while (j<NUMSENSORS) {
        SnsType* s = Sensors.getSensorBySnsIndex(j);
        if (s && s->snsIndex == MY_DEVICE_INDEX) {
            oled.printf("%d.%d=%d%s\n",s->snsType,s->snsID,(int) s->snsValue, (bitRead(s->Flags,0)==1)?"! ":" ");
        }
        j++;
    }
    oled.println("");    
  
    return;    
  }
  

#endif