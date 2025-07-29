#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "server.hpp"


// Constructor
WeatherInfoOptimized::WeatherInfoOptimized() {
    cache_index = 0;
    grid_coordinates_cached = false;
    cached_grid_x = 0;
    cached_grid_y = 0;
    memset(cached_grid_id, 0, sizeof(cached_grid_id));
    
    last_api_call_time = 0;
    total_api_calls = 0;
    failed_api_calls = 0;
    average_response_time = 0;
    
    // Initialize cache
    for (int i = 0; i < WEATHER_CACHE_SIZE; i++) {
        cache[i].valid = false;
        cache[i].timestamp = 0;
    }
    
    initWeather();
}

// Optimized updateWeather method
byte WeatherInfoOptimized::updateWeatherOptimized(uint16_t synctime) {
    //returns 3 if data is still fresh, 2 if too soon to retry, 1 if update was needed and successful, 0 if update failed

    // Check if update is needed
    if (this->lastUpdateT>0 && this->lastUpdateT + synctime > I.currentTime) {
        return 3; // Data is still fresh
    }
    
    // Check retry logic
    if ((uint32_t) this->lastUpdateAttempt>0 && this->lastUpdateAttempt + 600 > I.currentTime) {
        return 2; // Too soon to retry
    }

    this->lastUpdateAttempt = I.currentTime;
    
    SerialPrint("Weather update optimized starting...",true);

    // Try to load from cache first
    if (loadFromCache()) {
        SerialPrint("Grid data loaded from cache",true);
    }        else {
        fetchGridCoordinates();
    }


    // Initialize fresh data
    initWeather();
    
    uint32_t start_time = millis();
    bool success = false;


        // Sequential requests 
        success = fetchHourlyForecast() &&
                  fetchGridForecast() &&
                  fetchDailyForecast() &&
                  fetchSunriseSunset();
    
    if (success) {
        this->lastUpdateT = I.currentTime;

        saveToCache();
        
        // Update performance stats
        uint32_t response_time = millis() - start_time;
        average_response_time = (average_response_time + response_time) / 2;
        total_api_calls++;

        storeWeatherDataSD(); //store weather data to SD card

        SerialPrint(("Weather update completed in " + String(response_time) + " ms").c_str(),true);
    } else {
        failed_api_calls++;
        SerialPrint("Weather update failed for the " + (String) failed_api_calls + "th time",true);
    }

    return success?1:0; //1 if successful, 0 if failed, 2 if too soon to retry
}


// Helper function for grid coordinates fetching
bool WeatherInfoOptimized::fetchGridCoordinatesHelper() {
    char cbuf[100];
    snprintf(cbuf, 99, "https://api.weather.gov/points/%.4f,%.4f", this->lat, this->lon);
    String url = String(cbuf);
    
    JsonDocument doc;
    int httpCode;
    
    if (makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_BOX,ERROR_JSON_BOX)) {
        if (doc["properties"].is<JsonVariantConst>() && doc["properties"]["gridId"].is<JsonVariantConst>()) {
            strncpy(this->Grid_id, doc["properties"]["gridId"], sizeof(this->Grid_id) - 1);
            this->Grid_id[sizeof(this->Grid_id) - 1] = '\0';
            this->Grid_x = doc["properties"]["gridX"];
            this->Grid_y = doc["properties"]["gridY"];
            this->grid_coordinates_cached = true;
            updateGridCoordinatesCache();
            return true;
        } else {
            storeError("Grid coordinates Json had incorrect format", ERROR_JSON_BOX,true);
            this->lastUpdateError = I.currentTime;
        }
        
    } 
    return false;
}

// Optimized grid coordinates fetching with caching
bool WeatherInfoOptimized::fetchGridCoordinates() {
    if (isGridCoordinatesValid()) {
        return true; // Use cached coordinates
    }
    
    return retryWithBackoff("grid coordinates", std::bind(&WeatherInfoOptimized::fetchGridCoordinatesHelper, this));
}

// Optimized HTTP request method
bool WeatherInfoOptimized::makeSecureRequest(const String& url, JsonDocument& doc, int& httpCode, ERRORCODES HTTP, ERRORCODES JSON, const char* cert_path) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    HTTPClient http;
    WiFiClientSecure wfclient;
    String ca_cert = getCert(cert_path);
    
    wfclient.setCACert(ca_cert.c_str());
    http.useHTTP10(true);
    http.begin(wfclient, url.c_str());
    
    uint32_t start_time = millis();
    httpCode = http.GET();
    delay(1000);
    uint32_t response_time = millis() - start_time;
    

    bool success = false;


    if (httpCode < 400) {
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (!error) {
            success = true;
            // Update average response time
            if (average_response_time == 0) {
                average_response_time = response_time;
            } else {
                average_response_time = (average_response_time + response_time) / 2;
            }
        } else {
            storeError(enumErrorToName(JSON).c_str(),JSON,true);
            this->lastUpdateError = I.currentTime;
        }
    } else {
        storeError(enumErrorToName(HTTP).c_str(),HTTP,true);
        this->lastUpdateError = I.currentTime;
    }
    
    http.end();
    return success;
}

