#include "globals.hpp"

#ifdef _USEWEATHER

#include "Weather_Optimized.hpp"
#include "timesetup.hpp"
#include "server.hpp"

namespace {

void buildHourlyJsonFilter(JsonDocument& filterDoc) {
    JsonObject period = filterDoc["properties"]["periods"][0].to<JsonObject>();
    period["startTime"] = true;
    period["temperature"] = true;
    period["temperatureUnit"] = true;
    period["probabilityOfPrecipitation"]["value"] = true;
    period["dewpoint"]["value"] = true;
    period["relativeHumidity"]["value"] = true;
    period["windSpeed"] = true;
    period["icon"] = true;
}

void buildDailyJsonFilter(JsonDocument& filterDoc) {
    JsonObject period = filterDoc["properties"]["periods"][0].to<JsonObject>();
    period["startTime"] = true;
    period["endTime"] = true;
    period["icon"] = true;
    period["isDaytime"] = true;
    period["temperature"] = true;
    period["temperatureUnit"] = true;
    period["probabilityOfPrecipitation"]["value"] = true;
    period["number"] = true;
    period["detailedForecast"] = true;
}

void buildGridJsonFilter(JsonDocument& filterDoc) {
    JsonObject wbgt = filterDoc["properties"]["wetBulbGlobeTemperature"]["values"][0].to<JsonObject>();
    wbgt["validTime"] = true;
    wbgt["value"] = true;
    JsonObject rain = filterDoc["properties"]["quantitativePrecipitation"]["values"][0].to<JsonObject>();
    rain["validTime"] = true;
    rain["value"] = true;
    JsonObject ice = filterDoc["properties"]["iceAccumulation"]["values"][0].to<JsonObject>();
    ice["validTime"] = true;
    ice["value"] = true;
    JsonObject snow = filterDoc["properties"]["snowfallAmount"]["values"][0].to<JsonObject>();
    snow["validTime"] = true;
    snow["value"] = true;
}

void weatherSetupStepBegin(const char* label, bool setupProgress) {
    if (!setupProgress) return;
    SerialPrint((String("Weather setup: ") + label + "...").c_str(), true);
    #ifdef _USETFT
    tftPrint(String(label) + "... ", false, TFT_WHITE, 2, 1, false, -1, -1);
    #endif
}

void weatherSetupStepEnd(bool ok, bool setupProgress) {
    if (!setupProgress) return;
    SerialPrint(ok ? "Weather setup: OK." : "Weather setup: FAIL.", true);
    #ifdef _USETFT
    tftPrint(ok ? "OK." : "FAIL.", true, ok ? TFT_GREEN : TFT_RED);
    #endif
}

} // namespace





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

uint32_t WeatherInfoOptimized::localMidnightToday() {
    time_t localNow = I.currentTime;
    return unixToLocal(makeUnixTime((byte)(year(localNow) - 2000), month(localNow), day(localNow), 0, 0, 0, false));
}

int32_t WeatherInfoOptimized::nwsDayOffsetFromMidnight(time_t t) {
    uint32_t midnightToday = localMidnightToday();
    int32_t dayOffset = (int32_t)((t - 21600 - midnightToday) / 86400);
    if (dayOffset < 0) dayOffset = 0;
    return dayOffset;
}

time_t WeatherInfoOptimized::floorToHour(time_t t) {
    return (time_t)((uint32_t)t - ((uint32_t)t % 3600UL));
}

int16_t WeatherInfoOptimized::hourSlot(time_t targetTime) const {
    if (!isHourlyValid()) return -1;
    if (targetTime < (time_t)hourBase) return -1;
    int32_t i = (int32_t)((targetTime - (time_t)hourBase) / 3600);
    if (i >= 0 && i < NUM_HOURLY) return (int16_t)i;
    return -1;
}

time_t WeatherInfoOptimized::hourTime(int16_t slot) const {
    if (!isHourlyValid() || slot < 0 || slot >= NUM_HOURLY) return 0;
    return (time_t)hourBase + (time_t)slot * 3600;
}

void WeatherInfoOptimized::ensureHourBaseFromTimestamp(time_t t) {
    if (t == 0) return;
    time_t h = floorToHour(t);

    if (!isHourlyValid()) {
        hourBase = (uint32_t)h;
        return;
    }

    // Re-anchor when hourly failed but SD left an old hourBase with no live slots
    bool anyHourly = false;
    for (uint16_t i = 0; i < NUM_HOURLY; i++) {
        if (hourly[i].isValid()) {
            anyHourly = true;
            break;
        }
    }
    if (!anyHourly) {
        time_t windowEnd = (time_t)hourBase + (time_t)NUM_HOURLY * 3600;
        if (h < (time_t)hourBase || h >= windowEnd) {
            hourBase = (uint32_t)h;
        }
    }
}

void WeatherInfoOptimized::setupPeriodAnchor(time_t firstPeriodStart, bool firstIsDaytime) {
    periodBaseIsDaytime = true;
    periodAnchorDayOffset = nwsDayOffsetFromMidnight(firstPeriodStart);
    if (firstIsDaytime) {
        periodBaseStart = (uint32_t)firstPeriodStart;
        firstFilledPeriod = 0;
    } else {
        periodBaseStart = localMidnightToday() + (uint32_t)periodAnchorDayOffset * 86400UL;
        firstFilledPeriod = 1;
    }
}

