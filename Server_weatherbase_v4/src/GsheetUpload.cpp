#include "GsheetUpload.hpp"
#include "timesetup.hpp"
#include "Devices.hpp"
#include "utility.hpp"

STRUCT_GOOGLESHEET GSheetInfo;
extern STRUCT_CORE I;
extern Devices_Sensors Sensors;

bool initGsheet() {
    // Set the callback for Google API access token generation status (for debug only)
    GSheet.setTokenCallback(tokenStatusCallback);
    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(300);

    
    //Begin the access token generation for Google API authentication
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
    if (!GSheet.ready()) return false;
    return true;
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
        return false;
    }
    SerialPrint(" OK");
    return success;
  }
  
  
  String file_findSpreadsheetIDByName(const char* sheetname) {
    
        // Searches all files for the matching filename and returns the file ID if found
        // Returns empty string if not found
        SerialPrint("SearchForIDByFilename: " + String(sheetname) + " ");
        FirebaseJson result;
        
        String thisFileID = "";
        
        bool success = GSheet.listFiles(&result /* returned list of all files */);      
    
        if (!success) {
            result.clear(); // Clean up FirebaseJson
            SerialPrint("ERROR: Failed to list files with error: " + String(GSheet.errorReason()),true);
            return "";
        }
    
        //put file array into data object
        FirebaseJsonData response;
        result.get(response,"files");
      
        if (response.success) {
            FirebaseJsonArray thisfile;
            response.get<FirebaseJsonArray /* type e.g. FirebaseJson or FirebaseJsonArray */>(thisfile /* object that used to store value */);
                  
            int fileIndex = 0;
            const int MAX_FILES_TO_CHECK = 50; // Limit to prevent memory issues
        
            do {
                thisfile.get(response, fileIndex++); //iterate through each file
                if (response.success && fileIndex <= MAX_FILES_TO_CHECK) {
                    FirebaseJson fileinfo;
      
                    //Get FirebaseJson data
                    response.get<FirebaseJson>(fileinfo);
      
                    size_t count = fileinfo.iteratorBegin();
                    
                    for (size_t i = 0; i < count; i++) {
                        FirebaseJson::IteratorValue value = fileinfo.valueAt(i);
                        String s1(value.key);
                        String s2(value.value);
                        s2 = s2.substring(1, s2.length()-1);
      
                        if (s1 == "id") thisFileID = s2;
                        if (s1 == "name" && s2 == sheetname) {
                            // Found matching filename
                            fileinfo.iteratorEnd();
                            fileinfo.clear(); // Clean up FirebaseJson
                            thisfile.clear(); // Clean up FirebaseJsonArray
                            result.clear(); // Clean up FirebaseJson
                            SerialPrint(" OK - Found file ID: " + thisFileID,true);
                            return thisFileID;
                        }
                    }
                    fileinfo.iteratorEnd();
                    fileinfo.clear(); // Clean up FirebaseJson
                }
            } while (response.success && fileIndex <= MAX_FILES_TO_CHECK);
            
            thisfile.clear(); // Clean up FirebaseJsonArray
        }
    
        //if we get here, we did not find the file
        result.clear(); // Clean up FirebaseJson
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: file not found");
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_findSpreadsheetIDByName");
        GSheetInfo.lastErrorTime = I.currentTime;
        storeError(GSheetInfo.lastGsheetResponse,ERROR_GSHEET_FIND,true);
        SerialPrint(" ERROR: file_findSpreadsheetIDByName did not find " + String(sheetname),true);
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
  
    bool success = GSheet.create(&response /* returned response */, &spreadsheet /* spreadsheet object */, USER_EMAIL /* your email that this spreadsheet shared to */);

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
        return 0;
    }
}
  
  bool file_createHeaders(char* fileID, String Headers) {
    SerialPrint("file_createHeaders... ");
    if (strncmp(fileID, "ERROR", 5) == 0 || strlen(fileID) == 0) {
        snprintf(GSheetInfo.lastGsheetResponse,100,"ERROR: no valid file ID");
        snprintf(GSheetInfo.lastGsheetFunction,30,"file_createHeaders");
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
    return success;
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
            storeError(GSheetInfo.lastGsheetResponse,ERROR_GSHEET_CREATE,true);
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
    
    uint8_t currentPosition = 0;
    while (Sensors.cycleSensors(&currentPosition,0) && rowInd < MAX_ROWS) {
        SnsType* sensor = Sensors.getSensorBySnsIndex(currentPosition);    
        if (sensor && sensor->IsSet) {
            DevType* device = Sensors.getDeviceBySnsIndex(sensor->deviceIndex);
            if (device && device->IsSet && sensor->lastCloudUploadTime < I.currentTime-(GSheetInfo.uploadGsheetIntervalMinutes*60)) { //only upload if the last upload was more than the interval minutes ago
                valueRange.add("majorDimension", "ROWS");
                valueRange.set("values/[" + (String) rowInd + "]/[0]", (String) device->MAC); //DEVID,IPAddress,snsID,SnsName,Time Logged,Time Read,HumanTime,Flags,Measurement
                valueRange.set("values/[" + (String) rowInd + "]/[1]", IPToString(device->IP));
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