// Optimized data processing methods
bool WeatherInfoOptimized::processHourlyData(JsonObject& properties) {

    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    uint8_t cnt = 0;
        
    for (JsonObject properties_period : periodsArray) {

        if (cnt >= NUMWTHRDAYS * 24) break;
        
        String tmp = (String) properties_period["startTime"].as<const char*>();
        this->dT[cnt] = breakNOAATimestamp(tmp);
        
        // Optimized temperature processing
        double temp = (double)properties_period["temperature"];
        const char* tu = (const char*) properties_period["temperatureUnit"];
        if (strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }
        this->temperature[cnt] = (int8_t)temp;
        
        
        // Optimized other fields
        this->PoP[cnt] = (uint8_t)properties_period["probabilityOfPrecipitation"]["value"];
        this->dewPoint[cnt] = (int8_t)properties_period["dewpoint"]["value"];
        this->humidity[cnt] = (uint8_t)properties_period["relativeHumidity"]["value"];
        
        // Optimized wind speed parsing
        const char* ws = properties_period["windSpeed"].as<const char*>();
        String wind_str = String(ws);
        int mph_index = wind_str.indexOf(" mph");
        if (mph_index > 0) {
            this->windspeed[cnt] = (uint8_t)wind_str.substring(0, mph_index).toInt();
        }
        
        // Optimized weather ID processing
        const char* icon = properties_period["icon"].as<const char*>();
        this->weatherID[cnt] = breakIconLink(String(icon), this->dT[cnt], 0);
        
        cnt++;
    }

    if (cnt>0) return true;

    storeError("Could not parse Hourly JSON",ERROR_JSON_HOURLY,true);
    this->lastUpdateError = I.currentTime;
    return false;
}

bool WeatherInfoOptimized::processGridData(JsonObject& properties) {
    // Process wet bulb temperature


    JsonArray values = properties["wetBulbGlobeTemperature"]["values"].as<JsonArray>();
    uint16_t cnt = 0;
    for (JsonObject value : values) {
        if (cnt >= NUMWTHRDAYS * 24) break;
        
        String tmp = value["validTime"];
        uint32_t dt = breakNOAATimestamp(tmp);
        uint16_t i = getIndex(dt);
        
        while (cnt <= i && cnt < NUMWTHRDAYS * 24) {
            double tempC = (double)value["value"];
            this->wetBulbTemperature[cnt] = (int8_t)((tempC * 9.0 / 5.0) + 32.0);
            cnt++;
        }
    }
    
    if (cnt==0) {
        storeError("Could not parse Grid JSON",ERROR_JSON_GRID,true);
        this->lastUpdateError = I.currentTime;
        return false;
    }

    // Process precipitation data with optimized loops
    processPrecipitationData(properties, "quantitativePrecipitation", this->rainmm, this->flag_rain);
    processPrecipitationData(properties, "iceAccumulation", this->icemm, this->flag_ice);
    processPrecipitationData(properties, "snowfallAmount", this->snowmm, this->flag_snow);
    return true;
}

void WeatherInfoOptimized::processPrecipitationData(JsonObject& properties, const char* field_name, uint8_t* data_array, bool& flag) {
    uint16_t cnt = 0;
    
    JsonArray values = properties[field_name]["values"].as<JsonArray>();
    for (JsonObject value : values) {
        if (cnt >= NUMWTHRDAYS * 24) break;
        
        String tmp = value["validTime"];
        uint32_t dt = breakNOAATimestamp(tmp);
        uint16_t i = getIndex(dt);
        
        while (cnt <= i && cnt < NUMWTHRDAYS * 24) {
            data_array[cnt] = (uint8_t)((double)value["value"]);
            if (data_array[cnt] > 0) {
                flag = true;
            }
            cnt++;
        }
    }

    if (cnt==0) {
        String s = "[Minor] No " + (String) field_name + " values";
        storeError(s.c_str(),ERROR_JSON_GRID,true);    
        this->lastUpdateError = I.currentTime;
    }
    return;
}