int16_t WeatherInfoOptimized::periodSlotForDayDiff(int32_t dayDiff, bool wantDaytime) const {
    if (periodBaseStart == 0) return -1;
    int16_t slot = (int16_t)(2 * dayDiff + (wantDaytime ? 0 : 1));
    if (slot < 0 || slot >= NUM_PERIODS) return -1;
    return slot;
}

int16_t WeatherInfoOptimized::getPeriodSlotForDaysFromToday(uint8_t daysfromnow, bool wantDaytime) const {
    int32_t todayDayOffset = nwsDayOffsetFromMidnight(I.currentTime);
    int32_t dayDiff = (todayDayOffset + (int32_t)daysfromnow) - periodAnchorDayOffset;
    return periodSlotForDayDiff(dayDiff, wantDaytime);
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
byte WeatherInfoOptimized::updateWeatherOptimized(uint16_t synctime, bool setupProgress) {
    //returns 3 if data is still fresh, 2 if too soon to retry, 1 if update was needed and successful, 0 if update failed

    // Check if update is needed
    if (this->lastUpdateT>0 && this->lastUpdateT + synctime > I.currentTime) {
        if (setupProgress) {
            SerialPrint("Weather setup: data still fresh, skipping NOAA fetch.", true);
            #ifdef _USETFT
            tftPrint("Weather data still fresh.", true, TFT_GREEN);
            #endif
        }
        return 3; // Data is still fresh
    }
    
    // Check retry logic
    if ((uint32_t) this->lastUpdateAttempt>0 && this->lastUpdateAttempt + 600 > I.currentTime) {
        if (setupProgress) {
            SerialPrint("Weather setup: too soon to retry NOAA fetch.", true);
            #ifdef _USETFT
            tftPrint("Weather retry cooldown.", true, TFT_YELLOW);
            #endif
        }
        return 2; // Too soon to retry
    }

    this->lastUpdateAttempt = I.currentTime;

    if (!wifiReadyForNetwork()) {
        SerialPrint("Weather update: WiFi not available", true);
        return 0;
    }

    #ifdef _USE_HEADER_INFO_ALERT
    HeaderInfoAlertGuard headerAlert("Wthr update", TFT_YELLOW, TFT_BLACK, 300);
    #endif
    
    SerialPrint("Weather update optimized starting...",true);
    esp_task_wdt_reset();

    weatherSetupStepBegin("Grid coords", setupProgress);
    bool gridCoordsOk = loadFromCache() || fetchGridCoordinates();
    if (gridCoordsOk) {
        SerialPrint("Grid coordinates ready", true);
    }
    weatherSetupStepEnd(gridCoordsOk, setupProgress);
    esp_task_wdt_reset();

    uint32_t start_time = millis();

    // Each fetch clears only its own domain inside the processor (on new data).
    // Failed fetches leave other domains (e.g. SD-loaded daily) intact.
    weatherSetupStepBegin("Hourly forecast", setupProgress);
    bool hourlyOk = fetchHourlyForecast();
    weatherSetupStepEnd(hourlyOk, setupProgress);
    esp_task_wdt_reset();

    weatherSetupStepBegin("Grid forecast", setupProgress);
    bool gridOk = fetchGridForecast();
    weatherSetupStepEnd(gridOk, setupProgress);
    esp_task_wdt_reset();

    weatherSetupStepBegin("Daily forecast", setupProgress);
    bool dailyOk = fetchDailyForecast();
    weatherSetupStepEnd(dailyOk, setupProgress);
    esp_task_wdt_reset();

    weatherSetupStepBegin("Weather alerts", setupProgress);
    bool alertsOk = fetchWeatherAlerts();
    weatherSetupStepEnd(alertsOk, setupProgress);
    esp_task_wdt_reset();

    weatherSetupStepBegin("Sunrise/sunset", setupProgress);
    bool sunOk = fetchSunriseSunset();
    weatherSetupStepEnd(sunOk, setupProgress);
    esp_task_wdt_reset();

    bool forecastUpdated = hourlyOk || gridOk || dailyOk;
    bool fullSuccess = hourlyOk && gridOk && dailyOk && alertsOk && sunOk;

    SerialPrint(("Weather fetch results: hourly=" + String(hourlyOk) + " grid=" + String(gridOk) +
        " daily=" + String(dailyOk) + " alerts=" + String(alertsOk) + " sun=" + String(sunOk)).c_str(), true);

    if (forecastUpdated) {
        #ifdef _USESDCARD
        storeWeatherDataSD();
        #endif
    }

    if (fullSuccess) {
        this->lastUpdateT = I.currentTime;
        this->fetchedAt = I.currentTime;

        saveToCache();

        uint32_t response_time = millis() - start_time;
        average_response_time = (average_response_time + response_time) / 2;
        total_api_calls++;

        SerialPrint(("Weather update completed in " + String(response_time) + " ms").c_str(),true);
    } else {
        failed_api_calls++;
        if (forecastUpdated) {
            SerialPrint("Weather update partially succeeded (some fetches failed)", true);
        } else {
            SerialPrint("Weather update failed for the " + (String) failed_api_calls + "th time", true);
        }
    }

    return fullSuccess ? 1 : 0;
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
    M.timeout = WEATHER_HTTP_TIMEOUT_SHORT_MS;
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
    initHourlyForecastData();

    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    uint8_t processedCount = 0;
    bool hourBaseSet = false;

    for (JsonObject period : periodsArray) {
        String tmp = period["startTime"].as<const char*>();
        TimeInterval ti = parseNOAAInterval(tmp);

        time_t periodHour = floorToHour(ti.start);
        if (!hourBaseSet) {
            hourBase = (uint32_t)periodHour;
            hourBaseSet = true;
        }

        int16_t i = hourSlot(periodHour);
        if (i < 0) continue;

        double temp = (double)period["temperature"];
        const char* tu = period["temperatureUnit"];
        if (tu && strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }
        hourly[i].temperature = (int8_t)temp;
        hourly[i].PoP = (int8_t)period["probabilityOfPrecipitation"]["value"];
        hourly[i].dewPoint = (int8_t)period["dewpoint"]["value"];
        hourly[i].humidity = (int8_t)period["relativeHumidity"]["value"];

        const char* ws = period["windSpeed"].as<const char*>();
        String wind_str = String(ws);
        int mph_index = wind_str.indexOf(" mph");
        if (mph_index > 0) {
            hourly[i].windSpeed = (int16_t)wind_str.substring(0, mph_index).toInt();
        }

        const char* icon = period["icon"].as<const char*>();
        hourly[i].weatherID = breakIconLink(String(icon), ti);

        processedCount++;
    }

    if (processedCount > 0) return true;

    storeError("Could not parse Hourly JSON", ERROR_JSON_HOURLY, true);
    this->lastUpdateError = I.currentTime;
    return false;
}


bool WeatherInfoOptimized::processGridData(JsonObject& properties) {
    bool hasWbgtField = properties["wetBulbGlobeTemperature"]["values"].is<JsonArray>();
    bool hasRainField = properties["quantitativePrecipitation"]["values"].is<JsonArray>();
    bool hasIceField = properties["iceAccumulation"]["values"].is<JsonArray>();
    bool hasSnowField = properties["snowfallAmount"]["values"].is<JsonArray>();

    if (!hasWbgtField && !hasRainField && !hasIceField && !hasSnowField) {
        storeError("Grid JSON missing expected fields", ERROR_JSON_GRID, true);
        this->lastUpdateError = I.currentTime;
        return false;
    }

    for (uint16_t i = 0; i < NUM_HOURLY; i++) {
        hourly[i].wetBulb = WEATHER_INVALID_TEMP;
    }
    flag_rain = false;
    flag_ice = false;
    flag_snow = false;

    uint16_t wetBulbCount = 0;

    if (hasWbgtField) {
        JsonArray values = properties["wetBulbGlobeTemperature"]["values"].as<JsonArray>();
        for (JsonObject value : values) {
            if (value["validTime"].isNull() || value["value"].isNull()) continue;

            TimeInterval ti = parseNOAAInterval(value["validTime"].as<const char*>());
            if (ti.start == 0) continue;

            ensureHourBaseFromTimestamp(ti.start);

            int hoursInInterval = ti.duration / 3600;
            if (hoursInInterval < 1) hoursInInterval = 1;

            double tempC = (double)value["value"];
            int8_t tempF = (int8_t)((tempC * 9.0 / 5.0) + 32.0);

            for (int h = 0; h < hoursInInterval; h++) {
                time_t slotTime = floorToHour(ti.start + (h * 3600));
                int16_t idx = hourSlot(slotTime);
                if (idx >= 0 && idx < NUM_HOURLY) {
                    hourly[idx].wetBulb = tempF;
                    wetBulbCount++;
                }
            }
        }
    }

    uint16_t precipSlots = 0;
    if (hasRainField) {
        processPrecipitationData(properties, "quantitativePrecipitation", PrecipField::Rain, this->flag_rain);
        precipSlots += flag_rain ? 1 : 0;
    }
    if (hasIceField) {
        processPrecipitationData(properties, "iceAccumulation", PrecipField::Ice, this->flag_ice);
        precipSlots += flag_ice ? 1 : 0;
    }
    if (hasSnowField) {
        processPrecipitationData(properties, "snowfallAmount", PrecipField::Snow, this->flag_snow);
        precipSlots += flag_snow ? 1 : 0;
    }

    if (wetBulbCount > 0 || precipSlots > 0) {
        return true;
    }

    // NOAA often omits WBGT values; accept a parsed grid response with no active precip.
    if (hasRainField || hasIceField || hasSnowField) {
        SerialPrint("processGridData: no mapped grid slots (WBGT/precip empty for window)", true);
        return true;
    }

    storeError("Could not parse Grid JSON", ERROR_JSON_GRID, true);
    this->lastUpdateError = I.currentTime;
    return false;
}

void WeatherInfoOptimized::processPrecipitationData(JsonObject& properties, const char* field_name,
                                                    PrecipField field, bool& flag) {
    if (field == PrecipField::Rain) {
        for (uint16_t i = 0; i < NUM_HOURLY; i++) hourly[i].rainMm = 0;
    } else if (field == PrecipField::Ice) {
        for (uint16_t i = 0; i < NUM_HOURLY; i++) hourly[i].iceMm = 0;
    } else {
        for (uint16_t i = 0; i < NUM_HOURLY; i++) hourly[i].snowMm = 0;
    }
    flag = false;

    JsonArray values = properties[field_name]["values"].as<JsonArray>();

    for (JsonObject value : values) {
        if (value["validTime"].isNull() || value["value"].isNull()) continue;

        TimeInterval ti = parseNOAAInterval(value["validTime"].as<String>());
        if (ti.start == 0) continue;

        float totalValue = value["value"];
        if (totalValue <= 0) continue;

        ensureHourBaseFromTimestamp(ti.start);

        int hoursInInterval = ti.duration / 3600;
        if (hoursInInterval < 1) hoursInInterval = 1;

        float hourlyRate = totalValue / (float)hoursInInterval;

        for (int h = 0; h < hoursInInterval; h++) {
            time_t currentHour = floorToHour(ti.start + (h * 3600));
            int16_t idx = hourSlot(currentHour);

            if (idx >= 0 && idx < NUM_HOURLY) {
                int16_t rate = (int16_t)hourlyRate;
                if (field == PrecipField::Rain) {
                    hourly[idx].rainMm = rate;
                } else if (field == PrecipField::Ice) {
                    hourly[idx].iceMm = rate;
                } else {
                    hourly[idx].snowMm = rate;
                }
                flag = true;
            }
        }
    }
}


bool WeatherInfoOptimized::processDailyData(JsonObject& properties) {
    initDailyPeriodData();

    JsonArray periodsArray = properties["periods"].as<JsonArray>();
    bool anchorSet = false;

    for (JsonObject period : periodsArray) {
        TimeInterval ti = parseNOAAInterval(period["startTime"].as<const char*>());
        TimeInterval ti_end = parseNOAAInterval(period["endTime"].as<const char*>());
        const char* icon = period["icon"];
        bool periodIsDaytime = period["isDaytime"].as<bool>();
        double temp = (double)period["temperature"];
        const char* tu = period["temperatureUnit"];
        if (tu && strcmp(tu, "C") == 0) {
            temp = (temp * 9.0 / 5.0) + 32.0;
        }

        uint8_t currentPeriodNumber = (uint8_t)period["number"];

        if (!anchorSet && currentPeriodNumber == 1) {
            setupPeriodAnchor(ti.start, periodIsDaytime);
            anchorSet = true;
        }

        dailyWeatherForecast dWF;
        dWF.initDailyWeatherForecast();
        dWF.dT_start = ti.start;
        dWF.dT_end = ti_end.start;
        dWF.tempF = (int8_t)temp;
        dWF.weatherID = breakIconLink(String(icon), ti);
        dWF.PoP = (uint8_t)period["probabilityOfPrecipitation"]["value"];
        dWF.isDaytime = periodIsDaytime;
        strncpy(dWF.details, period["detailedForecast"].as<const char*>(), sizeof(dWF.details));

        char filename[35];
        snprintf(filename, 34, "/Data/DailyWthr/%d.bin", currentPeriodNumber);
        if (!writeAnythingToSD(filename, &dWF, sizeof(dailyWeatherForecast))) {
            SerialPrint("Could not write daily forecast to file: " + String(filename), true);
            return false;
        }

        if (!anchorSet) continue;

        int32_t dayOffset = nwsDayOffsetFromMidnight(ti.start);
        int16_t slot = (int16_t)(2 * dayOffset + (periodIsDaytime ? 0 : 1));
        if (slot < 0 || slot >= NUM_PERIODS) continue;

        periods[slot].start = ti.start;
        periods[slot].end = ti_end.start;
        periods[slot].temp = dWF.tempF;
        periods[slot].weatherID = dWF.weatherID;
        periods[slot].PoP = dWF.PoP;
        periods[slot].isDaytime = periodIsDaytime;
    }
    return anchorSet;
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
        esp_task_wdt_reset();
        if (request_func()) {
            return true;
        }
        
        if (attempt < MAX_RETRY_ATTEMPTS - 1) {
            uint32_t delay_ms = (3 * attempt) * 1000; // Exponential backoff: 0s, 3s, 6s
            for (uint32_t waited = 0; waited < delay_ms; waited += 500) {
                delay(500);
                esp_task_wdt_reset();
            }
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

        JsonDocument filterDoc;
        buildHourlyJsonFilter(filterDoc);
        JsonDocument doc;

        String url = getGrid(2);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\nAccept: application/geo+json\n";

        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = WEATHER_HTTP_TIMEOUT_MS;
        M.usePSRAM = true;
        M.responseDoc = &doc;
        M.filter = &filterDoc;
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
                SerialPrint("Hourly properties missing after HTTP OK", true);
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
        JsonDocument filterDoc;
        buildGridJsonFilter(filterDoc);
        JsonDocument doc;

        String url = getGrid(0);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\nAccept: application/geo+json\n";

        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = WEATHER_HTTP_TIMEOUT_MS;
        M.usePSRAM = true;
        M.responseDoc = &doc;
        M.filter = &filterDoc;
        M.setExtraHeaders(extraHeaders.c_str());

        if (SendHTTPMessage(M)) {
            // Check specifically for the properties object

            if (M.httpCode == 304) {
                SerialPrint("fetchGridForecast: data is not modified", true);
                return true;
            }

            JsonObject props = doc["properties"];
            if (!props.isNull()) {
                if (processGridData(props)) return true;
                SerialPrint("fetchGridForecast: processGridData failed", true);
            } else {
                SerialPrint("Grid properties missing after HTTP OK", true);
                storeError("Grid properties missing after HTTP OK", ERROR_JSON_GRID, true);
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
        JsonDocument filterDoc;
        buildDailyJsonFilter(filterDoc);
        JsonDocument doc;
        String url = getGrid(1);
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\nAccept: application/geo+json\n";


        HTTPMessage M;
        M.setUrl(url.c_str());
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = WEATHER_HTTP_TIMEOUT_MS;
        M.usePSRAM = true;
        M.responseDoc = &doc;
        M.filter = &filterDoc;
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
    M.timeout = WEATHER_HTTP_TIMEOUT_SHORT_MS;
    M.usePSRAM = true;
    M.responseDoc = &doc;
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

int8_t WeatherInfoOptimized::getTemperature(uint32_t dt, bool wetbulb, bool asindex) {
    if (asindex) {
        if (dt >= NUM_HOURLY) return WEATHER_INVALID_TEMP;
        if (wetbulb) return hourly[dt].wetBulb;
        return hourly[dt].temperature;
    }

    if (dt == 0) dt = I.currentTime;
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;

    if (wetbulb) return hourly[i].wetBulb;
    return hourly[i].temperature;
}

int8_t WeatherInfoOptimized::getHumidity(uint32_t dt) {
    if (dt == 0) dt = I.currentTime;
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].humidity;
}

int16_t WeatherInfoOptimized::getWeatherID(uint32_t dt) {
    if (dt == 0) dt = I.currentTime;
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].weatherID;
}

int8_t WeatherInfoOptimized::getPoP(uint32_t dt) {
    if (dt == 0) {
        double totalPOP = 1;
        int16_t start = hourSlot(I.currentTime);
        if (start < 0) start = 0;
        for (int16_t j = start; j < start + 24 && j < NUM_HOURLY; j++) {
            double tmp = hourly[j].PoP;
            if (tmp < 0) tmp = 0;
            totalPOP *= (1 - (tmp / 100));
        }
        return (int8_t)((1 - totalPOP) * 100);
    }
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].PoP;
}

int16_t WeatherInfoOptimized::getRain(uint32_t dt) {
    if (dt == 0) {
        int16_t totalrain = 0;
        int16_t start = hourSlot(I.currentTime);
        if (start < 0) start = 0;
        for (int16_t j = start; j < start + 24 && j < NUM_HOURLY; j++) {
            int16_t tmp = hourly[j].rainMm;
            if (tmp < 0) tmp = 0;
            totalrain += tmp;
        }
        return totalrain;
    }
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].rainMm;
}

int16_t WeatherInfoOptimized::getSnow(uint32_t dt) {
    if (dt == 0) {
        int16_t total = 0;
        int16_t start = hourSlot(I.currentTime);
        if (start < 0) start = 0;
        for (int16_t j = start; j < start + 24 && j < NUM_HOURLY; j++) {
            int16_t tmp = hourly[j].snowMm;
            if (tmp < 0) tmp = 0;
            total += tmp;
        }
        return total;
    }
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].snowMm;
}

