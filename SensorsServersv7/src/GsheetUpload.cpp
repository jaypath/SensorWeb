#include "globals.hpp"
#ifdef _USEGSHEET
#include "GsheetUpload.hpp"
#include "timesetup.hpp"
#include "Devices.hpp"
#include "utility.hpp"



//global google cert valid to 2036
#include <Arduino.h>

/*
// Use 'static const char' with 'PROGMEM' to keep this in Flash memory
static const char root_ca_google[] PROGMEM = "-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw\n
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw\n
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA\n
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo\n
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w\n
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw\n
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl\n
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH\n
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8\n
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk\n
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92\n
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p\n
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN\n
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID\n
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E\n
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb\n
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe\n
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy\n
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4\n
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J\n
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef\n
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/\n
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT\n
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ\n
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm\n
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb\n
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n
-----END CERTIFICATE-----";
*/

STRUCT_GOOGLESHEET GSheetInfo;
extern STRUCT_CORE I;
extern Devices_Sensors Sensors;


  
bool initGsheet() {
    // Set the callback for Google API access token generation status (for debug only)
    GSheet.setTokenCallback(tokenStatusCallback);
    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(300);
    //Gsheet.setCert(root_ca_google);
    
    //Begin the access token generation for Google API authentication
    GSheet.begin(GSheetInfo.clientEmail, GSheetInfo.projectID, GSheetInfo.privateKey);
    if (!GSheet.ready()) {
        tftPrint("GSHEET INIT FAILED.", true, TFT_RED);
        storeError("Gsheet initialization failed");
        String reason = GSheet.errorReason();
        if (reason.length() > 0) SerialPrint("GSheet not ready reason: " + reason, true);
        return false;
    }
    return true;
}


void startGsheet() {   
    if (GSheetInfo.useGsheet) {
        tftPrint("Initializing Gsheet... ", false, TFT_WHITE, 2, 1, false, -1, -1);
        
        // Load credentials; if missing/empty, initialize defaults
      if (!readGsheetInfoSD() ||
      strlen(GSheetInfo.clientEmail) == 0 ||
      strlen(GSheetInfo.projectID) == 0 ||
      strlen(GSheetInfo.privateKey) == 0) {
        initGsheetInfo();
        storeGsheetInfoSD();        
        }

      // Only initialize GSheet when WiFi is connected and time is set, as token generation requires valid time
        if (CheckWifiStatus(false)==1 && I.currentTime > 1000) {
            initGsheet();
        } else {
            tftPrint("GSHEET NOT READY - WiFi not connected or time not set", true, TFT_RED);
            storeError("Gsheet not ready - WiFi not connected or time not set");
            return;
        }

    }    else {
        tftPrint("Skipping Gsheet initialization.", true, TFT_GREEN);
    }
}