bool WeatherInfoOptimized::processDailyData(JsonObject& properties) {
    uint16_t cnt = 0;
    JsonArray periodsArray = properties["periods"].as<JsonArray>();

    for (JsonObject properties_value : periodsArray) {
        if (cnt >= 14) break;
        
        String tmp = properties_value["startTime"];
        this->daily_dT[cnt] = breakNOAATimestamp(tmp);
        
        if (properties_value["isDaytime"]) {
            double temp = (double)properties_value["temperature"];
            const char* tu = properties_value["temperatureUnit"];
            if (strcmp(tu, "C") == 0) {
                temp = (temp * 9.0 / 5.0) + 32.0;
            }
            this->daily_tempMax[cnt] = (int8_t)temp;
            this->daily_tempMin[cnt] = -125;
        } else {
            this->daily_tempMax[cnt] = -125;
            double temp = (double)properties_value["temperature"];
            const char* tu = properties_value["temperatureUnit"];
            if (strcmp(tu, "C") == 0) {
                temp = (temp * 9.0 / 5.0) + 32.0;
            }
            this->daily_tempMin[cnt] = (int8_t)temp;
        }
        
        this->daily_PoP[cnt] = (uint8_t)((double)properties_value["probabilityOfPrecipitation"]["value"]);
        
        const char* icon = properties_value["icon"];
        this->daily_weatherID[cnt] = breakIconLink(String(icon), this->daily_dT[cnt], this->daily_dT[cnt] + 43200);
        
        cnt++;
    }

    if (cnt>0) {
        return true;
    }

    
    storeError("Could not parse Daily JSON",ERROR_JSON_DAILY,true);
    this->lastUpdateError = I.currentTime;
    return false;

}

// Caching methods
bool WeatherInfoOptimized::isGridCoordinatesValid() {
    return grid_coordinates_cached && Grid_x != 0 && Grid_y != 0;
}

void WeatherInfoOptimized::updateGridCoordinatesCache() {
    // Update cache with current grid coordinates
    cache[cache_index].grid_x = Grid_x;
    cache[cache_index].grid_y = Grid_y;
    strncpy(cache[cache_index].grid_id, Grid_id, sizeof(cache[cache_index].grid_id));
    cache[cache_index].timestamp = I.currentTime;
    cache[cache_index].valid = true;
}

bool WeatherInfoOptimized::loadFromCache() {
    // Check if we have valid cached data
    for (int i = 0; i < WEATHER_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].last_successful_update + 3600 > I.currentTime) {
            // Cache is still valid, restore grid coordinates
            Grid_x = cache[i].grid_x;
            Grid_y = cache[i].grid_y;
            strncpy(Grid_id, cache[i].grid_id, sizeof(Grid_id));
            grid_coordinates_cached = true;
            return true;
        }
    }
    return false;
}

void WeatherInfoOptimized::saveToCache() {
    cache[cache_index].last_successful_update = I.currentTime;
    cache_index = (cache_index + 1) % WEATHER_CACHE_SIZE;
}

// Error handling and retry logic
bool WeatherInfoOptimized::handleApiError(const String& operation, int httpCode) {

    #ifdef _DEBUG
        Serial.printf("API error in %s: HTTP %d\n", operation.c_str(), httpCode);
    #endif
    storeError(("API error: " + operation + " HTTP " + String(httpCode)).c_str());
    this->lastUpdateError = I.currentTime;
    return false;
}

bool WeatherInfoOptimized::retryWithBackoff(const String& operation, std::function<bool()> request_func) {
    for (int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
        if (request_func()) {
            return true;
        }
        
        if (attempt < MAX_RETRY_ATTEMPTS - 1) {
            uint32_t delay_ms = (3 * attempt) * 1000; // Exponential backoff: 1s, 2s, 4s
            delay(delay_ms);
        }
    }
    
    storeError(("Failed " + operation + " after " + String(MAX_RETRY_ATTEMPTS) + " attempts").c_str());
    this->lastUpdateError = I.currentTime;
    return false;
}

// Performance monitoring
void WeatherInfoOptimized::getPerformanceStats(uint32_t& total_calls, uint32_t& failed_calls, uint32_t& avg_response_time) {
    total_calls = this->total_api_calls;
    failed_calls = this->failed_api_calls;
    avg_response_time = this->average_response_time;
}

// Memory optimization
void WeatherInfoOptimized::optimizeMemoryUsage() {
    // Clear unused cache entries
    for (int i = 0; i < WEATHER_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].last_successful_update + 7200 < I.currentTime) {
            cache[i].valid = false;
        }
    }
}

size_t WeatherInfoOptimized::getMemoryUsage() {
    size_t base_size = sizeof(WeatherInfoOptimized);
    size_t cache_size = sizeof(cache);
    return base_size + cache_size;
}

