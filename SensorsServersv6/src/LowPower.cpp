#ifdef _USELOWPOWER
#include "LowPower.hpp"

void LOWPOWER_readAndSend() {

    uint32_t start_time = millis();
    //wait for WiFi to connect, but not more than 10 seconds
    while (WiFi.status() != WL_CONNECTED && millis() - start_time < 10000) {
        delay(50);
    }     

    // Ensure WiFi is fully connected before attempting to send
    if (WiFi.status() != WL_CONNECTED) {
        SerialPrint("WiFi not connected. Cannot send data.", true);        
    } else {
        byte snscount = readAllSensors(true);
        if (snscount > 0) {
            
            sendAllSensors(true, -1, true);
        } else {
            SerialPrint("No sensors to send", true);        
        }
    }
    LOWPOWER_sleep();            
}

void LOWPOWER_sleep(uint64_t sleepTime) {
    SerialPrint("Sleeping for " + String(sleepTime/1000000) + " seconds...", true);

    //turn off all power pins
    int8_t PowerPins[_SENSORNUM] = _POWERPINS;
    for (byte i=0;i<_SENSORNUM;i++) {
        if (PowerPins[i] >= 0) {
            pinMode(PowerPins[i], OUTPUT);
            digitalWrite(PowerPins[i], LOW);
        }
    }


    esp_sleep_enable_timer_wakeup(sleepTime);
    delay(100);
    esp_deep_sleep_start();
}

void LOWPOWER_Initialize() {
    SerialPrint("Initializing low power mode...", true);


    //turn on all power pins to allow devices to initialize. Note that in low power mode, the power pins are turned on  at startup - not with each sensorread call
    int8_t PowerPins[_SENSORNUM] = _POWERPINS;
    for (byte i=0;i<_SENSORNUM;i++) {
        if (PowerPins[i] >= 0) {
            pinMode(PowerPins[i], OUTPUT);
            digitalWrite(PowerPins[i], HIGH);
        }
    }



}
#endif