#include "globals.hpp"

#ifdef _USEWEATHER

#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "server.hpp"

#ifdef _USESDCARD

#include "SDCard.hpp"
#endif


// determine noaa timestamp intervals


TimeInterval WeatherInfoOptimized::parseNOAAInterval(String tm) {
    TimeInterval ti = {0, 3600}; // Default to 1 hour duration
    
    int slashIndex = tm.indexOf('/');
    
    if (slashIndex != -1) {
        String durationStr = tm.substring(slashIndex + 1); // e.g., "PT6H" or "P1D"
        tm = tm.substring(0, slashIndex); // The ISO8601 part
        
        // Simple duration parser for NWS formats
        if (durationStr.startsWith("PT")) {
            if (durationStr.endsWith("H")) {
                ti.duration = durationStr.substring(2, durationStr.length() - 1).toInt() * 3600;
            } else if (durationStr.endsWith("M")) {
                ti.duration = durationStr.substring(2, durationStr.length() - 1).toInt() * 60;
            }
        } else if (durationStr.startsWith("P") && durationStr.endsWith("D")) {
            ti.duration = durationStr.substring(1, durationStr.length() - 1).toInt() * 86400;
        }        
    }

    // Parse ISO8601 date/time (required)
    ti.start = iso8601ToUnix(tm, true);

    return ti;
}



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
                  fetchWeatherAlerts() &&
                  fetchSunriseSunset();
    
    if (success) {
        this->lastUpdateT = I.currentTime;

        saveToCache();
        
        // Update performance stats
        uint32_t response_time = millis() - start_time;
        average_response_time = (average_response_time + response_time) / 2;
        total_api_calls++;

        #ifdef _USESDCARD
        storeWeatherDataSD(); //store weather data to SD card
        #endif

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
    snprintf(cbuf, 99, "https://api.weather.gov/points/%.4f,%.4f", Prefs.LATITUDE, Prefs.LONGITUDE);
    String url = String(cbuf);
    
    JsonDocument doc;
    int16_t httpCode;
    String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";


    if (makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_BOX,ERROR_JSON_BOX,extraHeaders)) {
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


// Optimized HTTP request method: uses Server_SecureMessageEx with stream (JsonDocument*) to avoid getString() RAM spike
bool WeatherInfoOptimized::makeSecureRequest(String& url, JsonDocument& doc, int16_t& httpCode, ERRORCODES HTTP, ERRORCODES JSON, String& extraHeaders, const char* cert_path, const JsonDocument* filter) {
    if (CheckWifiStatus(false) != 1) {
        return false;
    }

    String payload_dummy;
    String method = "GET";
    String contentType = "";
    String body = "";
    String cacert = String(cert_path);

    uint32_t start_time = millis();
    bool success = Server_SecureMessageEx(url, payload_dummy, httpCode, cacert, method, contentType, body, extraHeaders, &doc, filter);
    uint32_t response_time = millis() - start_time;

    if (success) {
        if (average_response_time == 0) {
            average_response_time = response_time;
        } else {
            average_response_time = (average_response_time + response_time) / 2;
        }
    } else {
        this->lastUpdateError = I.currentTime;
        if (httpCode >= 400) {
            storeError(enumErrorToName(HTTP).c_str(), HTTP, true);
        } else {
            storeError(enumErrorToName(JSON).c_str(), JSON, true);
        }
    }
    return success;
}

// Optimized data processing methods
bool WeatherInfoOptimized::processHourlyData(JsonObject& properties) {
    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    uint32_t midnightToday = makeUnixTime(year()-2000, month(), day(), 0, 0, 0); //midnight local time, default behavior is to return local time
    uint8_t processedCount = 0;
        
    for (JsonObject period : periodsArray) {        
        String tmp = period["startTime"].as<const char*>();
        TimeInterval ti = parseNOAAInterval(tmp);
        

        int16_t i = getIndex(ti.start);
        if (i < 0) continue;
        
        this->dT[i] = ti.start;
        
        // Temperature processing
        double temp = (double)period["temperature"];
        const char* tu = period["temperatureUnit"];
        if (tu && strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }
        this->temperature[i] = (int8_t)temp;
        
        // Other fields using the same anchored index
        this->PoP[i] = (uint8_t)period["probabilityOfPrecipitation"]["value"];
        this->dewPoint[i] = (int8_t)period["dewpoint"]["value"];
        this->humidity[i] = (uint8_t)period["relativeHumidity"]["value"];
        
        // Wind speed parsing
        const char* ws = period["windSpeed"].as<const char*>();
        String wind_str = String(ws);
        int mph_index = wind_str.indexOf(" mph");
        if (mph_index > 0) {
            this->windspeed[i] = (int16_t)wind_str.substring(0, mph_index).toInt();
        }
        
        // Icon / Weather ID
        const char* icon = period["icon"].as<const char*>();
        this->weatherID[i] = breakIconLink(String(icon), ti);
        
        processedCount++;
    }

    if (processedCount > 0) return true;

    storeError("Could not parse Hourly JSON", ERROR_JSON_HOURLY, true);
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
        TimeInterval ti = parseNOAAInterval(tmp);
        int16_t i = getIndex(ti.start);
        if (i < 0) continue;
        
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

void WeatherInfoOptimized::processPrecipitationData(JsonObject& properties, const char* field_name, int16_t* data_array, bool& flag) {
    // Reset the 168-hour array (7 days * 24 hours)
    memset(data_array, 0, NUMWTHRDAYS * 24); 

    JsonArray values = properties[field_name]["values"].as<JsonArray>();

    for (JsonObject value : values) {
        TimeInterval ti = parseNOAAInterval(value["validTime"].as<String>());
        if (ti.start == 0) continue;

        float totalValue = value["value"]; // Total mm for the whole duration
        if (totalValue <= 0) continue;
        
        flag = true;

        // Calculate how many hours this record covers
        int hoursInInterval = ti.duration / 3600;
        if (hoursInInterval < 1) hoursInInterval = 1;

        // Hourly rate (mm per hour)
        float hourlyRate = totalValue / (float)hoursInInterval;

        // Populate each hour in the cache
        for (int h = 0; h < hoursInInterval; h++) {
            time_t currentHour = ti.start + (h * 3600);
            int16_t idx = getIndex(currentHour);
            
            if (idx >= 0 && idx < NUMWTHRDAYS * 24) {
                // If NWS sends overlapping intervals, we take the max rate 
                // or you could add them, but assignment is safer for grid data.
                data_array[idx] = (int16_t)hourlyRate;
            }
        }
    }
}


bool WeatherInfoOptimized::processDailyData(JsonObject& properties) {
    // Reset arrays to your invalid temperature constant
    memset(this->daily_tempMax, -120, sizeof(this->daily_tempMax));
    memset(this->daily_tempMin, -120, sizeof(this->daily_tempMin));

    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    
    // Get current day start (Midnight)
    uint32_t midnightToday = makeUnixTime(year()-2000, month(), day(), 0, 0, 0); //this is local time unless asLocalTime is false

    for (JsonObject period : periodsArray) {
        String startStr = period["startTime"].as<const char*>();
        TimeInterval ti = parseNOAAInterval(startStr);
        // FIX: Subtract 6 hours (21600s) before calculating dayOffset.
        // This ensures "Saturday Night" (starts 6pm) groups with "Saturday Day".
        int dayOffset = (ti.start - 21600 - midnightToday) / 86400;
        
        // Safety: If it's early morning and we subtracted 6 hours, 
        // dayOffset might be -1. Clamp it to 0.
        if (dayOffset < 0) dayOffset = 0;
        if (dayOffset >= 14) continue;

        double temp = (double)period["temperature"];
        const char* tu = period["temperatureUnit"];
        if (tu && strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }

        if (period["isDaytime"]) {
            // It's a High temp for that day
            this->daily_tempMax[dayOffset] = (int8_t)temp;
            
            // Daytime data is usually the primary for the daily summary
            this->daily_PoP[dayOffset] = (uint8_t)period["probabilityOfPrecipitation"]["value"];
            const char* icon = period["icon"];
            this->daily_weatherID[dayOffset] = breakIconLink(String(icon), ti);
        } else {
            // It's a Low temp for that day
            this->daily_tempMin[dayOffset] = (int8_t)temp;
            
            // If Max is still -120, it means it's already evening and "Today's" High 
            // wasn't sent. Use Night's PoP/Icon as the fallback representative.
            if (this->daily_tempMax[dayOffset] == -120) { 
                this->daily_PoP[dayOffset] = (uint8_t)period["probabilityOfPrecipitation"]["value"];
                const char* icon = period["icon"];
                this->daily_weatherID[dayOffset] = breakIconLink(String(icon), ti);
            }
        }
    }
    return true;
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

        int16_t httpCode;
        String url = getGrid(2);

        bool b1=false,b2 = false,b3=false;
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";

        JsonDocument filter;
        filter["properties"]["periods"][0]["startTime"] = true;
        filter["properties"]["periods"][0]["temperature"] = true;
        filter["properties"]["periods"][0]["temperatureUnit"] = true;
        filter["properties"]["periods"][0]["probabilityOfPrecipitation"]["value"] = true;
        filter["properties"]["periods"][0]["dewpoint"]["value"] = true;
        filter["properties"]["periods"][0]["relativeHumidity"]["value"] = true;
        filter["properties"]["periods"][0]["windSpeed"] = true;
        filter["properties"]["periods"][0]["icon"] = true;


        b1=makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_HOURLY,ERROR_JSON_HOURLY,extraHeaders, "", &filter);
        if (b1) {
            // Check specifically for the properties object
            JsonObject props = doc["properties"];
            if (!props.isNull()) {
                return processHourlyData(props);
            } else {
                SerialPrint("Hourly properties missing or doc too small", true);
            }
            
        } else {
            SerialPrint("Hourly request failed",true);
            SerialPrint("HTTP Code: " + String(httpCode),true);
            storeError("Hourly request failed", ERROR_HTTPFAIL_HOURLY,true);
            this->lastUpdateError = I.currentTime;
        }
        return b1 && b2 && b3;

    });
}

