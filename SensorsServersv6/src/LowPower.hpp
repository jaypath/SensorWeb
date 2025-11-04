#ifdef _USELOWPOWER
#ifndef LOWPOWER_HPP
#define LOWPOWER_HPP
#include "Devices.hpp"
#include "globals.hpp"

struct LowPowerType {
    uint32_t Server_IPs[10] = {0}; //store server IPs to Prefs
    uint64_t Server_MACs[10] = {0}; //store server MACs to Prefs
    byte Server_Count = 0; //number of servers listed in prefs
    bool IsUpToDate = false; //set to true if the Server_IPs array has been updated

    int8_t availableServerIndex() {
        for (int i = 0; i < 10; i++) {
            if (LocalLowPower.Server_IPs[i] == 0) {
                return i;
                break;
            }
        }
        return -1;
    }

    int8_t addServer(uint64_t MAC, uint32_t IP) {
        int8_t index = availableServerIndex();
        if (index == -1) return -1; //no space
        //check if the server is already listed
        if (findServerIndex(MAC,IP) >= 0) return -2; //server already listed
        LocalLowPower.Server_IPs[index] = IP;
        LocalLowPower.Server_MACs[index] = MAC;
        LocalLowPower.Server_Count++;
        LocalLowPower.IsUpToDate = false;
        return index;
    }

    int8_t findServerIndex(uint64_t MAC, uint32_t IP) {
        //updates IP
        for (int i = 0; i < 10; i++) {
            if (LocalLowPower.Server_MACs[i] == MAC) {
                LocalLowPower.Server_IPs[i] = IP;
                return i;
            }
        }
        return -1;
    }

};

extern Devices_Sensors Sensors;
extern STRUCT_PrefsH Prefs;
extern LowPowerType LPStruct;




#endif
#endif
#endif