// Implement remaining methods from original interface
bool WeatherInfoOptimized::fetchHourlyForecast() {
    return retryWithBackoff("hourly forecast", [this]() {
        JsonDocument doc;
        int httpCode;
        String url = getGrid(2);

        bool b1=false,b2 = false,b3=false;

        b1=makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_HOURLY,ERROR_JSON_HOURLY);
        if (b1) {
            b2 = doc["properties"].is<JsonVariantConst>();

            if (b2) {
                JsonObject tempobj = doc["properties"].as<JsonObject>();
                b3 =  processHourlyData(tempobj);
            } else {
                storeError("Hourly Json had incorrect format", ERROR_JSON_HOURLY,true);
                this->lastUpdateError = I.currentTime;
            }
        }
        return b1 && b2 && b3;

/*        return makeSecureRequest(url, doc, httpCode) && 
               doc["properties"].is<JsonVariantConst>() &&
               processHourlyData(tempobj);
               */
    });
}

bool WeatherInfoOptimized::fetchGridForecast() {
    return retryWithBackoff("grid forecast", [this]() {
        JsonDocument doc;
        int httpCode;
        String url = getGrid(0);
        
        bool b1=false,b2 = false,b3=false;
        b1=makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_GRID,ERROR_JSON_GRID);
        if (b1) {
            b2 = doc["properties"].is<JsonVariantConst>();

            if (b2) {
                JsonObject tempobj = doc["properties"].as<JsonObject>();
                b3 =  processGridData(tempobj);
            } else {
                storeError("Grid Json had incorrect format", ERROR_JSON_GRID,true);
                this->lastUpdateError = I.currentTime;
            }
        }
        return b1 && b2 && b3;


/*        return makeSecureRequest(url, doc, httpCode) && 
               doc["properties"].is<JsonVariantConst>() &&
               processGridData(tempobj);
               */
    });
    
}

bool WeatherInfoOptimized::fetchDailyForecast() {
    return retryWithBackoff("daily forecast", [this]() {
        JsonDocument doc;
        int httpCode;        
        String url = getGrid(1);
        bool b1=false,b2 = false,b3=false;
        b1=makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_DAILY,ERROR_JSON_DAILY);
        if (b1) {
            b2 = doc["properties"].is<JsonVariantConst>();
            if (b2) {
                JsonObject tempobj = doc["properties"].as<JsonObject>();
                b3 =  processDailyData(tempobj);
            } else {
                storeError("Daily Json had incorrect format", ERROR_JSON_DAILY,true);
                this->lastUpdateError = I.currentTime;
            }
        }
        return b1 && b2 && b3;
    });
}

bool WeatherInfoOptimized::fetchSunriseSunset() {
    char cbuf[100];
    snprintf(cbuf, 99, "https://api.sunrisesunset.io/json?lat=%f&lng=%f", this->lat, this->lon);
    String url = String(cbuf);
    
    JsonDocument doc;
    int httpCode;
    
    if (makeSecureRequest(url, doc, httpCode, ERROR_HTTPFAIL_SUNRISE, ERROR_JSON_SUNRISE, "/Certificates/sunrisesunset.crt")) {
        if (doc["results"].is<JsonVariantConst>()) {
            JsonObject results = doc["results"];
            const char* srise = results["sunrise"];
            const char* sset = results["sunset"];
            const char* sdat = results["date"];

            
            String sun = String(sdat) + " " + String(srise);
            this->sunrise = convertStrTime(sun);
            
            sun = String(sdat) + " " + String(sset);
            this->sunset = convertStrTime(sun);
            
            return true;
        } else {
            storeError("Sunrise IO Json had incorrect format", ERROR_JSON_SUNRISE,true);
            this->lastUpdateError = I.currentTime;
        }
    }
    return false;
}

// Forward compatibility - implement original interface methods
bool WeatherInfoOptimized::updateWeather(uint16_t synctime) {
    return updateWeatherOptimized(synctime);
}

// Implement all the original getter methods (same as original Weather.cpp)
uint8_t WeatherInfoOptimized::getIndex(time_t dT) {
    if (dT == 0) dT = I.currentTime;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if  (this->dT[j] == dT) return j;
        if (this->dT[j] > dT) {
            if (j>0 ) j--;
            return j;
        }
    }
    return 255;
}

int8_t WeatherInfoOptimized::getTemperature(uint32_t dt, bool wetbulb, bool asindex) {
    if (asindex) {
        if (wetbulb) return this->wetBulbTemperature[dt];
        
        return this->temperature[dt];
    }
    
    if (dt==0) dt = I.currentTime;
    uint16_t i = getIndex(dt);
    if (i==255) return -120;
    
    if (wetbulb) return this->wetBulbTemperature[i];
    else return this->temperature[i];
}