int16_t WeatherInfoOptimized::getIce(uint32_t dt) {
    if (dt == 0) {
        int16_t total = 0;
        int16_t start = hourSlot(I.currentTime);
        if (start < 0) start = 0;
        for (int16_t j = start; j < start + 24 && j < NUM_HOURLY; j++) {
            int16_t tmp = hourly[j].iceMm;
            if (tmp < 0) tmp = 0;
            total += tmp;
        }
        return total;
    }
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].iceMm;
}

int8_t WeatherInfoOptimized::getDewPoint(uint32_t dt) {
    if (dt == 0) dt = I.currentTime;
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].dewPoint;
}

int16_t WeatherInfoOptimized::getWindSpeed(uint32_t dt) {
    if (dt == 0) dt = I.currentTime;
    int16_t i = hourSlot(dt);
    if (i < 0) return WEATHER_INVALID_TEMP;
    return hourly[i].windSpeed;
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
    if (icon.indexOf("smoke",0)>-1) return 761;
    if (icon.indexOf("haze",0)>-1) return 761;

    if (icon.indexOf("hot",0)>-1) return 702;
    if (icon.indexOf("cold",0)>-1) return 701;
    
    if (icon.indexOf("ovc",0)>-1) return 804; //8/8
    if (icon.indexOf("bkn",0)>-1) return 803; //5-7/8
    if (icon.indexOf("sct",0)>-1) return 802; //3-4/8
    if (icon.indexOf("few",0)>-1) return 801; //1-2/8
    if (icon.indexOf("skc",0)>-1) return 800; //0/8
    if (icon.indexOf("clr",0)>-1) return 800;

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
        case 702: strcpy(weathername, "Hot"); return true;
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


void WeatherInfoOptimized::initHourlyForecastData() {
    hourBase = 0;
    for (uint16_t i = 0; i < NUM_HOURLY; i++) {
        hourly[i].invalidate();
    }
}

void WeatherInfoOptimized::initHourlyGridData() {
    for (uint16_t i = 0; i < NUM_HOURLY; i++) {
        hourly[i].wetBulb = WEATHER_INVALID_TEMP;
        hourly[i].rainMm = WEATHER_INVALID_TEMP;
        hourly[i].iceMm = WEATHER_INVALID_TEMP;
        hourly[i].snowMm = WEATHER_INVALID_TEMP;
    }
    flag_rain = false;
    flag_ice = false;
    flag_snow = false;
}

void WeatherInfoOptimized::initDailyPeriodData() {
    periodBaseStart = 0;
    periodAnchorDayOffset = 0;
    periodBaseIsDaytime = true;
    firstFilledPeriod = 0;
    for (uint16_t i = 0; i < NUM_PERIODS; i++) {
        periods[i].invalidate();
    }
}

void WeatherInfoOptimized::initSunTimes() {
    sunrise = 0;
    sunset = 0;
}

bool WeatherInfoOptimized::initWeather() {
    initHourlyForecastData();
    initDailyPeriodData();
    initSunTimes();

    lastUpdateT = 0;
    lastUpdateAttempt = 0;
    fetchedAt = 0;

    return true;
}

uint16_t WeatherInfoOptimized::getDailyRain(uint8_t daysfromnow) {
    uint32_t MN0 = unixToLocal(makeUnixTime(year() - 2000, month(), day(), 0, 0, 0, false));
    return getDailyRain(MN0 + daysfromnow * 86400UL, MN0 + daysfromnow * 86400UL + 86400UL);
}

uint16_t WeatherInfoOptimized::getDailyRain(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    if (!isHourlyValid()) return 0;
    for (uint16_t j = 0; j < NUM_HOURLY; j++) {
        time_t ht = hourTime((int16_t)j);
        if (ht >= starttime && ht < endtime) {
            int16_t tmp = hourly[j].rainMm;
            if (tmp > 0) total += (uint16_t)tmp;
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailySnow(uint8_t daysfromnow) {
    uint32_t MN0 = unixToLocal(makeUnixTime(year() - 2000, month(), day(), 0, 0, 0, false));
    return getDailySnow(MN0 + daysfromnow * 86400UL, MN0 + daysfromnow * 86400UL + 86400UL);
}

uint16_t WeatherInfoOptimized::getDailySnow(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    if (!isHourlyValid()) return 0;
    for (uint16_t j = 0; j < NUM_HOURLY; j++) {
        time_t ht = hourTime((int16_t)j);
        if (ht >= starttime && ht < endtime) {
            int16_t tmp = hourly[j].snowMm;
            if (tmp > 0) total += (uint16_t)tmp;
        }
    }
    return total;
}

uint16_t WeatherInfoOptimized::getDailyIce(uint8_t daysfromnow) {
    uint32_t MN0 = unixToLocal(makeUnixTime(year() - 2000, month(), day(), 0, 0, 0, false));
    return getDailyIce(MN0 + daysfromnow * 86400UL, MN0 + daysfromnow * 86400UL + 86400UL);
}

uint16_t WeatherInfoOptimized::getDailyIce(uint32_t starttime, uint32_t endtime) {
    uint16_t total = 0;
    if (!isHourlyValid()) return 0;
    for (uint16_t j = 0; j < NUM_HOURLY; j++) {
        time_t ht = hourTime((int16_t)j);
        if (ht >= starttime && ht < endtime) {
            int16_t tmp = hourly[j].iceMm;
            if (tmp > 0) total += (uint16_t)tmp;
        }
    }
    return total;
}

uint8_t WeatherInfoOptimized::getDailyPoP(uint8_t daysfromnow, bool indays) {
    (void)indays;
    if (daysfromnow >= NUMWTHRDAYS) return 255;

    int16_t daySlot = getPeriodSlotForDaysFromToday(daysfromnow, true);
    if (daySlot >= 0 && periods[daySlot].isValid()) {
        return periods[daySlot].PoP;
    }
    int16_t nightSlot = getPeriodSlotForDaysFromToday(daysfromnow, false);
    if (nightSlot >= 0 && periods[nightSlot].isValid()) {
        return periods[nightSlot].PoP;
    }
    return 0;
}

int16_t WeatherInfoOptimized::getDailyWeatherID(uint8_t daysfromnow, bool indays) {
    (void)indays;
    if (daysfromnow >= NUMWTHRDAYS) return 255;

    int16_t daySlot = getPeriodSlotForDaysFromToday(daysfromnow, true);
    if (daySlot >= 0 && periods[daySlot].isValid()) {
        return periods[daySlot].weatherID;
    }
    int16_t nightSlot = getPeriodSlotForDaysFromToday(daysfromnow, false);
    if (nightSlot >= 0 && periods[nightSlot].isValid()) {
        return periods[nightSlot].weatherID;
    }
    return WEATHER_UNKNOWN_ID;
}

void WeatherInfoOptimized::getDailyTemp(uint8_t daysfromnow, int8_t* temp) {
    temp[0] = WEATHER_INVALID_TEMP;
    temp[1] = WEATHER_INVALID_TEMP;
    if (daysfromnow >= NUMWTHRDAYS) {
        return;
    }

    int16_t daySlot = getPeriodSlotForDaysFromToday(daysfromnow, true);
    int16_t nightSlot = getPeriodSlotForDaysFromToday(daysfromnow, false);

    if (daySlot >= 0 && periods[daySlot].isValid()) {
        temp[0] = periods[daySlot].temp;
    }
    if (nightSlot >= 0 && periods[nightSlot].isValid()) {
        temp[1] = periods[nightSlot].temp;
    }

    if (daysfromnow == 0 && temp[0] == WEATHER_INVALID_TEMP) {
        temp[0] = getTemperature(I.currentTime);
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
    // Accept any phenomenon we have a name for. getAlertName() is the single source of truth
    // for the full NWS P-VTEC code set (land, winter, tropical, coastal, and marine), so the
    // accept-list and the display names can never drift apart.
    if (phenomenon == nullptr) return false;
    return !getAlertName(phenomenon).equals("Unknown");
}

String WeatherInfoOptimized::getAlertName(const char* phenomenon) {
    if (phenomenon == nullptr) {
        //use the currently loaded alertinfo
        phenomenon = this->alertInfo.phenomenon;
        if (phenomenon == nullptr) return "Unknown";
    }

    // NWS P-VTEC phenomenon codes (https://www.weather.gov/vtec/). This map is the single
    // source of truth for "known" alerts: filterAlerts() accepts anything named here.

    // --- Convective / severe ---
    if (strcmp(phenomenon, "TO") == 0) return "Tornado";
    if (strcmp(phenomenon, "SV") == 0) return "Severe Thunderstorm";
    if (strcmp(phenomenon, "EW") == 0) return "Extreme Wind";
    if (strcmp(phenomenon, "SQ") == 0) return "Snow Squall";
    if (strcmp(phenomenon, "MA") == 0) return "Special Marine";

    // --- Flooding ---
    if (strcmp(phenomenon, "FF") == 0) return "Flash Flood";
    if (strcmp(phenomenon, "FA") == 0) return "Areal Flood";
    if (strcmp(phenomenon, "FL") == 0) return "Flood";
    if (strcmp(phenomenon, "HY") == 0) return "Hydrologic";
    if (strcmp(phenomenon, "CF") == 0) return "Coastal Flood";
    if (strcmp(phenomenon, "LS") == 0) return "Lakeshore Flood";
    if (strcmp(phenomenon, "SS") == 0) return "Storm Surge";

    // --- Tropical ---
    if (strcmp(phenomenon, "HU") == 0) return "Hurricane";
    if (strcmp(phenomenon, "TY") == 0) return "Typhoon";
    if (strcmp(phenomenon, "TR") == 0) return "Tropical Storm";
    if (strcmp(phenomenon, "HI") == 0) return "Inland Hurricane Wind";
    if (strcmp(phenomenon, "TI") == 0) return "Inland Tropical Storm Wind";
    if (strcmp(phenomenon, "HF") == 0) return "Hurricane Force Wind";
    if (strcmp(phenomenon, "TS") == 0) return "Tsunami";

    // --- Winter / cold ---
    if (strcmp(phenomenon, "WS") == 0) return "Winter Storm";
    if (strcmp(phenomenon, "WW") == 0) return "Winter Weather";
    if (strcmp(phenomenon, "IS") == 0) return "Ice Storm";
    if (strcmp(phenomenon, "BZ") == 0) return "Blizzard";
    if (strcmp(phenomenon, "ZR") == 0) return "Freezing Rain";
    if (strcmp(phenomenon, "ZF") == 0) return "Freezing Fog";
    if (strcmp(phenomenon, "LE") == 0) return "Lake Effect Snow";
    if (strcmp(phenomenon, "WC") == 0) return "Wind Chill";
    if (strcmp(phenomenon, "EC") == 0) return "Extreme Cold";
    if (strcmp(phenomenon, "FZ") == 0) return "Freeze";
    if (strcmp(phenomenon, "FR") == 0) return "Frost";
    if (strcmp(phenomenon, "HZ") == 0) return "Hard Freeze";
    if (strcmp(phenomenon, "UP") == 0) return "Heavy Freezing Spray";

    // --- Heat ---
    if (strcmp(phenomenon, "EH") == 0) return "Excessive Heat";
    if (strcmp(phenomenon, "HT") == 0) return "Heat";

    // --- Wind ---
    if (strcmp(phenomenon, "HW") == 0) return "High Wind";
    if (strcmp(phenomenon, "WI") == 0) return "Wind";
    if (strcmp(phenomenon, "LW") == 0) return "Lake Wind";

    // --- Visibility / air quality / fire ---
    if (strcmp(phenomenon, "FG") == 0) return "Dense Fog";
    if (strcmp(phenomenon, "SM") == 0) return "Dense Smoke";
    if (strcmp(phenomenon, "DS") == 0) return "Dust Storm";
    if (strcmp(phenomenon, "DU") == 0) return "Blowing Dust";
    if (strcmp(phenomenon, "AS") == 0) return "Air Stagnation";
    if (strcmp(phenomenon, "AQ") == 0) return "Air Quality";
    if (strcmp(phenomenon, "AF") == 0) return "Ashfall";
    if (strcmp(phenomenon, "FW") == 0) return "Fire Weather";

    // --- Coastal / beach ---
    if (strcmp(phenomenon, "SU") == 0) return "High Surf";
    if (strcmp(phenomenon, "RP") == 0) return "Rip Current";
    if (strcmp(phenomenon, "BH") == 0) return "Beach Hazard";

    // --- Marine ---
    if (strcmp(phenomenon, "GL") == 0) return "Gale";
    if (strcmp(phenomenon, "SR") == 0) return "Storm";
    if (strcmp(phenomenon, "SE") == 0) return "Hazardous Seas";
    if (strcmp(phenomenon, "SC") == 0) return "Small Craft";
    if (strcmp(phenomenon, "SW") == 0) return "Small Craft (Hazardous Seas)";
    if (strcmp(phenomenon, "RB") == 0) return "Small Craft (Rough Bar)";
    if (strcmp(phenomenon, "SI") == 0) return "Small Craft (Winds)";
    if (strcmp(phenomenon, "BW") == 0) return "Brisk Wind";
    if (strcmp(phenomenon, "LO") == 0) return "Low Water";
    if (strcmp(phenomenon, "MF") == 0) return "Marine Dense Fog";
    if (strcmp(phenomenon, "MS") == 0) return "Marine Dense Smoke";
    if (strcmp(phenomenon, "MH") == 0) return "Marine Ashfall";

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
    *end   = unixToLocal(vtecTimeToUnix(vtec + 35));

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
        String extraHeaders = "User-Agent: (ArborysWeatherProject, contact@yourdomain.com)\nAccept: application/geo+json\n";
        extraHeaders += "If-Modified-Since: " + String(this->lastAlertFetchTime) + "\n";


        HTTPMessage M;
        M.setUrl(url);
        M.setMethod("GET");
        M.setContentType("application/json");
        M.setCacert("bundle");
        M.timeout = WEATHER_HTTP_TIMEOUT_MS;
        M.usePSRAM = true;
        M.responseDoc = &doc;
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
            deleteFiles("*", "/Data/Events");    
            //clear bits 0,2,3
            clearBit(I.WeatherEventFlags, 0); //bit 0 is for flagged alerts
            //bit 1 is for last alert, no longer used
            clearBit(I.WeatherEventFlags, 2); //bit 2 is for severe alerts
            clearBit(I.WeatherEventFlags, 3); //bit 3 is for imminent alerts

            this->alertInfo.initAlertInfo();
    
            JsonArray features = doc["features"].as<JsonArray>();
            
            if (features.size() == 0) {
                //no alerts found
                //clean up
            } else {

                for (JsonObject feature : features) {
                    uint16_t currentEventNumber = this->NumWeatherEvents+1;

                    JsonObject props = feature["properties"];
                    const char* vtecRaw = props["parameters"]["VTEC"][0]; // VTEC is usually the first element
                    
                    if (vtecRaw == nullptr) continue;

                    char off[5], phen[3], sig[2], etnStr[5];
                    time_t tStart, tEnd;
                    if (!parseVTEC(vtecRaw, off, phen, sig, etnStr, &tStart, &tEnd)) continue;

                    if (!filterAlerts(phen)) continue;

                    //set event bit one, bit 0 is for flagged alerts are present                    
                    setBit(I.WeatherEventFlags, 0);

                    WeatherEventFile fileData;
                    memset(&fileData, 0, sizeof(WeatherEventFile));

                    const char* severity = props["severity"].as<const char*>();
                    strncpy(fileData.severity, severity ? severity : "", sizeof(fileData.severity) - 1);
                    fileData.severity[sizeof(fileData.severity) - 1] = '\0';

                    const char* certainty = props["certainty"].as<const char*>();
                    strncpy(fileData.certainty, certainty ? certainty : "", sizeof(fileData.certainty) - 1);
                    fileData.certainty[sizeof(fileData.certainty) - 1] = '\0';

                    const char* urgency = props["urgency"].as<const char*>();
                    strncpy(fileData.urgency, urgency ? urgency : "", sizeof(fileData.urgency) - 1);
                    fileData.urgency[sizeof(fileData.urgency) - 1] = '\0';

                    fileData.time_start = tStart;
                    fileData.time_end = tEnd;
                    strncpy(fileData.office, off, 4);
                    strncpy(fileData.phenomenon, phen, 2);
                    strncpy(fileData.significance, sig, 1);                    
                    fileData.event_number = (uint16_t)atoi(etnStr);

                    const char* ev = props["event"].as<const char*>();
                    strncpy(fileData.event, ev ? ev : "", sizeof(fileData.event) - 1);
                    fileData.event[sizeof(fileData.event) - 1] = '\0';

                    const char* hl = props["headline"].as<const char*>();
                    strncpy(fileData.headline, hl ? hl : "", sizeof(fileData.headline) - 1);
                    fileData.headline[sizeof(fileData.headline) - 1] = '\0';

                    const char* desc = props["description"].as<const char*>();
                    strncpy(fileData.description, desc ? desc : "", sizeof(fileData.description) - 1);
                    fileData.description[sizeof(fileData.description) - 1] = '\0';

                    char filename[22];
                    snprintf(filename, 21, "/Data/Events/%d.txt", 
                        currentEventNumber);

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
                        this->alertInfo.eventnumber = currentEventNumber;
                        strncpy(this->alertInfo.event, fileData.event, 32);

                        if (eseverity == WeatherSeverity::EXTREME || eseverity == WeatherSeverity::SEVERE) setBit(I.WeatherEventFlags, 2); //set bit 2 to 1
                        if (eurgency == WeatherUrgency::IMMINENT) setBit(I.WeatherEventFlags, 3); //set bit 3 to 1
                    }


                    if (!writeAnythingToSD(filename, &fileData, sizeof(WeatherEventFile))) {
                        SerialPrint("fetchWeatherAlerts: Could not write weather event file: " + String(filename), true);
                        storeError("fetchWeatherAlerts: Could not write weather event file");
                        continue;
                    }

                    this->NumWeatherEvents = currentEventNumber;
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
    snprintf(filename, 31, "/Data/Events/%d.txt", 
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
