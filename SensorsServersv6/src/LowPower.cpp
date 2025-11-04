#ifdef _USELOWPOWER
#include <nvs_flash.h>

LowPowerType LPStruct;

void LOWPOWER_setup() {

    //1. load the prefs data into the LowPowerType struct, if it exists
    Preferences lowPowerPrefs;
    if (lowPowerPrefs.begin("LOWPOWER", true))  {
        if (lowPowerPrefs.isKey("ServerData")) {        
            lowPowerPrefs.getBytes("ServerData", &LPStruct, sizeof(LPStruct));
        }
        lowPowerPrefs.end();
    }
    
    if (LPStruct.Server_Count > 0) {
        //add the servers in my memory to the devices and sensors list
        for (int i = 0; i < LPStruct.Server_Count; i++) {
            if (LPStruct.Server_IPs[i] == 0) continue;
            Sensors.addDevice(LPStruct.Server_MACs[i], IPAddress(LPStruct.Server_IPs[i]), ((String) "Server" + String(i)).c_str(), 0, 0, 100);
        }
    }


    //2. announce myself and get responses. These are automatically added to the LPStruct.Server_IPs array and to devices and sensors list
    //note that announcemyself returns the number of servers that responded, so will pass if any were present, even those that did not respond!
    while (AnnounceMyself() == 0) {        
        SerialPrint("No servers responded to my presence. Retrying in 10s...", true);
        delay(10000);        
    }
    
    //3. check if servers were added to my list
    if (LPStruct.IsUpToDate == false) saveLowPowerPrefs();

    //return and store/send data!
    return;
}


void saveLowPowerPrefs() {
    Preferences lowPowerPrefs;
    if (!lowPowerPrefs.begin("LOWPOWER", false))         {
        SerialPrint("Failed to create LOWPOWER prefs. Clearing and recreating...", true);
        nvs_flash_erase();

        lowPowerPrefs.begin("LOWPOWER", false);
    }
    LPStruct.IsUpToDate = true;
    lowPowerPrefs.putBytes("ServerData", &LPStruct, sizeof(LPStruct));
    lowPowerPrefs.end();
}



#endif