uint8_t WeatherInfoOptimized::getHumidity(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->humidity[i];
}

int16_t WeatherInfoOptimized::getWeatherID(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->weatherID[i];
}

uint8_t WeatherInfoOptimized::getPoP(uint32_t dt) {
    if (dt==0) {
        double totalPOP=1;
        for (uint16_t j=0;j<24;j++) totalPOP *= (1 - (this->PoP[j]/100));
        return (uint8_t) ((1- totalPOP)*100);
    }
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->PoP[i];
}

uint16_t WeatherInfoOptimized::getRain(uint32_t dt) {
    if (dt==0) {
        uint16_t totalrain = 0;
        for (uint16_t j=0;j<24;j++) totalrain+=this->rainmm[j];
        return totalrain;
    }
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->rainmm[i];
}

uint16_t WeatherInfoOptimized::getSnow(uint32_t dt) {
    if (dt==0) {
        uint16_t total = 0;
        for (uint16_t j=0;j<24;j++) total+=this->snowmm[j];
        return total;
    }
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->snowmm[i];
}

uint16_t WeatherInfoOptimized::getIce(uint32_t dt) {
    if (dt==0) {
        uint16_t total = 0;
        for (uint16_t j=0;j<24;j++) total+=this->icemm[j];
        return total;
    }
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->icemm[i];
}

int8_t WeatherInfoOptimized::getDewPoint(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    uint16_t i = getIndex(dt);
    if (i==255) return -120;
    return this->dewPoint[i];
}

uint8_t WeatherInfoOptimized::getWindSpeed(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    uint16_t i = getIndex(dt);
    if (i==255) return 255;
    return this->windspeed[i];
}

time_t WeatherInfoOptimized::breakNOAATimestamp(String tm) {
    uint8_t y = (byte) ((int) tm.substring(0,4).toInt()-2000);
    uint8_t m = (byte) tm.substring(5,7).toInt();
    uint8_t d = (byte) tm.substring(8,10).toInt();
    uint8_t h = (byte) tm.substring(11,13).toInt();
    uint8_t n = (byte) tm.substring(14,16).toInt();
    uint8_t s = (byte) tm.substring(17,19).toInt();
    return makeUnixTime(y,m,d,h,n,s);
}

int16_t WeatherInfoOptimized::breakIconLink(String icon, uint32_t starttime, uint32_t endtime) {
    // Same implementation as original Weather.cpp
    if (icon.indexOf("hurricane",0)>-1) return 504;
    if (icon.indexOf("tropical_storm",0)>-1) return 504;
    if (icon.indexOf("blizzard",0)>-1) return 603;
    
    if (icon.indexOf("snow",0)>-1) {
        if (icon.indexOf("rain_snow",0)>-1) return 611;
        if (icon.indexOf("snow_sleet",0)>-1) return 611;
        if (icon.indexOf("snow_fzra",0)>-1) return 611;
        
        if (endtime <= starttime + 3600 ) {
            byte total = this->getSnow(starttime);
            if (total<13) return 600;
            if (total<38) return 601;
            return 602;
        } else {
            byte total = this->getDailySnow(starttime,endtime);
            if (total<25) return 600;
            if (total<100) return 601;
            return 602;
        }
        return 600;
    }
    
    if (icon.indexOf("fzra",0)>-1) return 611;
    if (icon.indexOf("sleet",0)>-1) return 301;
    
    if (icon.indexOf("tsra",0)>-1) {
        if (endtime <= starttime + 3600 ) {
            byte total = this->getRain(starttime);
            if (total<4) return 200;
            return 201;
        } else {
            byte total = this->getDailyRain(starttime,endtime);
            if (total<5) return 200;
            return 201;
        }
        if (icon.indexOf("tsra_hi",0)>-1) return 201;
        return 200;
    }
    
    if (icon.indexOf("rain",0)>-1) {
        if (icon.indexOf("rain_sleet",0)>-1) return 611;
        if (icon.indexOf("rain_fzra",0)>-1) return 611;
        
        if (endtime <= starttime + 3600 ) {
            byte total = this->getRain(starttime);
            if (total<1) return 301;
            if (total<2) return 500;
            if (total<4) return 501;
            if (total<6) return 502;
            return 503;
        } else {
            byte total = this->getDailyRain(starttime,endtime);
            if (total<2) return 301;
            if (total<5) return 500;
            if (total<15) return 501;
            if (total<25) return 502;
            return 503;
        }
        return 500;
    }
    
    if (icon.indexOf("dust",0)>-1) return 761;
    if (icon.indexOf("hot",0)>-1) return 800;
    if (icon.indexOf("cold",0)>-1) return 701;
    
    if (icon.indexOf("ovc",0)>-1) return 804;
    if (icon.indexOf("bkn",0)>-1) return 803;
    if (icon.indexOf("sct",0)>-1) return 801;
    if (icon.indexOf("few",0)>-1) return 801;
    if (icon.indexOf("skc",0)>-1) return 800;
    if (icon.indexOf("fog",0)>-1) return 741;
    
    return 999;
}