void initGsheetInfo() {
    GSheetInfo.useGsheet = true;
    GSheetInfo.lastGsheetUploadTime = 0;
    GSheetInfo.lastGsheetUploadSuccess = 0;
    GSheetInfo.uploadGsheetFailCount = 0;
    GSheetInfo.uploadGsheetIntervalMinutes = 20; //MINUTES between uploads
    GSheetInfo.lastErrorTime = 0;
    memset(GSheetInfo.lastGsheetResponse,0,100);
    memset(GSheetInfo.lastGsheetFunction,0,30);
    GSheetInfo.lastGsheetSDSaveTime = 0;
    memset(GSheetInfo.GsheetID,0,64);
    memset(GSheetInfo.GsheetName,0,24);
    
    // Initialize with default values
    strcpy(GSheetInfo.projectID, "arduinodatalog-415401");
    strcpy(GSheetInfo.clientEmail, "espdatalogger@arduinodatalog-415401.iam.gserviceaccount.com");
    strcpy(GSheetInfo.userEmail, "jaypath@gmail.com");
    strcpy(GSheetInfo.privateKey, "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC6e4DR7dGHAwnx\nFe9+B/+RyHGOgflQRVSu0G0xX50CQqLBA5rmVmGsqgLY4lZbFXwYF9NNQtjBMbw/\n5aoTYMRmFjzNETt5hOXRW7xE+VH/krnNZgqNqvOarj8dEEupK9LkknlA/8qql8qP\ng7XEIZLir06ZLDDKqrFiJOk5TPUUVit0Jz+SPRWv3UgAnVk5zgGqwIsil+Q7GXPq\nVmtDOieLL468cz7Bhbvm61q16+WsRf091kiP02BxNBc7Ygf/mea7l3b50GZahCmO\nBVD7LdejUDM8cpxs4lJhVm9OXagQwn64FW1pmBXLaw4FuM4gl/wZsqV60qV8kqo1\nDHZw0NIRAgMBAAECggEAB4ytUHpkdpb9N5wV6vQVkPQkokKBapIy+WSNXh/HAEbc\ntkwQQOHXgJUtqjSADt4P3P0WcCjcE/awroTMic4rp08gSSRI5EyYvfz88k0maF6L\nHH1UgSueAvmpyxI8QOoZ9qhWJbcxUA6W/BBGyxzZmKkkmUOCamsGhUDVqwziOV9p\nqpcpmZKnxlbLtWUaa5YNZCGaVG48KN5WqvUqACrGvQag2tprY/DUvNOPFd+ulAki\nOEFBK/q00Ru3A5l8c+yZRhy5U86nOTZKeDMBIePiEd91rfXHUIwspJ6bRO+zE2UO\nIg3A6TStb6daxyHLD2Dj0SL2exJEpnDp/NeTu+lkYQKBgQDw3Tr+/Z0JhJaIa0je\nTVNWErAJL7Oa87V12L0iJJHR9c0F5Zo++5EF1+uBm+l+raYoaTtEmi/vORE0lOPg\nS/W0l7Z7WPa9JfJCZBNpqIHOSaP+6UP0dUHk/pz21u8+bP1Z01RBvi7Ew21xXEM1\n+JTPhDtE6fEW8jrIqMbZBn9fsQKBgQDGM2+49+9FynTJWKthEJq7A6q9rvt1bhtB\nW8KAgHzKkeHKCVzWj/e+0VhlZrcnDnnjFdJXFEduRSpCycJQBKNKTt1EmBoU5g06\nFweit+FOwSivw7ux1PPUHFSp6ARzK70iCWN9ji3cQVGLdtzfOJU82f5ssKAqPUR8\n5H9nWNqQYQKBgBl835xSDAcQz7kZ2Tkk55epHJWsRY41EdOpnsH5KrEUGKDyHfNi\nPYNnyNULQZcVGwsVr57fzgi7ejWdN8vpXdPBZh8BWALF/C/IVUGOAkZpBoCYAIfi\nzJlF1ChOsDxj3h9ePIFEdcB+iZtATyBr8JtQ+9CcDNYHxe6r5XbbuCjRAoGBAIh5\nBmawoZrGqt+xJGBzlHdNMRXnFNJo/G9mhWkCD+tTw8rf44MCIq7Lazh3H4nPF/Jb\nJjg7iGvPSCgw0JFUgDM8VnNS4DKfrV/gV6udPZCCxEcyWV07qqDU2R8c2WOMLHDx\nUgY0DjPo7gM/1xoE1g3OdLfWbpJnGW99zpQUxHpBAoGAeOoiU25hHxpuuyGZ8d6l\nm/AQAz+ORklGyxOi1HnvZr1y1yVK8+f1sNSTsxe3ZFk4lmeUQdhJWk6/75ma86Fx\nPhV42ri06DSXvU/XUFL8IexqFGrxqet3v104tsUY2PBhyzW/aoIDh2lct2GePjx2\nNyCunqbb3vTNqNcIKOCfLXQ=\n-----END PRIVATE KEY-----\n");
}

