#include <Arduino.h>
#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif __has_include(<WiFiNINA.h>)
#include <WiFiNINA.h>
#elif __has_include(<WiFi101.h>)
#include <WiFi101.h>
#elif __has_include(<WiFiS3.h>)
#include <WiFiS3.h>
#endif

#include <Wire.h>

#include <ESP_Google_Sheet_Client.h>
#include <time.h>


#define ESP_GOOGLE_SHEET_CLIENT_USE_PSRAM
#define WIFI_SSID "CoronaRadiata_Guest"
#define WIFI_PASSWORD "snakesquirrel"

#define PROJECT_ID "arduinotest-413620"

//Service Account's client email
#define CLIENT_EMAIL "espservice-sa1234@arduinotest-413620.iam.gserviceaccount.com"

//Service Account's private key
const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCRLxXCizKpiZRk\nWQm+la9X91aux7cbkIAde/BYebyhqhPUMGVN/LYnzt33Cf96LBAE9+edylnT7jPG\nzbkBhcSJlUHQg18i/CWMdhWLO8bdDoJ7XwD6fK2uKDyPdsl0CJnSvOHW/R3KLesX\nZEKOn/x7BLzzv4JG1fbnnCmaeqbNm2ftU5BbrdRWPTcRrux6iN/3QKeOnOCpceoz\nhc1ivOEuAdVd3NUhLyvT3PgLQAYPvitZ9YOUON1kQeHc6qEn81cAHC3Zhb8103Jx\neDUI4w2whqBK/pLdno0z0hvS0IjnQNV4i73kHpQr6R1iClsceWxCchYQULVl/Nsa\nMzY07dMZAgMBAAECggEAFRL5nKCp/PQMhzhpcrG1nxY3De3NhYHHJwB7lcwt9mWC\ntVVFDzn0kvizPY3E+M32kVIO29eojFvSRjRMe40YR0RUJlk7cQ+av7XQUMKfjizy\nAch7kuCQMWyDxetfOKma8n0aZyOctGFP87hI9P0CIDVC3DrbRT7/tQT1AQh2t/dz\n+TNAcpkQAqIAh/unQOnqXTiZ32nD6eWpipzISWcBrOotaORxpP+7Es4Q+MapZ9VG\nl75AkO/XZg1ZhjMZXKxOk2eDc8lPGMKlWq+xiQmwnIA86ynpAg1pfnimZGClLyxn\n6004CKLYEs9/kgofX236URxHtMy+BkVuVNPxGi21UQKBgQDB75+8en1UeVAbzxyb\ngQtx7rhK9jrCZhWJlPILjTOVZhB2ZBgoIaCBWyl9gNTRZvt0f5zltTbBBT9rw1GN\nh2PcBZD/FjiMv/CwPg8oJohdKApA3jdqwDK4OvH08s5TI6thX0PQwgR5mTyfnm5J\nq8pSQD2W5g6EEeiO/yk4Rc8srQKBgQC/pWckX4VOHRSiN4GgGmyFw8bz/ltooER4\nE/qqcaLwjnBhScAWJxsOZx/vJfi5LiosgVtvGHOHnM2RDLDngAHzfesmRyttclBI\nC4Av+5cc+j2iC2uTU9mV19o8Nt/29azj3blmJ2E1ktHi/LH2U46uZBbtTsp/030L\nN6M48hXBnQKBgAo9IG3O+bbwAK63LId5NKV7OPecMFX7FcABwRWjCsokbVISzsOv\nos2xhms91f6INVZdNmdaPd3K7SI/WZrjPv5qvND611l2+LoVTK+N+T0R1BjAoqRc\nKVFYq1WHTCVPiMjHQslOtPXGhVDYCHKTQ0c8ZQQGeVW3rFOAXSi/ZsSxAoGAJQwX\ndTuV7HTIsVcjksyo3+7pve8UwpQAyGmsUlinU/NlHBmCrYWfwFgFH1jqzPl0o6fa\nAg7q/nM8debLJ+Vh+y0DUSH+7ihkBplajWwIWUyyr2mfwRo7fLD+ehdtkjMJ2f7Z\nwTqpmw6nzkZlLKYYdfPZTsSldrJugMWfovmBZS0CgYBXsAipozEaWq/CAp55L8BA\neEbb+yI7ada/zcp1oxjq1RSmAmmT3BA1AjbWZwKAgd98+IZbocaqrYwLMyrm7Izp\nilmT/hM109QweqtwaaHoAQUL1hglE3BE58wHW6LA9wMSNamn1WGlpdJ2vvEQJhV4\nBTkrXhICaxKSA8SHlSEd8Q==\n-----END PRIVATE KEY-----\n";

