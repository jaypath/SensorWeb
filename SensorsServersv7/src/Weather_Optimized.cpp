#include "globals.hpp"

#ifdef _USEWEATHER

#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "server.hpp"





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
    ti.start = iso8601ToUnix(tm);
    ti.start = unixToLocal(ti.start);

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
    char url[100];
    snprintf(url, 99, "https://api.weather.gov/points/%.4f,%.4f", Prefs.LATITUDE, Prefs.LONGITUDE);
    
    JsonDocument doc;

    HTTPMessage M;
    M.setUrl(url);
    M.setMethod("GET");
    M.setContentType("application/json");
    M.setExtraHeaders("User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n");    
    M.timeout = 15000; // 15 second timeout for geocoding
    M.usePSRAM = true;
    M.responseDoc = &doc;

    if (SendHTTPMessage(M)) {
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



// Optimized data processing methods
bool WeatherInfoOptimized::processHourlyData(JsonObject& properties) {
    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    uint32_t midnightToday = unixToLocal(makeUnixTime(year()-2000, month(), day(), 0, 0, 0,false)); //midnight local time, default behavior is to return local time
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
    uint32_t midnightToday = unixToLocal(makeUnixTime(year()-2000, month(), day(), 0, 0, 0,false)); //this is local time unless asLocalTime is false

    for (JsonObject period : periodsArray) {
        TimeInterval ti = parseNOAAInterval(period["startTime"].as<const char*>());
        TimeInterval ti_end = parseNOAAInterval(period["endTime"].as<const char*>());
        // FIX: Subtract 6 hours (21600s) before calculating dayOffset.
        // This ensures "Saturday Night" (starts 6pm) groups with "Saturday Day".
        const char* icon = period["icon"];
        double temp = (double)period["temperature"];
        const char* tu = period["temperatureUnit"];
        if (tu && strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }

        uint8_t currentPeriodNumber = (uint8_t)period["number"];
        
        //initialize the tomorrowPeriodNumber and isDaytime variables
        if (currentPeriodNumber == 1) {
            this->isDaytime = period["isDaytime"].as<bool>();
            if (period["isDaytime"]) this->tomorrowPeriodNumber = 3;
            else this->tomorrowPeriodNumber = 2;
        } 

        //save the daily forecast to a file for this day
        dailyWeatherForecast dWF;
        dWF.initDailyWeatherForecast();
        dWF.dT_start = ti.start;
        dWF.dT_end = ti_end.start;
        dWF.tempF = (int8_t)temp;
        dWF.weatherID = breakIconLink(String(icon), ti);
        dWF.PoP = (uint8_t)period["probabilityOfPrecipitation"]["value"];
        dWF.isDaytime = period["isDaytime"];
        strncpy(dWF.details, period["detailedForecast"].as<const char*>(), sizeof(dWF.details));

        //store this daily forecast to a file for this day, relative to now
        char filename[35];
        snprintf(filename, 34, "/Data/DailyWthr/%d.bin", currentPeriodNumber);
        if (!writeAnythingToSD(filename, &dWF, sizeof(dailyWeatherForecast))) {
            SerialPrint("Could not write daily forecast to file: " + String(filename), true);
            return false;
        }

        int dayOffset = (ti.start - 21600 - midnightToday) / 86400;
        
        // Safety: If it's early morning and we subtracted 6 hours, 
        // dayOffset might be -1. Clamp it to 0.
        if (dayOffset < 0) dayOffset = 0;
        if (dayOffset >= 14) continue;


        if (period["isDaytime"]) {
            // It's a High temp for that day
            this->daily_tempMax[dayOffset] = dWF.tempF;
            
            // Daytime data is usually the primary for the daily summary
            this->daily_PoP[dayOffset] = dWF.PoP;
            this->daily_weatherID[dayOffset] = dWF.weatherID;
        } else {
            // It's a Low temp for that day
            this->daily_tempMin[dayOffset] = dWF.tempF;
            
            // If Max is still -120, it means it's already evening and "Today's" High 
            // wasn't sent. Use Night's PoP/Icon as the fallback representative.
            if (this->daily_tempMax[dayOffset] == -120) { 
                this->daily_PoP[dayOffset] = dWF.PoP;
                this->daily_weatherID[dayOffset] = dWF.weatherID;
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

        bool b1=false;
        String extraHeaders = "User-Agent:(ArborysWeatherProject, contact@yourdomain.com)\n";

        JsonDocument filter;
        filter["properties"]["periods"][0]["startTime"] = true;
        filter["properties"]["periods"][0]["temperature"] = true;
        filter["properties"]["periods"][0]["temperatureUnit"] = true;
        filter["properties"]["periods"][0]["probabilityOfPrecipitation"]["value"] = true;
        filter["properties"]["periods"][0]["dewpoint"]["value"] = true;
        filter["properties"]["periods"][0]["relativeHumidity"]["value"] = true;
        filter["properties"]["periods"][0]["windSpeed"] = true;
        filter["properties"]["periods"][0]["icon"] = true;

        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = 15000; // 15 second timeout for nws
        M.usePSRAM = true;
        M.responseDoc = &doc;
        M.filter = &filter;
        M.setExtraHeaders(extraHeaders.c_str());

        if (SendHTTPMessage(M)) {
            // Check specifically for the properties object

            if (M.httpCode == 304) {
                SerialPrint("fetchHourlyForecast: data is not modified", true);
                return true;
            }

            JsonObject props = doc["properties"];
            if (!props.isNull()) {
                return processHourlyData(props);
            } else {
                SerialPrint("Hourly properties missing or doc too small", true);
            }
            
        } else {
            SerialPrint("Hourly request failed",true);
            SerialPrint("HTTP Code: " + String(M.httpCode),true);
            storeError("Hourly request failed with code: " + String(M.httpCode), ERROR_HTTP_RESPONSE,true);
            this->lastUpdateError = I.currentTime;
        }
        return false;
    });
}

bool WeatherInfoOptimized::fetchGridForecast() {
    return retryWithBackoff("grid forecast", [this]() {

        SerialPrint("fetchGridForecast: starting", true);
        JsonDocument doc;

        String url = getGrid(0);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";

        // Filter can stay in Stack/Internal RAM as it is very small
        JsonDocument filter;
        JsonObject props = filter["properties"];
        /*
        props["wetBulbGlobeTemperature"]["values"][0]["validTime"] = true;
        props["wetBulbGlobeTemperature"]["values"][0]["value"] = true;
        props["quantitativePrecipitation"]["values"][0]["validTime"] = true;
        props["quantitativePrecipitation"]["values"][0]["value"] = true;
        props["iceAccumulation"]["values"][0]["validTime"] = true;
        props["iceAccumulation"]["values"][0]["value"] = true;
        props["snowfallAmount"]["values"][0]["validTime"] = true;
        props["snowfallAmount"]["values"][0]["value"] = true;
        */

        props["wetBulbGlobeTemperature"]["values"] = true;
        props["quantitativePrecipitation"]["values"] = true;
        props["iceAccumulation"]["values"] = true;
        props["snowfallAmount"]["values"] = true;


        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = 15000; // 15 second timeout for nws
        M.usePSRAM = true;
        M.responseDoc = &doc;
        //M.filter = &filter;
        M.setExtraHeaders(extraHeaders.c_str());

        if (SendHTTPMessage(M)) {
            // Check specifically for the properties object

            if (M.httpCode == 304) {
                SerialPrint("fetchGridForecast: data is not modified", true);
                return true;
            }

            JsonObject props = doc["properties"];
            if (!props.isNull()) {
                return processGridData(props);
            } else {
                SerialPrint("Grid properties missing or doc too small", true);
            }
            
        } else {
            SerialPrint("Grid request failed",true);
            SerialPrint("HTTP Code: " + String(M.httpCode),true);
            storeError("Grid request failed with code: " + String(M.httpCode), ERROR_HTTP_RESPONSE,true);
            this->lastUpdateError = I.currentTime;
        }

        return false;
    });
}

bool WeatherInfoOptimized::fetchDailyForecast() {
    return retryWithBackoff("daily forecast", [this]() {
        JsonDocument doc;
        String url = getGrid(1);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";


        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = 15000; // 15 second timeout for nws
        M.usePSRAM = true;
        M.responseDoc = &doc;
        //M.filter = nullptr;
        M.setExtraHeaders(extraHeaders.c_str());

        if (SendHTTPMessage(M)) {
            // Check specifically for the properties object

            if (M.httpCode == 304) {
                SerialPrint("fetchDailyForecast: data is not modified", true);
                return true;
            }

            JsonObject props = doc["properties"];
            if (!props.isNull()) {
                return processDailyData(props);
            } else {
                SerialPrint("Daily properties missing or doc too small", true);
            }
            
        } else {
            SerialPrint("Daily request failed",true);
            SerialPrint("HTTP Code: " + String(M.httpCode),true);
            storeError("Daily request failed with code: " + String(M.httpCode), ERROR_HTTP_RESPONSE,true);
            this->lastUpdateError = I.currentTime;
        }
        return false;
    });
}

bool WeatherInfoOptimized::fetchSunriseSunset() {
    char cbuf[100];
    snprintf(cbuf, 99, "https://api.sunrisesunset.io/json?lat=%f&lng=%f", Prefs.LATITUDE, Prefs.LONGITUDE);
    String url = String(cbuf);
    
    JsonDocument doc;
    
    String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";


    HTTPMessage M;
    M.setUrl(url.c_str());
    M.setMethod("GET");
    M.setContentType("application/json");
    M.setCacert("bundle");
    M.timeout = 15000; // 15 second timeout for nws
    M.usePSRAM = true;
    M.responseDoc = &doc;
    //M.filter = nullptr;
    M.setExtraHeaders(extraHeaders.c_str());

    if (SendHTTPMessage(M)) {
        // Check specifically for the properties object

        if (M.httpCode == 304) {
            SerialPrint("fetchSunriseSunset: data is not modified", true);
        }

        if (doc["results"].is<JsonVariantConst>()) {
            JsonObject results = doc["results"];
            const char* srise = results["sunrise"];
            const char* sset = results["sunset"];
            const char* sdat = results["date"];

            
            String sun = String(sdat) + " " + String(srise);
            this->sunrise = convertStrTime(sun,true); //false, was reported in local time so do not try to convert from UTC to local
            this->sunrise = unixToLocal(this->sunrise);

            sun = String(sdat) + " " + String(sset);
            this->sunset = convertStrTime(sun,true); //false, was reported in local time so do not try to convert from UTC to local
            this->sunset = unixToLocal(this->sunset);
            return true;
        } else {
            SerialPrint("Sunrise Sunset Json had incorrect format", true);
            storeError("Sunrise Sunset Json had incorrect format", ERROR_JSON_SUNRISE,true);
            this->lastUpdateError = I.currentTime;
        }
    } else {
        SerialPrint("Sunrise Sunset request failed",true);
        SerialPrint("HTTP Code: " + String(M.httpCode),true);
        storeError("Sunrise Sunset request failed with code: " + String(M.httpCode), ERROR_HTTP_RESPONSE,true);
        this->lastUpdateError = I.currentTime;
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
    uint32_t midnightLocal = unixToLocal(makeUnixTime(year()-2000, month(), day(), 0, 0, 0, false));

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
    uint32_t MN0 = unixToLocal(makeUnixTime(year()-2000,month(),day(),0,0,0,false));
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
    uint32_t MN0 = unixToLocal(makeUnixTime(year()-2000,month(),day(),0,0,0,false));
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
    uint32_t MN0 = unixToLocal(makeUnixTime(year()-2000,month(),day(),0,0,0,false));
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

String WeatherInfoOptimized::getAlertName(const char* phenomenon) {
    if (phenomenon == nullptr) {
        //use the currently loaded alertinfo
        phenomenon = this->alertInfo.phenomenon;
        if (phenomenon == nullptr) return "Unknown";
    }
    if (strcmp(phenomenon, "TO") == 0) return "Tornado";
    if (strcmp(phenomenon, "SV") == 0) return "Severe Thunderstorm";
    if (strcmp(phenomenon, "SQ") == 0) return "Snow Squall";
    if (strcmp(phenomenon, "HW") == 0) return "High Wind";
    if (strcmp(phenomenon, "HU") == 0) return "Hurricane";
    if (strcmp(phenomenon, "TS") == 0) return "Tropical Storm";
    if (strcmp(phenomenon, "HI") == 0) return "Inland Hurricane";
    if (strcmp(phenomenon, "FF") == 0) return "Flash Flood";
    if (strcmp(phenomenon, "WS") == 0) return "Winter Storm";
    if (strcmp(phenomenon, "WW") == 0) return "Winter Weather";
    if (strcmp(phenomenon, "IS") == 0) return "Ice Storm";
    if (strcmp(phenomenon, "BZ") == 0) return "Blizzard";
    if (strcmp(phenomenon, "ZR") == 0) return "Freezing Rain";
    if (strcmp(phenomenon, "EC") == 0) return "Extreme Cold";
    if (strcmp(phenomenon, "WC") == 0) return "Wind Chill";
    if (strcmp(phenomenon, "EH") == 0) return "Excessive Heat";
    if (strcmp(phenomenon, "AS") == 0) return "Air Quality";
    if (strcmp(phenomenon, "AF") == 0) return "Ashfall";
    return "Unknown";
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

    // 7. Parse Dates (Indices: Start at 22, End at 36)
    // Format: YYMMDDTHHMMZ
    *start = unixToLocal(vtecTimeToUnix(vtec + 22));
    *end   = unixToLocal(vtecTimeToUnix(vtec + 36));

    return true;
}

// Helper to convert VTEC timestamp (YYMMDDTHHMMZ) to time_t
time_t WeatherInfoOptimized::vtecTimeToUnix(const char* vtecPart) {
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

    return iso8601ToUnix(String(expanded));
}

bool WeatherInfoOptimized::fetchWeatherAlerts() {
    return retryWithBackoff("weather alerts", [this]() {

        SerialPrint("fetchWeatherAlerts: Starting", true);
        // 1. Initial Cleanup: Clear the internal alert active count

        JsonDocument doc;
        int16_t httpCode;
        
        // NOAA Alerts endpoint by coordinates
        char url[128];
        snprintf(url, 127, "https://api.weather.gov/alerts/active?point=%.4f,%.4f", Prefs.LATITUDE, Prefs.LONGITUDE);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\n";
        extraHeaders += "If-Modified-Since: " + String(this->lastAlertFetchTime) + "\n";


        // Filter to only extract the features of interest from each alert 
        JsonDocument filter;
        filter.clear();
        filter["features"][0]["properties"] = true;

//        filter["features"][0]["properties"]["headline"] = true;
//        filter["features"][0]["properties"]["event"] = true;
//        filter["features"][0]["properties"]["description"] = true;
//        filter["features"][0]["properties"]["severity"] = true;
//        filter["features"][0]["properties"]["certainty"] = true;
//        filter["features"][0]["properties"]["urgency"] = true;
//        filter["features"][0]["properties"]["onset"] = true;
//        filter["features"][0]["properties"]["ends"] = true;
//        filter["features"][0]["properties"]["parameters"]["VTEC"] = true;

        HTTPMessage M;
        M.setUrl(url);
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = 15000; // 15 second timeout for nws
        M.usePSRAM = true;
        M.responseDoc = &doc;
        //M.filter = &filter;
        M.setExtraHeaders(extraHeaders.c_str());

        bool success = SendHTTPMessage(M);
        if (success) {
            this->lastAlertUpdateTime = unixToLocal(iso8601ToUnix(doc["updated"].as<String>()));
            this->lastAlertFetchTime = I.currentTime;

            if (M.httpCode == 304) {
                //no changes
                return true;
            } 

            //cleanup
            this->NumWeatherEvents = 0;
            deleteFiles("*", "/data/events");    
            //clear bits 0,2,3
            bitWrite(I.WeatherEventFlags, 0, 0); //bit 0 is for flagged alerts
            bitWrite(I.WeatherEventFlags, 2, 0); //bit 2 is for severe alerts
            bitWrite(I.WeatherEventFlags, 3, 0); //bit 3 is for imminent alerts

            this->NumWeatherEvents = 0;    
            this->alertInfo.initAlertInfo();
    
            JsonArray features = doc["features"].as<JsonArray>();
            
            if (features.size() == 0) {
                //no alerts found
                //clean up
            } else {

                for (JsonObject feature : features) {
                    this->NumWeatherEvents++;

                    JsonObject props = feature["properties"];
                    const char* vtecRaw = props["parameters"]["VTEC"][0]; // VTEC is usually the first element
                    
                    if (vtecRaw == nullptr) continue;

                    char off[5], phen[3], sig[2], etnStr[5];
                    time_t tStart, tEnd;
                    if (!parseVTEC(vtecRaw, off, phen, sig, etnStr, &tStart, &tEnd)) continue;

                    if (!filterAlerts(phen)) continue;

                    //set event bit one, bit 0 is for flagged alerts are present                    
                    bitWrite(I.WeatherEventFlags, 0, 1);

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
                    snprintf(filename, 21, "/Data/Events/%d.txt", 
                        this->NumWeatherEvents+1);

                    int16_t eseverity = parseSeverity(fileData.severity);
                    int16_t eurgency = parseUrgency(fileData.urgency);

                    
                    if (eseverity>this->alertInfo.E_severity) {
                        //set the local info
                        this->alertInfo.E_severity = parseSeverity(fileData.severity);
                        this->alertInfo.E_certainty = parseCertainty(fileData.certainty);
                        this->alertInfo.E_urgency = parseUrgency(fileData.urgency);
                        strncpy(this->alertInfo.phenomenon, phen, 2);
                        this->alertInfo.time_start = tStart;
                        this->alertInfo.time_end = tEnd;
                        this->alertInfo.eventnumber = fileData.event_number;
                        strncpy(this->alertInfo.event, fileData.event, 32);

                        if (eseverity == WeatherSeverity::EXTREME || eseverity == WeatherSeverity::SEVERE) bitWrite(I.WeatherEventFlags, 2,1); //set bit 2 to 1
                        if (eurgency == WeatherUrgency::IMMINENT) bitWrite(I.WeatherEventFlags, 3,1); //set bit 3 to 1
                    }


                    if (!writeAnythingToSD(filename, &fileData, sizeof(WeatherEventFile))) {
                        SerialPrint("fetchWeatherAlerts: Could not write weather event file: " + String(filename), true);
                        storeError("fetchWeatherAlerts: Could not write weather event file");
                        continue;
                    }

                }
            }
            SerialPrint("fetchWeatherAlerts: " + String(this->NumWeatherEvents) + " alerts fetched successfully", true);
            return true;
            
        } else {
            SerialPrint("Alert request failed",true);
            SerialPrint("HTTP Code: " + String(M.httpCode),true);
            storeError("Alert request failed with code: " + String(M.httpCode), ERROR_HTTP_RESPONSE,true);
            this->lastUpdateError = I.currentTime;
            return false;
        }

        return false;
    });
}

bool WeatherInfoOptimized::loadNextWeatherAlert() {
//load the next weather event into the this->alertInfo struct

    if (this->NumWeatherEvents == 0) return false;

    if (this->alertInfo.eventnumber == 0 || this->alertInfo.eventnumber == this->NumWeatherEvents) this->alertInfo.eventnumber = 1;
    else this->alertInfo.eventnumber++;

    char filename [32] = "";
    snprintf(filename, 31, "/data/events/%d.txt", 
        this->alertInfo.eventnumber);
    
    WeatherEventFile fileData;

    if (!readAnythingFromSD(filename, &fileData, sizeof(WeatherEventFile))) return false;
    if (fileData.time_end < I.currentTime) return false; //alert is in the past, so no longer active

    this->alertInfo.E_severity = parseSeverity(fileData.severity);
    this->alertInfo.E_certainty = parseCertainty(fileData.certainty);
    this->alertInfo.E_urgency = parseUrgency(fileData.urgency);
    strncpy(this->alertInfo.phenomenon, fileData.phenomenon, 2);
    this->alertInfo.time_start = fileData.time_start;
    this->alertInfo.time_end = fileData.time_end;

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