bool WeatherInfoOptimized::initWeather() {
    for (uint16_t i=0;i<NUMWTHRDAYS*24;i++) {
        this->dT[i] = 0;
        this->temperature[i] = 0;
        this->humidity[i] = 0;
        this->weatherID[i] = 0;
        this->PoP[i] = 0;
        this->dewPoint[i] = 0;
        this->windspeed[i] = 0;
        this->wetBulbTemperature[i] = 0;
        this->rainmm[i] = 0;
        this->icemm[i] = 0;
        this->snowmm[i] = 0;
    }
    
    for (uint16_t i=0;i<14;i++) {
        this->daily_dT[i] = 0;
        this->daily_tempMax[i] = 0;
        this->daily_tempMin[i] = 0;
        this->daily_weatherID[i] = 0;
        this->daily_PoP[i] = 0;
    }
    
    this->sunrise = 0;
    this->sunset = 0;
    
    this->lat = 42.3018906;
    this->lon = -71.2981767;

    this->lastUpdateT = 0;
    this->lastUpdateAttempt = 0;

    return true;
}

// Implement remaining daily methods
uint16_t WeatherInfoOptimized::getDailyRain(uint8_t daysfromnow) {
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);
    uint16_t totalrain = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) {
            totalrain+=this->rainmm[j];
        }
    }
    return totalrain;
}

uint16_t WeatherInfoOptimized::getDailyRain(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= starttime && this->dT[j] < endtime) {
            total+=this->rainmm[j];
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailySnow(uint8_t daysfromnow) {
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);
    uint16_t total = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) {
            total+=this->snowmm[j];
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailySnow(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= starttime && this->dT[j] < endtime) {
            total+=this->snowmm[j];
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailyIce(uint8_t daysfromnow) {
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);
    uint16_t total = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) {
            total+=this->icemm[j];
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailyIce(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    for (uint16_t j=0;j<NUMWTHRDAYS*24;j++) {
        if (this->dT[j] >= starttime && this->dT[j] < endtime) {
            total+=this->icemm[j];
        }
    }
    return total;
}

uint8_t WeatherInfoOptimized::getDailyPoP(uint8_t intsfromnow, bool indays) {
    if (indays) {
        if (this->daily_tempMax[0] == -125) {
            if (intsfromnow>0) intsfromnow = (intsfromnow*2)-1;
        } else {
            intsfromnow = (intsfromnow*2);
        }
    }
    return this->daily_PoP[intsfromnow];
}

int16_t WeatherInfoOptimized::getDailyWeatherID(uint8_t intsfromnow, bool indays) {
    if (indays) {
        if (this->daily_tempMax[0] == -125) {
            if (intsfromnow>0) intsfromnow = (intsfromnow*2)-1;
        } else {
            intsfromnow = (intsfromnow*2);
        }
    }
    return this->daily_weatherID[intsfromnow];
}

void WeatherInfoOptimized::getDailyTemp(uint8_t daysfromnow, int8_t* temp) {
    int8_t iDay,iNight;
    
    if (this->daily_tempMax[0] == -125) {
        if (hour()>12) {
            iDay = daysfromnow*2-1;
            iNight = daysfromnow*2;
        } else {
            iDay = daysfromnow*2+1;
            iNight = daysfromnow*2+2;
        }
    } else {
        iDay = daysfromnow*2;
        iNight = daysfromnow*2+1;
    }
    
    if (iDay<0) {
        temp[0] = this->getTemperature();
    } else {
        temp[0] = this->daily_tempMax[iDay];
    }
    
    temp[1] = this->daily_tempMin[iNight];
}

String WeatherInfoOptimized::getGrid(uint8_t fc) {
    if (fc==1) return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y + (String) "/forecast";
    if (fc==2) return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y + (String) "/forecast/hourly";
    return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y;
}

// Placeholder methods for compatibility
uint32_t WeatherInfoOptimized::nextPrecip() { return 0; }
uint32_t WeatherInfoOptimized::nextRain() { return 0; }
uint32_t WeatherInfoOptimized::nextSnow() { return 0; }

// Cache management methods
bool WeatherInfoOptimized::clearCache() {
    for (int i = 0; i < WEATHER_CACHE_SIZE; i++) {
        cache[i].valid = false;
    }
    grid_coordinates_cached = false;
    return true;
}

bool WeatherInfoOptimized::isCacheValid() {
    for (int i = 0; i < WEATHER_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].last_successful_update + 3600 > I.currentTime) {
            return true;
        }
    }
    return false;
}