bool WeatherInfoOptimized::fetchGridForecast() {
    return retryWithBackoff("grid forecast", [this]() {

        JsonDocument doc;

        int16_t httpCode;
        String url = getGrid(0);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";

        // Filter can stay in Stack/Internal RAM as it is very small
        JsonDocument filter;
        JsonObject props = filter["properties"];
        props["wetBulbGlobeTemperature"]["values"][0]["validTime"] = true;
        props["wetBulbGlobeTemperature"]["values"][0]["value"] = true;
        props["quantitativePrecipitation"]["values"][0]["validTime"] = true;
        props["quantitativePrecipitation"]["values"][0]["value"] = true;
        props["iceAccumulation"]["values"][0]["validTime"] = true;
        props["iceAccumulation"]["values"][0]["value"] = true;
        props["snowfallAmount"]["values"][0]["validTime"] = true;
        props["snowfallAmount"]["values"][0]["value"] = true;

        bool b1 = makeSecureRequest(url, doc, httpCode, ERROR_HTTPFAIL_GRID, ERROR_JSON_GRID, extraHeaders, "", &filter);
        
        if (b1) {
            // ArduinoJson 7 returns a reference to the root object
            if (doc.containsKey("properties")) {
                JsonObject tempobj = doc["properties"];
                return processGridData(tempobj);
            } else {
                SerialPrint("Grid Json format incorrect or doc failed to grow in PSRAM", true);
                SerialPrint("Doc memory usage: " + String(doc.memoryUsage()) + " bytes", true);
                this->lastUpdateError = I.currentTime;
            }
        }
        return false;
    });
}