//gsheet functions
bool file_deleteSpreadsheetByID(const char* fileID) {
    SerialPrint("file_deleteSpreadsheetByID ");
    FirebaseJson response;
    
    bool success= GSheet.deleteFile(&response /* returned response */, fileID /* spreadsheet Id to delete */);
    
    if (success) {
      SerialPrint("success",true);
        return true;
    }
    else {
        String responseString = "";
        response.toString(responseString,true);
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: %s",responseString.c_str());
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_deleteSpreadsheetByID");
        GSheetInfo.lastErrorTime = I.currentTime;
        SerialPrint("ERROR: failed to delete spreadsheet",true);
        storeError("Gsheet: failed to delete spreadsheet",ERROR_GSHEET_DELETE,true);
        return false;
    }
  }
  
  bool file_deleteSpreadsheetByName(const char* filename){
    SerialPrint("file_deleteSpreadsheetByName");
    char fileID[64];
    bool success = false;
    int deleteCount = 0;
    const int MAX_DELETE_ATTEMPTS = 100; // Prevent infinite loop
    
    snprintf(fileID,64,"%s",file_findSpreadsheetIDByName(filename).c_str());
    while (fileID[0]!='\0' && deleteCount < MAX_DELETE_ATTEMPTS) {
        success = file_deleteSpreadsheetByID(fileID);
        deleteCount++;
        snprintf(fileID,64,"%s",file_findSpreadsheetIDByName(filename).c_str());
    }
    
    if (deleteCount >= MAX_DELETE_ATTEMPTS) {
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: Too many delete attempts");
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_deleteSpreadsheetByName");
        GSheetInfo.lastErrorTime = I.currentTime;
        SerialPrint("ERROR: Too many delete attempts",true);
        storeError("Gsheet: too many delete attempts",ERROR_GSHEET_DELETE,true);
        return false;
    }
    SerialPrint(" OK");
    return success;
  }
  
void file_deleteAllSheets() {
    file_findSpreadsheetIDByName("*",2);
}
void file_grantPermissions() {
    file_findSpreadsheetIDByName("*",1);
} 

