#ifdef _USEGSHEET
#ifndef GSHEETUPLOAD_HPP
#define GSHEETUPLOAD_HPP

#include <ESP_Google_Sheet_Client.h>

// Forward declarations - avoid circular includes since globals.hpp includes this file
struct STRUCT_CORE;
class Devices_Sensors;

struct STRUCT_GOOGLESHEET {
    bool useGsheet = true;
    uint32_t lastGsheetSDSaveTime = 0;
    uint32_t lastGsheetUploadTime = 0;
    int8_t lastGsheetUploadSuccess = 0;
    uint8_t uploadGsheetFailCount = 0;
    uint16_t uploadGsheetIntervalMinutes = 20; //MINUTES between uploads
    uint32_t lastErrorTime = 0;
    char lastGsheetResponse[100];
    char lastGsheetFunction[30];
    char GsheetID[64]; //file ID for this month's spreadsheet
    char GsheetName[24]; //file name for this month's spreadsheet
    char projectID[64]; // Google Cloud Project ID
    char clientEmail[128]; // Service Account's client email
    char userEmail[64]; // User's email address
    char privateKey[2048]; // Service Account's private key
};

// Initialize with default values
void startGsheet();
void initGsheetInfo();
bool initGsheet();
String file_findSpreadsheetIDByName(const char* sheetname, uint8_t specialcase=0);
bool file_deleteSpreadsheetByID(const char* fileID);
uint8_t file_createSpreadsheet(const char* sheetname, bool checkfile, char* fileID);
bool file_createHeaders(char* fileID, String Headers);
void tokenStatusCallback(TokenInfo info);
bool file_deleteSpreadsheetByName(const char* filename);
String SearchForIDByFilename(const char* filename);
int8_t Gsheet_uploadData(); //uploads data to gsheets
bool Gsheet_uploadSensorDataFunction(void);
String GsheetUploadErrorString();
void file_grantPermissions();
void file_deleteAllSheets();
bool file_sheetExists(String fileID);
bool file_createUserPermission(String fileID, bool notify, const char* message);


#endif
#endif