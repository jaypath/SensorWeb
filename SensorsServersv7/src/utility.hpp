#ifndef UTILITY_HPP
#define UTILITY_HPP

#include "globals.hpp"


//globals
#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
extern STRUCT_GOOGLESHEET GSheetInfo;
#endif
#ifdef _USETFT
#include "graphics.hpp"
#endif
 

//setup functions
void initI2C();
bool initSystem();
bool getEmbeddedFirmwareVersion(FirmwareVersion& out);
void getLocalFirmware(FirmwareVersion& out);
String firmwareJsonArray(const FirmwareVersion& fw);
bool isI2CDeviceReady(byte address);
int8_t initSDCard();
bool loadSensorData();
bool loadScreenFlags();


#if _HAS_LOCAL_SENSORS
bool retrieveSensorDataFromMemory(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID, byte* N, uint32_t* t, double* v, uint8_t* f, uint32_t starttime, uint32_t endtime, bool forwardOrder);
int16_t loadAverageSensorDataFromMemory(uint64_t deviceMAC, uint8_t sensorType, uint8_t sensorID, uint32_t* averagedTimes, double* averagedValues, uint8_t averagedFlags[], uint32_t timeStart, uint32_t timeEnd, uint32_t windowSize, uint16_t numPointsX);
bool retrieveMovingAverageSensorDataFromMemory(uint64_t deviceMAC, uint8_t snsType, uint8_t snsID, uint32_t starttime, uint32_t endtime, uint32_t windowSize, uint16_t* numPointsX, double* averagedValues, uint32_t* averagedTimes, uint8_t* averagedFlags, bool forwardOrder);
#endif

bool isTimeValid(uint32_t time);
bool isTempValid(double temp, bool extremeTemp=false);
bool isRHValid(double rh);
bool isSoilCapacitanceValid(double soil);
bool isSoilResistanceValid(double soil);
bool isPressureValid(double pressure);


//bit manipulation functions
//uint8_t
bool isBit(uint8_t value, uint8_t bit);
void clearBit(uint8_t &value, uint8_t bit);
void setBit(uint8_t &value, uint8_t bit);
void flipBit(uint8_t &value, uint8_t bit);

//uint16_t
bool isBit(uint16_t value, uint8_t bit);
void clearBit(uint16_t &value, uint8_t bit);
void setBit(uint16_t &value, uint8_t bit);
void flipBit(uint16_t &value, uint8_t bit);

int8_t shoutThis(String S, bool newline=true, uint16_t color=0xFFFF, byte fontType=2, byte fontsize=1, bool cleartft=false, int x=-1, int y=-1);
bool tftPrint(String S, bool newline=true, uint16_t color=0xFFFF, byte fontType=2, byte fontsize=1, bool cleartft=false, int x=-1, int y=-1);
//serial printing
bool SerialPrint(const char* S, bool newline=false, int8_t level=0);
bool SerialPrint(String S, bool newline=false, int8_t level=0);

int8_t delete_all_core_data(bool flushPrefs = false, bool flushDevicesSensors = false);
uint32_t deleteCoreStruct();
uint32_t deleteDataFiles(bool deleteFlags, bool deleteWeather, bool deleteGsheet, bool deleteDevices);
void failedToRegister();
void initScreenFlags(bool completeInit = false);
void storeCoreData(bool forceStore = true);
void handleStoreCoreData();
void storeError(String E, ERRORCODES CODE=ERROR_UNDEFINED, bool writeToSD = true);
void storeError(const char* E, ERRORCODES Z=ERROR_UNDEFINED, bool writeToSD = true);
String lastReset2String(bool addtime=true);
String getRebootDebugInfo();
void controlledReboot(const char* E, RESETCAUSE R,bool doreboot=true);
int inArrayBytes(byte arr[], int N, byte value,bool returncount=false);
bool inIndex(byte lookfor,byte used[],byte arraysize);
void pushDoubleArray(double arr[], byte N, double value);
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
void Byte2Bin(uint8_t value, char* output, bool invert = false);
void uint16ToBin(uint16_t value, char* output, bool invert = false);
bool stringToUInt64(String s, uint64_t* val, bool isHex);
String breakString(String *inputstr,String token,bool reduceOriginal=true);
bool cycleIndex(int16_t& start, uint16_t arraysize, uint16_t origin, bool backwards=false);
bool cycleByteIndex(byte& start, byte arraysize, byte origin, bool backwards=false);

uint8_t countFlagged(int16_t snsType=0, uint16_t flagsthatmatter = 0b00000011, uint8_t flagsettings= 0b00000011, uint32_t MoreRecentThan=0, bool countCriticalExpired=false, bool countAnyExpired=false, uint16_t optionalsnsflags=0);
void checkHVAC(void);
void initSensor(int k); //k is the index to sensor to init. use -256 [anything <-255] to clear all, and any number over 255 to clear expired (in which case the value of k is the max age in minutes)

String ArrayToString(const uint8_t* Arr, byte len, char separator= '.', bool asHex = false);

// --- MAC address conversion utilities ---
void uint64ToMAC(uint64_t mac64, byte* macArray);
uint64_t MACToUint64(const uint8_t* macArray);
String MACToString(const uint64_t mac64, char separator=':', bool asHex=true);
String MACToString(const uint8_t* mac, char separator=':', bool asHex=true); //wrapper for ip2string

// --- PROCID byte access utility ---
uint8_t getPROCIDByte(uint64_t procid, uint8_t byteIndex);

void systemHousekeeping(bool fullHousekeeping=false);
void updateWifiChannel(); // refresh I.WifiChannel from WiFi.channel() when valid
int16_t force_switch_ota_slot(int slot_number=-1, String* failureDetail=nullptr);

int8_t otaPartitionSlotNumber(const esp_partition_t* partition);
bool getOtaPartitionFirmwareVersion(const esp_partition_t* partition, FirmwareVersion& out);
bool checkOtaSlotAtBoot();
bool check_and_switch_to_newer_firmware(bool verbose=true,bool doswitch=false);

uint8_t returnLiBatteryPercentage(double voltage);
uint8_t returnPbBatteryPercentage(double voltage);
void displaySetupProgress(bool success);

#ifdef _USETFT
void screenWiFiDown();
void displayWiFiStatus(byte retries, bool success);
void displayOTAProgress(unsigned int progress, unsigned int total);
void displayOTAError(int error);

#endif
#endif