String file_findSpreadsheetIDByName(const char* sheetname, uint8_t specialcase) {

    // Searches all files for the matching filename and returns the file ID if found
    // Returns empty string if not found
    SerialPrint("SearchForIDByFilename: " + String(sheetname) + " ");
    FirebaseJson result;
    
    String thisFileID = "";
    //-------
    bool success = GSheet.listFiles(&result /* returned list of all files */,20,"name");     //first page 
    if (!success) {
        result.clear(); // Clean up FirebaseJson
        SerialPrint("ERROR: Failed to list files with error: " + String(GSheet.errorReason()),true);
        return "";
    }

    bool morePages = true;
    while (success && morePages) {
        //put file array into data object
        FirebaseJsonData filesData;
        if (!result.get(filesData, "files")) {
            // No files array in response
            break;
        }
        if (!filesData.success) {
            break;
        }
        
        FirebaseJsonArray filesArr;
        filesData.get<FirebaseJsonArray>(filesArr);

        // Iterate through the array elements
        size_t count = filesArr.size();
        for (size_t i = 0; i < count; i++) {
            FirebaseJsonData fileData;
            filesArr.get(fileData, i); // Get the JSON object at index i
            if (!fileData.success) continue;
            
            FirebaseJson details;
            fileData.get<FirebaseJson>(details);
        
            String nameStr, idStr;
            // Get the "name" and "id" from the file object
            FirebaseJsonData nameData, idData;
            bool isgood1 = details.get(nameData, "name") && nameData.success;
            bool isgood2 = details.get(idData, "id") && idData.success;
            if (isgood1) nameStr = nameData.to<const char*>();
            if (isgood2) idStr = idData.to<const char*>();

            if (isgood1 && isgood2) {
                SerialPrint("file_findSpreadsheetIDByName: File: " + nameStr + ", ID: " + idStr + ", looking for: " + sheetname, true);

                if (strcmp(sheetname, "*") == 0 ) {                            
                    if (specialcase == 1) file_createUserPermission(idStr, false, NULL);                            
                    if (specialcase == 2 || specialcase == 3) {
                        //delete the file
                        file_deleteSpreadsheetByID(idStr.c_str()); //in this case, the data is not removed from the SD card.
                    }
                } else {
                    if (strcmp(nameStr.c_str(), sheetname) == 0) {
                        
                        if (specialcase == 2) {
                            //delete the file
                            file_deleteSpreadsheetByID(idStr.c_str()); //in this case, the data is removed from the SD card.                                
                            SerialPrint(" OK - Deleted file ID: " + idStr,true);
                        } else {
                            if (specialcase == 1) {
                                file_createUserPermission(idStr, false, NULL);                            
                            }
                            SerialPrint(" OK - Found file ID: " + idStr,true);
                        }

                        filesArr.clear();
                        result.clear();
                        return idStr;
                    }
                }
            }
        }
        
        // Check for next page AFTER processing all files on current page
        filesArr.clear();
        FirebaseJsonData nextPageData;
        String nextPageTokenStr = "";
        if (result.get(nextPageData, "nextPageToken") && nextPageData.success) {
            nextPageTokenStr = nextPageData.to<const char*>();
        }
        
        if (nextPageTokenStr.length() > 0) {
            result.clear(); // Clear before getting next page
            success = GSheet.listFiles(&result, 20, "name", nextPageTokenStr.c_str());     //next page 
            morePages = success;
        } else {
            morePages = false;
        }
    }
    
    result.clear(); // Clean up FirebaseJson
    
    //if we get here, we did not find the file
    
    SerialPrint("file_findSpreadsheetIDByName: did not find " + String(sheetname),true);
    return "";
}
  
  uint8_t file_createSpreadsheet(String sheetname, bool checkFile, char* fileID) {
    //returns 0 for fail, 1 for success, 2 for file already exists
    String outcome = "";
    memset(fileID, 0, 64);
    if (checkFile) {
        String foundFileID = file_findSpreadsheetIDByName(sheetname.c_str());
        if (foundFileID.length() > 0) {
            strcpy(fileID, foundFileID.c_str());
            return 2; //file already exists
        } else {
            //do nothing, the file does not exist, so we will create it
            
        }
    } 
  
    FirebaseJson spreadsheet;
    spreadsheet.set("properties/title", sheetname);
    spreadsheet.set("sheets/properties/title", "Sheet1");
    spreadsheet.set("sheets/properties/sheetId", 1); //readonly
    spreadsheet.set("sheets/properties/sheetType", "GRID");
    spreadsheet.set("sheets/properties/sheetType", "GRID");
    spreadsheet.set("sheets/properties/gridProperties/rowCount", 200000);
    spreadsheet.set("sheets/properties/gridProperties/columnCount", 10);
  
    spreadsheet.set("sheets/developerMetadata/[0]/metadataValue", "Jaypath");
    spreadsheet.set("sheets/developerMetadata/[0]/metadataKey", "Creator");
    spreadsheet.set("sheets/developerMetadata/[0]/visibility", "DOCUMENT");
  
    FirebaseJson response;
  
    bool success = GSheet.create(&response /* returned response */, &spreadsheet /* spreadsheet object */, GSheetInfo.userEmail /* your email that this spreadsheet shared to */);

    if (success) {
        // Get the spreadsheet id from already created file.
        FirebaseJsonData result;
        response.get(result, FPSTR("id")); // parse for the file ID
        if (result.success) {
            String fileIDStr = result.to<const char *>();
            strcpy(fileID, fileIDStr.c_str());
        }
        // Get the spreadsheet URL.
        result.clear();
        response.toString(outcome,true);
        response.clear(); // Clean up FirebaseJson
        spreadsheet.clear(); // Clean up FirebaseJson
        SerialPrint(" OK",true);
        return 1;
    } else {
        response.toString(outcome,true);
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: %s",outcome.c_str());
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_createSpreadsheet");
        GSheetInfo.lastErrorTime = I.currentTime;
        response.clear(); // Clean up FirebaseJson
        spreadsheet.clear(); // Clean up FirebaseJson
        SerialPrint(" ERROR: failed to create spreadsheet",true);
        storeError("Gsheet: failed to create spreadsheet",ERROR_GSHEET_CREATE,true);
        return 0;
    }
}
  
  bool file_createHeaders(char* fileID, String Headers) {
    SerialPrint("file_createHeaders... ");
    if (strncmp(fileID, "ERROR", 5) == 0 || strlen(fileID) == 0) {
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: no valid file ID");
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_createHeaders");
        storeError("Gsheet: no valid file ID for headers",ERROR_GSHEET_CREATE,true);
        GSheetInfo.lastErrorTime = I.currentTime;
        SerialPrint(" ERROR: no valid file ID",true);
        return false;
    }

    uint8_t cnt = 0;
    int strOffset=-1;
    String temp;
    FirebaseJson valueRange;
    FirebaseJson response;
    valueRange.add("majorDimension", "ROWS");
    
    while (Headers.length()>0 && Headers!=",") {
        strOffset = Headers.indexOf(",", 0);
        if (strOffset == -1) { //did not find the "," so the remains are the last header.
            temp=Headers;
            Headers="";
        } else {
            temp = Headers.substring(0, strOffset);
            Headers.remove(0, strOffset + 1);
        }

        if (temp.length()>0) valueRange.set("values/[0]/[" + (String) (cnt++) + "]", temp);
    }

    bool success = GSheet.values.append(&response /* returned response */, fileID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
    
    // Clean up FirebaseJson objects
    valueRange.clear();
    response.clear();
    SerialPrint(" OK",true);
    if (!success) {
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: failed to create headers");
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_createHeaders");
        storeError("Gsheet: failed to create headers",ERROR_GSHEET_CREATE,true);
        GSheetInfo.lastErrorTime = I.currentTime;
        SerialPrint(" ERROR: failed to create headers",true);
        return false;
    }
    return true;
}
    
  
  bool Gsheet_uploadSensorDataFunction(void) {
    SerialPrint("Gsheet_uploadSensorDataFunction... ");
    FirebaseJson valueRange;
    FirebaseJson response;
  
    //time_t t = now(); now a global!
  
    String expectedGsheetName = "ArduinoLog" + (String) dateify(I.currentTime,"yyyy-mm");
    if (strlen(GSheetInfo.GsheetID)==0 ||  strcmp(GSheetInfo.GsheetName,expectedGsheetName.c_str())!=0) {
        SerialPrint("Gsheet_uploadSensorDataFunction: need to create new spreadsheet",true);
        //need to create new spreadsheet
        memset(GSheetInfo.GsheetID,0,64);
        strncpy(GSheetInfo.GsheetName, expectedGsheetName.c_str(), 23);
        GSheetInfo.GsheetName[23] = '\0'; // Ensure null termination
        uint8_t success = file_createSpreadsheet(String(GSheetInfo.GsheetName),true,GSheetInfo.GsheetID);
        if (success==0 || strlen(GSheetInfo.GsheetID)==0 || strncmp(GSheetInfo.GsheetID,"ERROR",5)==0) {
            snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR:%s",GSheetInfo.GsheetID);
            snprintf(GSheetInfo.lastGsheetFunction,30,"file_createSpreadsheet");
            storeError("Gsheet: failed to create spreadsheet",ERROR_GSHEET_CREATE,true);
            GSheetInfo.lastErrorTime = I.currentTime;
            SerialPrint("ERROR: failed to create spreadsheet",true);
            return false;
        }
        SerialPrint("Gsheet_uploadSensorDataFunction: created new spreadsheet",true);
        if (!file_createHeaders(GSheetInfo.GsheetID,"DEVID,IPAddress,snsID,SnsName,Time Logged,Time Read,HumanTime,Flags,Measurement")) {
                    snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: failed to create headers");
                    snprintf(GSheetInfo.lastGsheetFunction,30,"Gsheet_uploadSensorDataFunction");
                    storeError(GSheetInfo.lastGsheetResponse,ERROR_GSHEET_HEADERS,true);
                    GSheetInfo.lastErrorTime = I.currentTime;
                    SerialPrint(" ERROR: failed to create headers",true);
                    return false;
        }
    }
    
    byte rowInd = 0;
    const int MAX_ROWS = 50; // Limit to prevent memory issues
    
    int16_t currentPosition = 0;
    while (Sensors.cycleSensors(currentPosition,0) && rowInd < MAX_ROWS) {
        ArborysSnsType* sensor = Sensors.getSensorBySnsIndex(currentPosition);    
        if (sensor && sensor->IsSet) {
            ArborysDevType* device = Sensors.getDeviceBySnsIndex(sensor->deviceIndex);
            if (device && device->IsSet && sensor->timeLogged > sensor->lastCloudUploadTime && sensor->lastCloudUploadTime < I.currentTime-(GSheetInfo.uploadGsheetIntervalMinutes*60)) { //only upload if the last upload was more than the interval minutes ago and the last read time is greater than the last upload time
                valueRange.add("majorDimension", "ROWS");
                valueRange.set("values/[" + (String) rowInd + "]/[0]", (String) device->MAC); //DEVID,IPAddress,snsID,SnsName,Time Logged,Time Read,HumanTime,Flags,Measurement
                valueRange.set("values/[" + (String) rowInd + "]/[1]", device->IP.toString());
                valueRange.set("values/[" + (String) rowInd + "]/[2]", (String) sensor->snsType + "." + (String) sensor->snsID); 
                valueRange.set("values/[" + (String) rowInd + "]/[3]", (String) sensor->snsName);
                valueRange.set("values/[" + (String) rowInd + "]/[4]", (String) sensor->timeLogged);
                valueRange.set("values/[" + (String) rowInd + "]/[5]", (String) sensor->timeRead);
                valueRange.set("values/[" + (String) rowInd + "]/[6]", (String) dateify(sensor->timeLogged,"mm/dd/yy hh:nn:ss")); //use timelogged, since some sensors have no clock
                valueRange.set("values/[" + (String) rowInd + "]/[7]", (String) bitRead(sensor->Flags,0));
                valueRange.set("values/[" + (String) rowInd + "]/[8]", (String) sensor->snsValue);
                sensor->lastCloudUploadTime = I.currentTime;
                rowInd++;
            }
        }
    }

    // For Google Sheet API ref doc, go to https://developers.google.com/sheets/api/reference/rest/v4/spreadsheets.values/append
  
    bool success = GSheet.values.append(&response /* returned response */, GSheetInfo.GsheetID /* spreadsheet Id to append */, "Sheet1!A1" /* range to append */, &valueRange /* data range to append */);
    
    // Clean up FirebaseJson objects
    valueRange.clear();
    response.clear();
    
    if (success) {
        SerialPrint("Uploaded " + (String) rowInd + " rows",true);
        GSheetInfo.lastGsheetUploadTime = I.currentTime; 
    } else {
        snprintf(GSheetInfo.lastGsheetResponse,100,"UPDATE ERROR: %s",GSheetInfo.GsheetID);
        snprintf(GSheetInfo.lastGsheetFunction,30,"Gsheet_uploadSensorData");
        storeError(GSheetInfo.lastGsheetResponse,ERROR_GSHEET_UPLOAD,true);
        GSheetInfo.lastErrorTime = I.currentTime;
        SerialPrint(" ERROR: failed to upload data",true);

        //remove the spreadsheet info stored
        GSheetInfo.GsheetID[0] = '\0';
        GSheetInfo.GsheetName[0] = '\0';
        return false;
    }

    SerialPrint("END of Gsheet_uploadSensorDataFunctionm and file ID is now  " + String(GSheetInfo.GsheetID),true);
    return success;
}
  
  int8_t Gsheet_uploadData() {
    //wrapper function to check upload status here.
    SerialPrint("Gsheet_uploadData... ");
    if (GSheetInfo.useGsheet == false ) {
        SerialPrint("Gsheet is not enabled",true);
        return 0; //everything is ok, just not time to upload!
    }
    if ((GSheetInfo.lastGsheetUploadSuccess>0 && GSheetInfo.lastGsheetUploadTime>0 && I.currentTime-GSheetInfo.lastGsheetUploadTime<(GSheetInfo.uploadGsheetIntervalMinutes*60))) {
        SerialPrint("Not time to upload",true);
        return 0; //everything is ok, just not time to upload!
    }

    
    bool gReady = GSheet.ready();
    if (gReady==false) {
        SerialPrint("ERROR: GSheet not ready",true);
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: GSheet not ready");
        snprintf(GSheetInfo.lastGsheetFunction,30,"Gsheet_uploadData");
        GSheetInfo.lastErrorTime = I.currentTime;
        GSheetInfo.lastGsheetUploadSuccess = -1;    
    } else {
        
        if (Gsheet_uploadSensorDataFunction()==false) {
            snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: GSheet upload failed");
            snprintf(GSheetInfo.lastGsheetFunction,30,"Gsheet_uploadData");
            GSheetInfo.lastErrorTime = I.currentTime;
            GSheetInfo.lastGsheetUploadSuccess=-2;
            GSheetInfo.uploadGsheetFailCount++; //an actual error prevented upload!
        } else {
            GSheetInfo.lastGsheetUploadSuccess=1;
            GSheetInfo.lastGsheetUploadTime = I.currentTime;
            GSheetInfo.uploadGsheetFailCount=0; //upload success!

            storeGsheetInfoSD();       
        }
    }
    SerialPrint(" (1=OK, otherwise=ERROR): " + (String) GSheetInfo.lastGsheetUploadSuccess,true);        
    return GSheetInfo.lastGsheetUploadSuccess; 
}

  void tokenStatusCallback(TokenInfo info)
  {
      SerialPrint("tokenStatusCallback... ");
      if (info.status == token_status_error)
      {
          GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
          GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
          SerialPrint(" ERROR: token error",true);
      }
      else
      {
          GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
          SerialPrint(" OK",true);
      }
  }
  
  String GsheetUploadErrorString() {
    SerialPrint("GsheetUploadErrorString... ");
    String tmp = "";
    if (GSheetInfo.lastGsheetUploadSuccess==1) {
        tmp = "OK: Gsheet upload successful";
    }
    if (GSheetInfo.lastGsheetUploadSuccess==0) {
        tmp = "OK: Gsheet upload not ready or no data";
    }
    if (GSheetInfo.lastGsheetUploadSuccess==-1) {
        tmp = "ERROR: Gsheet not ready";
    } else if (GSheetInfo.lastGsheetUploadSuccess==-2) {
        tmp = "ERROR: Gsheet upload failed";
    }
    if (GSheetInfo.lastGsheetUploadSuccess==-3) {
        tmp = "ERROR: Gsheet upload failed for an unknown reason";
    }
    SerialPrint(" status: " + tmp,true);
    return tmp;
  }



  
//-----------------HTTPS functions to directly interact with Google sheets API, because some functions are not available in the ESP_Google_Sheet_Client library



// GET file metadata (200 -> exists)
bool file_sheetExists(String fileID) {
    String url = "https://www.googleapis.com/drive/v3/files/" + fileID + "?fields=id,name,createdTime";
    String token = GSheet.accessToken();
    
    String headers = "Authorization: Bearer " + token;  // newline if adding more
    String method = "GET";
    String contentType = "";   // none for GET
    String body = "";
    String resp;
    int16_t code;
    // Use "" or "*" for embedded CA bundle (see sdkconfig.defaults); or SD path e.g. "/Certificates/Google.crt"
    //String cacert = "/Certificates/Google.crt"; //use the SD card certificate
    String cacert = ""; //use the ESP-IDF certificate bundle
    
    bool ok = Server_SecureMessageEx(url, resp, code, cacert, method, contentType, body, headers);

    return ok && (code == 200 || code == 201 || code == 409);
    
}


// Create permission
bool file_createUserPermission(String fileID, bool notify, const char* message) {
  
    String url = "https://www.googleapis.com/drive/v3/files/" + fileID + "/permissions?supportsAllDrives=true";
    url += notify ? "&sendNotificationEmail=true" : "&sendNotificationEmail=false";
    
    if (notify && message && strlen(message) > 0) {
        String msg = message;
        msg.replace(" ", "%20");  // light URL encoding
        url += "&emailMessage=" + msg;
      }
    
    String token = GSheet.accessToken();
    
    FirebaseJson j;
    j.add("type", "user");
    j.add("role", "writer");
    j.add("emailAddress", GSheetInfo.userEmail);
    String body;
    j.toString(body, false);
    
    String headers = "Authorization: Bearer " + token + "\nContent-Type: application/json";
    String resp;
    int16_t code;
    // Use "" or "*" for embedded CA bundle; or SD path "/Certificates/Google.crt"
    //String cacert = "/Certificates/Google.crt"; //use the SD card certificate
    String cacert = ""; //use the ESP-IDF certificate bundle
    String method = "POST";
    String contentType = "application/json";
    
    bool ok = Server_SecureMessageEx(url, resp, code, cacert, method, contentType, body, headers);
    
    return ok && (code == 200 || code == 201 || code == 409); //409 permission already exists
}

  #endif