// Complete address to coordinates conversion using US Census Bureau Geocoding API
bool WeatherInfoOptimized::getCoordinatesFromAddress(const String& street, const String& city, const String& state, const String& zipCode, double& latitude, double& longitude) {
    // Validate ZIP code format (5 digits)
    if (zipCode.length() != 5) {
        SerialPrint("Invalid ZIP code format. Must be 5 digits.", true);
        return false;
    }
    
    for (int i = 0; i < 5; i++) {
        if (!isdigit(zipCode.charAt(i))) {
            SerialPrint("Invalid ZIP code format. Must contain only digits.", true);
            return false;
        }
    }
    
    // Validate state format (2 letters)
    if (state.length() != 2) {
        SerialPrint("Invalid state format. Must be 2 letters.", true);
        return false;
    }
    
    // Build the URL for the Census Bureau Geocoding API
    String url = "https://geocoding.geo.census.gov/geocoder/locations/address?";
    url += "street=" + urlEncode(street);
    url += "&city=" + urlEncode(city);
    url += "&state=" + urlEncode(state);
    url += "&zip=" + zipCode;
    url += "&benchmark=Public_AR_Current&format=json";
    
    JsonDocument doc;
    int httpCode;
    
    SerialPrint(("Fetching coordinates for address: " + street + ", " + city + ", " + state + " " + zipCode).c_str(), true);
    SerialPrint(("API URL: " + url).c_str(), true);
    
    // Make HTTP request to Census Geocoding API
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000); // 15 second timeout for geocoding
    
    httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        
        SerialPrint("Received response from Census Geocoding API", true);
        
        // Parse JSON response
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            SerialPrint("Failed to parse JSON response from Census Geocoding API", true);
            SerialPrint(("JSON Error: " + String(error.c_str())).c_str(), true);
            storeError("Failed to parse JSON response from Census Geocoding API", ERROR_JSON_GEOCODING,true);
            return false;
        }
        
        // Check if we have address matches
        if (doc["result"]["addressMatches"].is<JsonArray>()) {
            JsonArray addressMatches = doc["result"]["addressMatches"];
            
            if (addressMatches.size() > 0) {
                // Get the first (best) match
                JsonObject match = addressMatches[0];
                
                if (match["coordinates"]["x"].is<double>() && match["coordinates"]["y"].is<double>()) {
                    longitude = match["coordinates"]["x"].as<double>();
                    latitude = match["coordinates"]["y"].as<double>();
                    
                    SerialPrint(("Coordinates found: " + String(latitude, 6) + ", " + String(longitude, 6)).c_str(), true);
                    
                    // Log the matched address for verification
                    if (match["matchedAddress"].is<String>()) {
                        String matchedAddress = match["matchedAddress"].as<String>();
                        SerialPrint(("Matched Address: " + matchedAddress).c_str(), true);
                    }
                    
                    return true;
                }
            }
        }
        
        SerialPrint("No address matches found in the response", true);
    } else {
        SerialPrint(("HTTP request failed with code: " + String(httpCode)).c_str(), true);
        http.end();
    }
    
    // Fallback to ZIP code only method
    SerialPrint("Falling back to ZIP code only method", true);
    return getCoordinatesFromZipCodeFallback(zipCode, latitude, longitude);
}