bool WeatherInfoOptimized::fetchDailyForecast() {
    return retryWithBackoff("daily forecast", [this]() {
        JsonDocument doc;
        int16_t httpCode;        
        String url = getGrid(1);
        bool b1=false,b2 = false,b3=false;
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";

       
        JsonDocument filter;
        filter["properties"]["periods"][0]["startTime"] = true;
        filter["properties"]["periods"][0]["isDaytime"] = true;
        filter["properties"]["periods"][0]["temperature"] = true;
        filter["properties"]["periods"][0]["temperatureUnit"] = true;
        filter["properties"]["periods"][0]["probabilityOfPrecipitation"]["value"] = true;
        filter["properties"]["periods"][0]["icon"] = true;

        b1=makeSecureRequest(url, doc, httpCode,ERROR_HTTPFAIL_DAILY,ERROR_JSON_DAILY,extraHeaders, "", &filter);
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
    snprintf(cbuf, 99, "https://api.sunrisesunset.io/json?lat=%f&lng=%f", Prefs.LATITUDE, Prefs.LONGITUDE);
    String url = String(cbuf);
    
    JsonDocument doc;
    int16_t httpCode;
    
    String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";

    //is cert_path is specified then use specific certificate, otherwise use the ESP-IDF certificate bundle
    //if (makeSecureRequest(url, doc, httpCode, ERROR_HTTPFAIL_SUNRISE, ERROR_JSON_SUNRISE, "/Certificates/sunrisesunset.crt")) {
    if (makeSecureRequest(url, doc, httpCode, ERROR_HTTPFAIL_SUNRISE, ERROR_JSON_SUNRISE,extraHeaders)) {
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

// get the index to the array based on the time (all times are local)
int16_t WeatherInfoOptimized::getIndex(time_t targetTime) {
    if (targetTime == 0) targetTime = I.currentTime;

    // Midnight (year/month/day from global clock are in local not utc)
    uint32_t midnightLocal = makeUnixTime(year()-2000, month(), day(), 0, 0, 0, true);

    // Hour offset from midnight
    int16_t i = (targetTime - midnightLocal) / 3600;

    // Validation: Ensure the index is within your array bounds
    if (i >= 0 && i < NUMWTHRDAYS * 24) {
        return (uint8_t)i;
    }

    return -120; // Not found/Out of bounds
}


int8_t WeatherInfoOptimized::getTemperature(uint32_t dt, bool wetbulb, bool asindex) {
    if (asindex) {
        if (wetbulb) return this->wetBulbTemperature[dt];
        
        return this->temperature[dt];
    }
    
    if (dt==0) dt = I.currentTime;
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    
    if (wetbulb) return this->wetBulbTemperature[i];
    else return this->temperature[i];
}

int8_t WeatherInfoOptimized::getHumidity(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->humidity[i];
}

int16_t WeatherInfoOptimized::getWeatherID(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    int16_t i = getIndex(dt);
    if (i<0) return -120; //999 is unknown weather
    return this->weatherID[i];
}

int8_t WeatherInfoOptimized::getPoP(uint32_t dt) {
    if (dt==0) {
        //cumulative probability of precipitation
        double totalPOP=1;
        for (uint16_t j=0;j<24;j++) {
            double tmp = this->PoP[j];
            if (tmp<0) tmp = 0;
            totalPOP *= (1 - (tmp/100));
        }
        return (uint8_t) ((1- totalPOP)*100);
    }
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->PoP[i];
}

int16_t WeatherInfoOptimized::getRain(uint32_t dt) {
    if (dt==0) {
        int16_t totalrain = 0;
        for (uint16_t j=0;j<24;j++) {
            int16_t tmp = this->rainmm[j];
            if (tmp<0) tmp = 0;
            totalrain+=tmp;
        }
        return totalrain;
    }
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->rainmm[i];
}

int16_t WeatherInfoOptimized::getSnow(uint32_t dt) {
    if (dt==0) {
        int16_t total = 0;
        for (uint16_t j=0;j<24;j++) {
            int16_t tmp = this->snowmm[j];
            if (tmp<0) tmp = 0;
            total+=tmp;
        }
        return total;
    }
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->snowmm[i];
}

int16_t WeatherInfoOptimized::getIce(uint32_t dt) {
    if (dt==0) {
        int16_t total = 0;
        for (uint16_t j=0;j<24;j++) {
            int16_t tmp = this->icemm[j];
            if (tmp<0) tmp = 0;
            total+=tmp;
        }
        return total;
    }
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->icemm[i];
}

int8_t WeatherInfoOptimized::getDewPoint(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->dewPoint[i];
}

int16_t WeatherInfoOptimized::getWindSpeed(uint32_t dt) {
    if (dt==0) dt = I.currentTime;
    int16_t i = getIndex(dt);
    if (i<0) return -120;
    return this->windspeed[i];
}


int16_t WeatherInfoOptimized::breakIconLink(String icon, TimeInterval ti) {
    // Same implementation as original Weather.cpp
    if (icon.indexOf("hurricane",0)>-1) return 504;
    if (icon.indexOf("tropical_storm",0)>-1) return 504;
    if (icon.indexOf("blizzard",0)>-1) return 603;
    
    if (icon.indexOf("snow",0)>-1) {
        if (icon.indexOf("rain_snow",0)>-1) return 611;
        if (icon.indexOf("snow_sleet",0)>-1) return 611;
        if (icon.indexOf("snow_fzra",0)>-1) return 611;
        
        if (ti.duration <= 3600 ) {
            int16_t total = this->getSnow(ti.start);
            if (total<13) return 600;
            if (total<38) return 601;
            return 602;
        } else {
            int16_t total = this->getDailySnow(ti.start,ti.start + ti.duration);
            if (total<25) return 600;
            if (total<100) return 601;
            return 602;
        }
        return 600;
    }
    
    if (icon.indexOf("fzra",0)>-1) return 611;
    if (icon.indexOf("sleet",0)>-1) return 301;
    
    if (icon.indexOf("tsra",0)>-1) {
        if (ti.duration <= 3600 ) {
            int16_t total = this->getRain(ti.start);
            if (total<0) total = 0;
            if (total<4) return 200;
            return 201;
        } else {
            int16_t total = this->getDailyRain(ti.start,ti.start + ti.duration);
            if (total<5) return 200;
            return 201;
        }
        if (icon.indexOf("tsra_hi",0)>-1) return 201;
        return 200;
    }
    
    if (icon.indexOf("rain",0)>-1) {
        if (icon.indexOf("rain_sleet",0)>-1) return 611;
        if (icon.indexOf("rain_fzra",0)>-1) return 611;
        
        if (ti.duration <= 3600 ) {
            int16_t total = this->getRain(ti.start);
            if (total<0) total = 0;
            if (total<1) return 301;
            if (total<2) return 500;
            if (total<4) return 501;
            if (total<6) return 502;
            return 503;
        } else {
            int16_t total = this->getDailyRain(ti.start,ti.start + ti.duration);
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

String WeatherInfoOptimized::nameWeatherIcon(uint16_t icon) {
    char weathername[50];
    if (nameWeatherIcon(icon, weathername)) return String(weathername);
    return String();
}

bool WeatherInfoOptimized::nameWeatherIcon(uint16_t icon, char* weathername) {
    switch (icon) {
        case 200: strcpy(weathername, "Thunderstorm"); return true;
        case 201: strcpy(weathername, "Heavy Thunderstorm"); return true;
        case 301: strcpy(weathername, "Drizzle"); return true;
        case 500: strcpy(weathername, "Light Rain"); return true;
        case 501: strcpy(weathername, "Moderate Rain"); return true;
        case 502: strcpy(weathername, "Heavy Rain"); return true;
        case 503: strcpy(weathername, "Storm"); return true;
        case 504: strcpy(weathername, "Hurricane"); return true;
        case 600: strcpy(weathername, "Light Snow"); return true;
        case 601: strcpy(weathername, "Moderate Snow"); return true;
        case 602: strcpy(weathername, "Heavy Snow"); return true;
        case 603: strcpy(weathername, "Blizzard"); return true;
        case 611: strcpy(weathername, "Freezing Rain"); return true;
        case 701: strcpy(weathername, "Cold"); return true;
        case 741: strcpy(weathername, "Fog"); return true;
        case 761: strcpy(weathername, "Dust"); return true;
        case 800: strcpy(weathername, "Clear"); return true;
        case 801: strcpy(weathername, "Partly Cloudy"); return true;
        case 802: strcpy(weathername, "Cloudy"); return true;
        case 803: strcpy(weathername, "Mostly Cloudy"); return true;
        case 804: strcpy(weathername, "Overcast"); return true;
        case 999: strcpy(weathername, "Unknown"); return true;
        default: sprintf(weathername, "%d", icon); return true;
    }
    return false;
}


bool WeatherInfoOptimized::initWeather() {
    for (uint16_t i=0; i < NUMWTHRDAYS * 24; i++) {
        this->dT[i] = 0;
        this->temperature[i] = -120; // Null temperature
        this->humidity[i] = 0;
        this->weatherID[i] = 999;    // Unknown weather
        this->PoP[i] = 0;
        this->dewPoint[i] = -120;
        this->windspeed[i] = -120;
        this->wetBulbTemperature[i] = -120;
        this->rainmm[i] = -120;
        this->icemm[i] = -120;
        this->snowmm[i] = -120;
    }

    
    for (uint16_t i=0;i<14;i++) {
        this->daily_dT[i] = 0;
        this->daily_tempMax[i] = -120;
        this->daily_tempMin[i] = -120;
        this->daily_weatherID[i] = 999;
        this->daily_PoP[i] = 0;
    }
    
    this->sunrise = 0;
    this->sunset = 0;
    
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

uint8_t WeatherInfoOptimized::getDailyPoP(uint8_t daysfromnow, bool indays) {
    if (daysfromnow >= 14) return 255;
    
    // We no longer need the 'indays' logic check here because 
    // dayOffset in processDailyData has already aligned the data.
    return this->daily_PoP[daysfromnow];
}

int16_t WeatherInfoOptimized::getDailyWeatherID(uint8_t daysfromnow, bool indays) {
    if (daysfromnow >= 14) return 255;
    
    return this->daily_weatherID[daysfromnow];
}



void WeatherInfoOptimized::getDailyTemp(uint8_t daysfromnow, int8_t* temp) {
    temp[0] = -120;
    temp[1] = -120;
    if (daysfromnow >= 14) {
        return;
    }
    
    // index 0 is High, index 1 is Low
    temp[0] = this->daily_tempMax[daysfromnow];
    temp[1] = this->daily_tempMin[daysfromnow];

    // Optional: If it's late and High is 0 (unreported), 
    // fall back to current temperature for the "High" slot
    if (daysfromnow == 0 && temp[0] == -120) {
        temp[0] = this->getTemperature(); 
    }
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


bool WeatherInfoOptimized::filterAlerts(const char* phenomenon) {
    /*
    TO	Tornado	Immediate Shelter
    SV	Severe Thunderstorm	High Winds / Large Hail
    SQ	Snow Squall	Sudden Road Danger
    HW	High Wind	Roof / Tree Damage
    HU	Hurricane	Structural / Flood
    TS	Tropical Storm	Evacuation / Shelter
    HI	Inland Hurricane	Evacuation / Shelter
    FF	Flash Flood	Basement / Road Flooding    
    WS	Winter Storm	Heavy Snow / Travel
    WW	Winter Weather	Heavy Snow / Travel
    IS	Ice Storm	Power Outage Risk
    BZ	Blizzard	Total Isolation
    ZR	Freezing Rain	Road Hazard
    EC	Extreme Cold	Heat Pump / Pipe Safety
    WC 	Wind Chill	Evacuation / Shelter
    EH	Excessive Heat	HVAC / Health Load
    AS	Air Quality / Ashfall	Indoor Filtration
    AF	Ashfall	Indoor Filtration    
    */


    if (strcmp(phenomenon, "TO") == 0) return true;
    if (strcmp(phenomenon, "SV") == 0) return true;
    if (strcmp(phenomenon, "SQ") == 0) return true;
    if (strcmp(phenomenon, "HW") == 0) return true;
    if (strcmp(phenomenon, "HU") == 0) return true;
    if (strcmp(phenomenon, "TS") == 0) return true;
    if (strcmp(phenomenon, "HI") == 0) return true;
    if (strcmp(phenomenon, "FF") == 0) return true;
    if (strcmp(phenomenon, "WS") == 0) return true;
    if (strcmp(phenomenon, "WW") == 0) return true;
    if (strcmp(phenomenon, "IS") == 0) return true;
    if (strcmp(phenomenon, "BZ") == 0) return true;
    if (strcmp(phenomenon, "ZR") == 0) return true;
    if (strcmp(phenomenon, "EC") == 0) return true;
    if (strcmp(phenomenon, "WC") == 0) return true;
    if (strcmp(phenomenon, "EH") == 0) return true;
    if (strcmp(phenomenon, "AS") == 0) return true;
    if (strcmp(phenomenon, "AF") == 0) return true;

    return false;
}

bool WeatherInfoOptimized::parseVTEC(const char* vtec, char* office, char* phenomenon, char* significance, char* etn, time_t* start, time_t* end) {
    // 1. Check Product Class (must be /O for Operational)
    // Format: /O.NEW.KBOX.WS.W.0004.260221T1800Z-260222T1200Z/
    if (vtec[1] != 'O') return false;

    // 2. Disregard CAN and EXP alerts
    // Action code starts at index 3 and is 3 chars long
    char action[4];
    strncpy(action, vtec + 3, 3);
    action[3] = '\0';
    if (strcmp(action, "CAN") == 0 || strcmp(action, "EXP") == 0) return false;

    // 3. Extract Office ID (Starts at index 7, 4 chars)
    strncpy(office, vtec + 7, 4);
    office[4] = '\0';

    // 4. Extract Phenomenon (Starts at index 12, 2 chars)
    strncpy(phenomenon, vtec + 12, 2);
    phenomenon[2] = '\0';

    // 5. Extract Significance (Starts at index 15, 1 char)
    significance[0] = vtec[15];
    significance[1] = '\0';

    // 6. Extract ETN (Starts at index 17, 4 chars)
    strncpy(etn, vtec + 17, 4);
    etn[4] = '\0';

    // 7. Parse Dates (Indices: Start at 22, End at 38)
    // Format: YYMMDDTHHMMZ
    *start = vtecTimeToUnix(vtec + 22, true);
    *end   = vtecTimeToUnix(vtec + 36, true);

    return true;
}

// Helper to convert VTEC timestamp (YYMMDDTHHMMZ) to time_t
time_t WeatherInfoOptimized::vtecTimeToUnix(const char* vtecPart, bool asLocalTime) {
    // Input: "260221T1800Z" (13 chars)
    // Target: "2026-02-21T18:00:00Z"
    
    char expanded[21];
    snprintf(expanded, sizeof(expanded), "20%c%c-%c%c-%c%cT%c%c:%c%c:00Z",
                vtecPart[0], vtecPart[1], // Year
                vtecPart[2], vtecPart[3], // Month
                vtecPart[4], vtecPart[5], // Day
                vtecPart[7], vtecPart[8], // Hour
                vtecPart[9], vtecPart[10] // Minute
    );

    return iso8601ToUnix(String(expanded), asLocalTime);
}

bool WeatherInfoOptimized::fetchWeatherAlerts() {
    return retryWithBackoff("weather alerts", [this]() {

        // 1. Initial Cleanup: Clear the internal alert active count
        this->NumWeatherEvents = 0;
        //set eventflags bit 0 to 0
        I.EventFlags &= ~0x01; //clear bit 0

        deleteFiles("/data/events/*", "/data/events");

        JsonDocument doc;
        int16_t httpCode;
        
        // NOAA Alerts endpoint by coordinates
        char cbuf[128];
        snprintf(cbuf, 127, "https://api.weather.gov/alerts/active?point=%.4f,%.4f", Prefs.LATITUDE, Prefs.LONGITUDE);
        String url = String(cbuf);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";
        extraHeaders += "If-Modified-Since: " + String(this->lastAlertFetchTime) + "\n";


        // Filter to only extract the features of interest from each alert 
        JsonDocument filter;
        filter["features"][0]["properties"]["headline"] = true;
        filter["features"][0]["properties"]["event"] = true;
        filter["features"][0]["properties"]["description"] = true;
        filter["features"][0]["properties"]["severity"] = true;
        filter["features"][0]["properties"]["certainty"] = true;
        filter["features"][0]["properties"]["urgency"] = true;
        filter["features"][0]["properties"]["onset"] = true;
        filter["features"][0]["properties"]["ends"] = true;
        filter["features"][0]["properties"]["parameters"]["VTEC"] = true;


        if (makeSecureRequest(url, doc, httpCode, ERROR_HTTPFAIL_BOX, ERROR_JSON_BOX, extraHeaders, "", &filter)) {

            if (httpCode == 304) {
                //no changes
                return true;
            } 
            JsonArray features = doc["features"].as<JsonArray>();
            
            if (features.size() == 0) {
                //no alerts found
                //clean up
            } else {

                for (JsonObject feature : features) {

                    JsonObject props = feature["properties"];
                    const char* vtecRaw = props["parameters"]["VTEC"][0]; // VTEC is usually the first element
                    
                    if (vtecRaw == nullptr) continue;

                    char off[5], phen[3], sig[2], etnStr[5];
                    time_t tStart, tEnd;
                    if (!parseVTEC(vtecRaw, off, phen, sig, etnStr, &tStart, &tEnd)) continue;

                    if (!filterAlerts(phen)) continue;

                    WeatherEventFile fileData;
                    memset(&fileData, 0, sizeof(WeatherEventFile));
                    strncpy(fileData.severity, props["severity"], 9);
                    strncpy(fileData.certainty, props["certainty"], 9);
                    strncpy(fileData.urgency, props["urgency"], 9);
                    fileData.time_start = tStart;
                    fileData.time_end = tEnd;
                    strncpy(fileData.office, off, 4);
                    strncpy(fileData.phenomenon, phen, 2);
                    strncpy(fileData.significance, sig, 1);                    
                    fileData.event_number = (uint16_t)atoi(etnStr);
                    strncpy(fileData.event, props["event"] | "", 99);
                    strncpy(fileData.headline, props["headline"] | "", 199);
                    strncpy(fileData.description, props["description"] | "", 1199);

                    // Generate Filename: expdate-office-phenomenon-ETN.txt
                    char filename[22];
                    snprintf(filename, 21, "/data/events/%d.txt", 
                        this->NumWeatherEvents+1);
            
                    if (!writeAnythingToSD(filename, &fileData, sizeof(WeatherEventFile))) {
                        storeError("fetchWeatherAlerts: Could not write weather event file");
                        continue;
                    }

                    this->NumWeatherEvents++;
                    //set EventFlagsbit 0 to 1
                    I.EventFlags |= 0x01; //set bit 0 to 1

                    this->lastAlertFetchTime = I.currentTime;
                }
            }
        } else {
                //error
                storeError("fetchWeatherAlerts: Could not fetch weather alerts");
        }
        return true;
    });
}

bool WeatherInfoOptimized::loadNextWeatherAlert() {
//load the next weather event into the this->alertInfo struct

    if (this->NumWeatherEvents == 0) return false;

    if (this->alertInfo.eventnumber == 0 || this->alertInfo.eventnumber == this->NumWeatherEvents) this->alertInfo.eventnumber = 1;
    else this->alertInfo.eventnumber++;

    snprintf(this->alertInfo.filename, 21, "/data/events/%d.txt", 
        this->alertInfo.eventnumber);
    
    WeatherEventFile fileData;

    if (!readAnythingFromSD(this->alertInfo.filename, &fileData, sizeof(WeatherEventFile))) return false;
    this->alertInfo.E_severity = parseSeverity(fileData.severity);
    this->alertInfo.E_certainty = parseCertainty(fileData.certainty);
    this->alertInfo.E_urgency = parseUrgency(fileData.urgency);
    strncpy(this->alertInfo.phenomenon, fileData.phenomenon, 2);
    this->alertInfo.time_start = fileData.time_start;
    this->alertInfo.time_end = fileData.time_end;

    return true;
}

bool WeatherInfoOptimized::getWeatherAlertDetails(String& event, String& headline, String& description, String& severity, String& certainty, String& urgency) {
    if (this->NumWeatherEvents == 0) return false;
    if (this->alertInfo.eventnumber == 0 || this->alertInfo.eventnumber > this->NumWeatherEvents) return false;

    WeatherEventFile fileData;

    if (!readAnythingFromSD(this->alertInfo.filename, &fileData, sizeof(WeatherEventFile))) return false;

    if (description != "") description = (String) fileData.description;
    if (headline != "") headline = (String) fileData.headline;
    if (event != "") event = (String) fileData.event;
    if (severity != "") severity = (String) fileData.severity;
    if (certainty != "") certainty = (String) fileData.certainty;
    if (urgency != "") urgency = (String) fileData.urgency;
    
    return true;
}



WeatherSeverity WeatherInfoOptimized::parseSeverity(const char* s) {
    if (strcmp(s, "Extreme") == 0) return WeatherSeverity::EXTREME;
    if (strcmp(s, "Severe") == 0) return WeatherSeverity::SEVERE;
    if (strcmp(s, "Moderate") == 0) return WeatherSeverity::MODERATE;
    if (strcmp(s, "Minor") == 0) return WeatherSeverity::MINOR;
    return WeatherSeverity::UNKNOWN;
}
WeatherCertainty WeatherInfoOptimized::parseCertainty(const char* s) {
    if (strcmp(s, "Unknown") == 0) return WeatherCertainty::UNCERTAIN;
    if (strcmp(s, "Unlikely") == 0) return WeatherCertainty::UNLIKELY;
    if (strcmp(s, "Possible") == 0) return WeatherCertainty::POSSIBLE;
    if (strcmp(s, "Likely") == 0) return WeatherCertainty::LIKELY;
    if (strcmp(s, "Observed") == 0) return WeatherCertainty::OBSERVED;
    return WeatherCertainty::UNCERTAIN;
}   
WeatherUrgency WeatherInfoOptimized::parseUrgency(const char* s) {
    if (strcmp(s, "Unknown") == 0) return WeatherUrgency::INDETERMINATE;
    if (strcmp(s, "Past") == 0) return WeatherUrgency::PAST;
    if (strcmp(s, "Future") == 0) return WeatherUrgency::FUTURE;
    if (strcmp(s, "Expected") == 0) return WeatherUrgency::EXPECTED;
    if (strcmp(s, "Imminent") == 0) return WeatherUrgency::IMMINENT;
    return WeatherUrgency::INDETERMINATE;
}
#endif
