#if _HAS_LOCAL_SENSORS
#ifdef _USELEDMATRIX
#include "LEDMATRIX.hpp"
#include "globals.hpp"
#include "Devices.hpp"
#include <freertos/semphr.h>
  
//Defining variables for the LED display:
//FOR HARDWARE TYPE, only uncomment the only that fits your hardware type
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW //blue PCP with 7219 chip
//#define HARDWARE_TYPE MD_MAX72XX:GENERIC_HW
#define MAX_DEVICES 4
#define CS_PIN 5
#define CLK_PIN     18
#define DATA_PIN    23

MD_Parola MAXscreen = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); //hardware spi
//MD_Parola screen = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // Software spi

static SemaphoreHandle_t s_matrixMux = nullptr;

static void matrixLock() {
  if (!s_matrixMux) s_matrixMux = xSemaphoreCreateMutex();
  if (s_matrixMux) xSemaphoreTake(s_matrixMux, portMAX_DELAY);
}

static void matrixUnlock() {
  if (s_matrixMux) xSemaphoreGive(s_matrixMux);
}

void Matrix_Init(void) {
    matrixLock();
    MAXscreen.begin();
    MAXscreen.setIntensity(10);
    MAXscreen.displayClear();
    MAXscreen.setTextAlignment(PA_CENTER);
    MAXscreen.setInvert(false);
    MAXscreen.printf("INIT", LocalTF.MSG);
    matrixUnlock();
 }
 
uint32_t Matrix_Draw(bool allowInvert, const char* message) {
matrixLock();
MAXscreen.displayClear();
MAXscreen.setTextAlignment(PA_CENTER);       
MAXscreen.setInvert(allowInvert);
//screen.print(LocalTF.MSG);
MAXscreen.printf("%s",message);
matrixUnlock();
return millis();    

}


    
#endif
#endif
