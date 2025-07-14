#ifndef WEATHER_OPTIMIZED_HPP
#define WEATHER_OPTIMIZED_HPP

#include <Arduino.h>
#include <globals.hpp>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>

#ifdef _DEBUG
extern uint16_t TESTRUN;
#endif

#define NUMWTHRDAYS 5
#define WEATHER_CACHE_SIZE 3
#define MAX_RETRY_ATTEMPTS 3
#define PARALLEL_REQUESTS_ENABLED 1

// Weather data cache entry
struct WeatherCacheEntry {
    uint32_t timestamp;
    uint32_t grid_x;
    uint32_t grid_y;
    char grid_id[10];
    bool valid;
    uint32_t last_successful_update;
};

// Optimized WeatherInfo class - maintains same interface as original
class WeatherInfoOptimized {
private:
    // Core data arrays (same as original WeatherInfo)
    uint32_t dT[NUMWTHRDAYS*24] = {0};
    int8_t temperature[NUMWTHRDAYS*24] = {0};
    uint8_t humidity[NUMWTHRDAYS*24] = {0};
    int16_t weatherID[NUMWTHRDAYS*24] = {0};
    uint8_t PoP[NUMWTHRDAYS*24] = {0};
    int8_t dewPoint[NUMWTHRDAYS*24] = {0};
    uint8_t windspeed[NUMWTHRDAYS*24] = {0};
    int8_t wetBulbTemperature[NUMWTHRDAYS*24] = {0};
    uint8_t rainmm[NUMWTHRDAYS*24] = {0};
    uint8_t icemm[NUMWTHRDAYS*24] = {0};
    uint8_t snowmm[NUMWTHRDAYS*24] = {0};

    uint32_t daily_dT[14] = {0};
    int8_t daily_tempMax[14] = {0};
    int8_t daily_tempMin[14] = {0};
    int16_t daily_weatherID[14] = {0};
    uint8_t daily_PoP[14] = {0};
    char Grid_id[10] = "";
    int16_t Grid_x = 0;
    int16_t Grid_y = 0;

    // Optimization features
    WeatherCacheEntry cache[WEATHER_CACHE_SIZE];
    uint8_t cache_index;
    bool grid_coordinates_cached;
    char cached_grid_id[10];
    int16_t cached_grid_x;
    int16_t cached_grid_y;
    
    // Performance tracking
    uint32_t last_api_call_time;
    uint32_t total_api_calls;
    uint32_t failed_api_calls;
    uint32_t average_response_time;
    
    // Helper methods
    void InitWeather();
    uint8_t getIndex(time_t dT = 0);
    bool fetchGridCoordinates();
    bool fetchGridCoordinatesHelper();
    bool fetchHourlyForecast();
    bool fetchGridForecast();
    bool fetchDailyForecast();
    bool fetchSunriseSunset();
    
    // Optimized HTTP methods
    bool makeSecureRequest(const String& url, JsonDocument& doc, int& httpCode, 
                          const char* cert_path = "/Certificates/NOAA.crt");
    bool makeParallelRequests();
    
    // Caching methods
    bool isGridCoordinatesValid();
    void updateGridCoordinatesCache();
    bool loadFromCache();
    void saveToCache();
    
    // Data processing optimizations
    bool processHourlyData(JsonObject& properties);
    bool processGridData(JsonObject& properties);
    bool processDailyData(JsonObject& properties);
    void processPrecipitationData(JsonObject& properties, const char* field_name, 
                                 uint8_t* data_array, bool& flag);
    
    // Error handling and recovery
    bool handleApiError(const String& operation, int httpCode);
    bool retryWithBackoff(const String& operation, std::function<bool()> request_func);
    
    // Memory optimization
    void optimizeJsonDocument(JsonDocument& doc);
    void clearUnusedData();

public:
    // Public interface (same as original WeatherInfo)
    uint32_t lastUpdateT = 0;
    uint32_t lastUpdateAttempt = 0;
    uint32_t sunrise;
    uint32_t sunset;
    bool flag_rain;
    bool flag_snow;
    bool flag_ice;
    double lat, lon;

    // Constructor
    WeatherInfoOptimized();
    
    // Core methods (same interface as original WeatherInfo)
    uint32_t nextPrecip();
    uint32_t nextRain();
    uint32_t nextSnow();
    
    int8_t getTemperature(uint32_t dt=0, bool wetbulb=false, bool asindex=false);
    uint8_t getHumidity(uint32_t dt=0);
    int16_t getWeatherID(uint32_t dt=0);
    uint8_t getPoP(uint32_t dt=0);
    uint16_t getRain(uint32_t dt=0);
    uint16_t getSnow(uint32_t dt=0);
    uint16_t getIce(uint32_t dt=0);
    int8_t getDewPoint(uint32_t dt);
    uint8_t getWindSpeed(uint32_t dt);

    int16_t getDailyWeatherID(uint8_t intsfromnow, bool indays=false);
    void getDailyTemp(uint8_t daysfromnow, int8_t* temp);
    uint8_t getDailyPoP(uint8_t intsfromnow, bool indays=false);
    uint16_t getDailyRain(uint8_t daysfromnow);
    uint16_t getDailyRain(uint32_t starttime, uint32_t endtime);
    uint16_t getDailySnow(uint8_t daysfromnow);
    uint16_t getDailySnow(uint32_t starttime, uint32_t endtime);
    uint16_t getDailyIce(uint8_t daysfromnow);
    uint16_t getDailyIce(uint32_t starttime, uint32_t endtime);
    String getGrid(uint8_t fc=0);

    time_t breakNOAATimestamp(String tm);
    int16_t breakIconLink(String icon, uint32_t starttime, uint32_t endtime);
    bool updateWeather(uint16_t synctime = 3600);
    bool initWeather();
    
    // New optimization methods
    bool updateWeatherOptimized(uint16_t synctime = 3600);
    void getPerformanceStats(uint32_t& total_calls, uint32_t& failed_calls, uint32_t& avg_response_time);
    bool clearCache();
    bool isCacheValid();
    
    // Memory management
    void optimizeMemoryUsage();
    size_t getMemoryUsage();
    
    // Grid coordinates access (for compatibility)
    int16_t getGridX() const { return Grid_x; }
    int16_t getGridY() const { return Grid_y; }
    const char* getGridId() const { return Grid_id; }
};

extern String WEBHTML;

#endif 