bool taskComplete = false;

// For Pico, WiFiMulti is recommended.
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
WiFiMulti multi;
#endif

uint32_t lastEvent = 0;

void setup()
{

    Serial.begin(115200);
    Serial.println("Here/");
    Serial.println();

    // Set auto reconnect WiFi or network connection
#if defined(ESP32) || defined(ESP8266)
    WiFi.setAutoReconnect(true);
#endif

// Connect to WiFi or network
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    multi.addAP(WIFI_SSID, WIFI_PASSWORD);
    multi.run();
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

    Serial.print("Connecting to Wi-Fi");
    unsigned long ms = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
        if (millis() - ms > 10000)
            break;
#endif
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();


    // The WiFi credentials are required for Pico W
    // due to it does not have reconnect feature.
#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    GSheet.clearAP();
    GSheet.addAP(WIFI_SSID, WIFI_PASSWORD);
    // You can add many WiFi credentials here
#endif

    // Set the seconds to refresh the auth token before expire (60 to 3540, default is 300 seconds)
    GSheet.setPrerefreshSeconds(10 * 60);

    //Begin the access token generation for Google API authentication
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    // In case SD/SD_MMC storage file access, mount the SD/SD_MMC card.
    // SD_Card_Mounting(); // See src/GS_SDHelper.h

    // Or begin with the Service Account JSON file that uploaded to the Filesystem image or stored in SD memory card.
    // GSheet.begin("path/to/serviceaccount/json/file", esp_google_sheet_file_storage_type_flash /* or esp_google_sheet_file_storage_type_sd */);
}


void loop()
{
    //Call ready() repeatedly in loop for authentication checking and processing
    bool ready = GSheet.ready();

    if (lastEvent+5000<millis()) {
      taskComplete=false;
      lastEvent = millis();
    }


    if (ready && !taskComplete)
    {
        //For basic FirebaseJson usage example, see examples/FirebaseJson/Create_Edit_Parse/Create_Edit_Parse.ino

        //If you assign the spreadsheet id from your own spreadsheet,
        //you need to set share access to the Service Account's CLIENT_EMAIL

        FirebaseJson response;
        // Instead of using FirebaseJson for response, you can use String for response to the functions 
        // especially in low memory device that deserializing large JSON response may be failed as in ESP8266
        FirebaseJson valueRange;

        valueRange.add("majorDimension", "ROWS");
        valueRange.set("values/[0]/[0]", taskComplete);
        valueRange.set("values/[0]/[1]", millis());
        valueRange.set("values/[0]/[2]", "Jaywashere");

        bool success = GSheet.values.append(&response /* returned response */, "1ETj7hf3aBPsGEbcdDJ9S2CZbL4JCtGFxnURJ2OH3zw4" /* spreadsheet Id to append */, "Sheet1!A3" /* range to append */, &valueRange /* data range to append */);
        if (success)
            response.toString(Serial, true);
        else
            Serial.println(GSheet.errorReason());
        Serial.println();
        Serial.println(ESP.getFreeHeap());
    
        if (false) {
        Serial.println("Get spreadsheet values from range...");
        Serial.println("---------------------------------------------------------------");

        bool success = GSheet.values.get(&response /* returned response */, "1ETj7hf3aBPsGEbcdDJ9S2CZbL4JCtGFxnURJ2OH3zw4" /* spreadsheet Id to read */, "Sheet1!A1:B3" /* range to read */);
        response.toString(Serial, true);
        Serial.println();
        }

        taskComplete = true;
    }
}