// ZIP code to coordinates conversion using US Census Bureau Geocoding API (legacy function)
bool WeatherInfoOptimized::getCoordinatesFromZipCode(const String& zipCode, double& latitude, double& longitude) {
    
    // Validate ZIP code format (5 digits)
    if (zipCode.length() != 5) {
        SerialPrint("Invalid ZIP code format. Must be 5 digits.", true);
        storeError("Invalid ZIP code format. Must be 5 digits.", ERROR_JSON_GEOCODING,true);
        return false;
    }
    
    for (int i = 0; i < 5; i++) {
        if (!isdigit(zipCode.charAt(i))) {
            SerialPrint("Invalid ZIP code format. Must contain only digits.", true);
            storeError("Invalid ZIP code format. Must contain only digits.", ERROR_JSON_GEOCODING,true);
            return false;
        }
    }
    
    // For now, we'll use a default address structure since we only have ZIP code
    // In a full implementation, you would collect street, city, state from the user
    String street = "1 Main St";  // Default street address
    String city = "Unknown";      // Default city
    String state = "MA";          // Default state (you might want to make this configurable)
    
    // Build the URL for the Census Bureau Geocoding API
    String url = "https://geocoding.geo.census.gov/geocoder/locations/address?";
    url += "street=" + urlEncode(street);
    url += "&city=" + urlEncode(city);
    url += "&state=" + urlEncode(state);
    url += "&zip=" + zipCode;
    url += "&benchmark=Public_AR_Current&format=json";
    
    JsonDocument doc;
    int httpCode;
    
    SerialPrint(("Fetching coordinates for ZIP code: " + zipCode).c_str(), true);
    SerialPrint(("API URL: " + url).c_str(), true);
    
    // Make HTTP request to Census Geocoding API
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000); // 15 second timeout for geocoding
    
    httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        
        SerialPrint("Received response from Census Geocoding API", true);
        
        // Parse JSON response
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            SerialPrint("Failed to parse JSON response from Census Geocoding API", true);
            SerialPrint(("JSON Error: " + String(error.c_str())).c_str(), true);
            storeError("Failed to parse JSON response from Census Geocoding API", ERROR_JSON_GEOCODING,true);
            return false;
        }
        
        // Check if we have address matches
        if (doc["result"]["addressMatches"].is<JsonArray>()) {
            JsonArray addressMatches = doc["result"]["addressMatches"];
            
            if (addressMatches.size() > 0) {
                // Get the first (best) match
                JsonObject match = addressMatches[0];
                
                if (match["coordinates"]["x"].is<double>() && match["coordinates"]["y"].is<double>()) {
                    longitude = match["coordinates"]["x"].as<double>();
                    latitude = match["coordinates"]["y"].as<double>();
                    
                    SerialPrint(("Coordinates found: " + String(latitude, 6) + ", " + String(longitude, 6)).c_str(), true);
                    
                    // Log the matched address for verification
                    if (match["matchedAddress"].is<String>()) {
                        String matchedAddress = match["matchedAddress"].as<String>();
                        SerialPrint(("Matched Address: " + matchedAddress).c_str(), true);
                    }
                    
                    return true;
                }
            }
        }
        storeError("No address matches found in the response", ERROR_JSON_GEOCODING,true);
        SerialPrint("No address matches found in the response", true);

    } else {
        SerialPrint(("HTTP request failed with code: " + String(httpCode)).c_str(), true);

        String error = "HTTP request failed with code: " + String(httpCode);
        storeError(error.c_str(), ERROR_JSON_GEOCODING,true);
        http.end();
    }
    
    // Fallback to ZIP code only method
    SerialPrint("Falling back to ZIP code only method", true);
    return getCoordinatesFromZipCodeFallback(zipCode, latitude, longitude);
}

// Helper function to URL encode strings
String WeatherInfoOptimized::urlEncode(const String& str) {
    String encoded = "";
    for (unsigned int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            encoded += '%';
            if (c < 16) {
                encoded += '0';
            }
            encoded += String(c, HEX);
        }
    }
    return encoded;
}

// Fallback method using a simple geocoding service
bool WeatherInfoOptimized::getCoordinatesFromZipCodeFallback(const String& zipCode, double& latitude, double& longitude) {
    // Use a simple geocoding service (example with a free API)
    // Note: This is a simplified approach. In production, you might want to use
    // a more reliable service like Google Geocoding API (requires API key)
    
    String url = "http://api.zippopotam.us/US/" + zipCode;
    
    JsonDocument doc;
    int httpCode;
    
    SerialPrint(("Using fallback geocoding service for ZIP: " + zipCode).c_str(), true);
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    
    httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();
        
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            SerialPrint("Failed to parse JSON response from geocoding service", true);
            storeError("Failed to parse JSON response from geocoding service", ERROR_JSON_GEOCODING,true);
            this->lastUpdateError = I.currentTime;
            return false;
        }
        
        // Extract coordinates from the response
        if (doc["places"][0]["latitude"].is<String>() && doc["places"][0]["longitude"].is<String>()) {
            latitude = doc["places"][0]["latitude"].as<double>();
            longitude = doc["places"][0]["longitude"].as<double>();
            
            SerialPrint(("Coordinates found: " + String(latitude, 6) + ", " + String(longitude, 6)).c_str(), true);
            return true;
        }
    } else {
        SerialPrint(("Fallback geocoding failed with HTTP code: " + String(httpCode)).c_str(), true);
        String error = "Fallback geocoding failed with HTTP code: " + String(httpCode);
        storeError(error.c_str(), ERROR_JSON_GEOCODING,true);
        http.end();
    }
    
    // If all methods fail, return false
    SerialPrint("All geocoding methods failed for ZIP code: " + zipCode, true);
    return false;
} 
