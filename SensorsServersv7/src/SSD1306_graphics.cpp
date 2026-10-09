#ifdef _USESSD1306


#include "SSD1306_graphics.hpp"
#include "globals.hpp"
#include "hardware_fault.hpp"
#include "utility.hpp"
#if defined(_USEBRYANT)
#include "bryant_bus.hpp"
#endif

SSD1306AsciiWire oled;

bool initOled() {
  if (!isI2CDeviceReady(I2C_OLED)) {
    SerialPrint("OLED not detected on I2C", true);
    if (_I_AM_PERIPHERAL) hardwareFaultSet(HW_FAULT_OLED);
    return false;
  }

  #if RST_PIN >= 0
    oled.begin(_OLEDTYPE, I2C_OLED, RST_PIN);
  #else
    oled.begin(_OLEDTYPE, I2C_OLED);
  #endif
  oled.setFont(System5x7);
  oled.set1X();
  oled.clear();
  oled.setCursor(0,0);
  return true;
}

void redrawOled() {
#if defined(_USEBRYANT)
  bryantOledRefresh();
  return;
#endif

    oled.clear();
    oled.setCursor(0,0);
    oled.printf("[%u] %d:%02d\n",WiFi.localIP()[3],hour(),minute());
  
    byte j=0;
    while (j<Sensors.getNumSensors()) {
        ArborysSnsType* s = Sensors.getSensorBySnsIndex(j);
        if (s && s->deviceIndex == I.MY_DEVICE_INDEX) {
            oled.printf("%d.%d=%d%s\n",s->snsType,s->snsID,(int) s->snsValue, (bitRead(s->Flags,0)==1)?"! ":" ");
        }
        j++;
    }
    oled.println("");    
  
    return;    
  }

  void invertOled() {
    oled.ssd1306WriteCmd(SSD1306_SEGREMAP); // colAddr 0 mapped to SEG0 (RESET)
    oled.ssd1306WriteCmd(SSD1306_COMSCANINC); // Scan from COM0 to COM[N –1], normal (RESET)
  }

  

#endif