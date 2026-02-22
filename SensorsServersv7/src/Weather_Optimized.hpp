#ifdef _USEWEATHER
#ifndef WEATHER_OPTIMIZED_HPP
#define WEATHER_OPTIMIZED_HPP


#include "globals.hpp"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>


#define NUMWTHRDAYS 5
#define WEATHER_CACHE_SIZE 3
#define MAX_RETRY_ATTEMPTS 3
//#define PARALLEL_REQUESTS_ENABLED 1


struct TimeInterval {
    time_t start;
    uint32_t duration;
};


// Weather data cache entry
struct WeatherCacheEntry {
    uint32_t timestamp;
    uint32_t grid_x;
    uint32_t grid_y;
    char grid_id[10];
    bool valid;
    uint32_t last_successful_update;
};

enum WeatherSeverity : byte { UNKNOWN, MINOR, MODERATE, SEVERE, EXTREME };
enum WeatherCertainty : byte { UNCERTAIN, UNLIKELY, POSSIBLE, LIKELY, OBSERVED };
enum WeatherUrgency : byte { INDETERMINATE, PAST, FUTURE, EXPECTED, IMMINENT };

struct WeatherAlerts {
    uint8_t eventnumber;
    WeatherSeverity E_severity;
    WeatherCertainty E_certainty;
    WeatherUrgency E_urgency;
    char phenomenon[3]; //for example WW = winter weather
    char filename[22];
    int16_t iconID;
    time_t time_start;
    time_t time_end;
};

struct WeatherEventFile {
    char severity[10];
    char certainty[10];
    char urgency[10];
    int16_t iconID;
    time_t time_start;
    time_t time_end;
    char office[5]; //NWS office that issued the alert
    char phenomenon[3]; //for example WW = winter weather
    char significance[2]; //for example W = watch, A = advisory, etc
    uint16_t event_number; //ETN number of the event, if more than one are active. The filename will use this number. 4 digits long
    char event[100]; //event description
    char headline[200]; //headline of the event
    char description[1200]; //description of the event
};

// Optimized WeatherInfo class - maintains same interface as original
class WeatherInfoOptimized {
private:
    // Core data arrays (same as original WeatherInfo)
    uint32_t dT[NUMWTHRDAYS*24] = {0};
    int8_t temperature[NUMWTHRDAYS*24] = {0};
    int8_t humidity[NUMWTHRDAYS*24] = {0};
    int16_t weatherID[NUMWTHRDAYS*24] = {0};
    int8_t PoP[NUMWTHRDAYS*24] = {0};
    int8_t dewPoint[NUMWTHRDAYS*24] = {0};
    int16_t windspeed[NUMWTHRDAYS*24] = {0};
    int8_t wetBulbTemperature[NUMWTHRDAYS*24] = {0};
    int16_t rainmm[NUMWTHRDAYS*24] = {0};
    int16_t icemm[NUMWTHRDAYS*24] = {0};
    int16_t snowmm[NUMWTHRDAYS*24] = {0};

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
    int16_t getIndex(time_t dT = 0);
    bool fetchGridCoordinates();
    bool fetchGridCoordinatesHelper();
    bool fetchHourlyForecast();
    bool fetchGridForecast();
    bool fetchDailyForecast();
    bool fetchSunriseSunset();
    bool fetchWeatherAlerts();

    // Optimized HTTP methods
    // cert_path: SD path (e.g. "/Certificates/NOAA.crt") or "" / "*" for embedded CA bundle (sdkconfig.defaults)
//    bool makeSecureRequest(const String& url, JsonDocument& doc, int& httpCode, ERRORCODES HTTP, ERRORCODES JSON, const char* cert_path= "/Certificates/NOAA.crt");
    bool makeSecureRequest(String& url, JsonDocument& doc, int16_t& httpCode, ERRORCODES HTTP, ERRORCODES JSON, String& extraHeaders, const char* cert_path= "",  const JsonDocument* filter=nullptr);
    
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
                                 int16_t* data_array, bool& flag);
    
    // Error handling and recovery
    bool handleApiError(const String& operation, int httpCode);
    bool retryWithBackoff(const String& operation, std::function<bool()> request_func);
    
    // Memory optimization
    void optimizeJsonDocument(JsonDocument& doc);
    void clearUnusedData();

    bool filterAlerts(const char* phenomenon);
    WeatherSeverity parseSeverity(const char* s);
    WeatherCertainty parseCertainty(const char* s);
    WeatherUrgency parseUrgency(const char* s);
    bool parseVTEC(const char* vtec, char* office, char* phenomenon, char* significance, char* etn, time_t* start, time_t* end);
    time_t vtecTimeToUnix(const char* vtecPart, bool asLocalTime);
public:
    WeatherAlerts alertInfo;
    uint8_t NumWeatherEvents=0;
    uint32_t lastAlertFetchTime = 0;

    uint32_t lastUpdateT = 0;
    uint32_t lastUpdateAttempt = 0;
    uint32_t lastUpdateError = 0;
    uint32_t sunrise;
    uint32_t sunset;
    bool flag_rain;
    bool flag_snow;
    bool flag_ice;
    // lat, lon removed - now using Prefs.LATITUDE and Prefs.LONGITUDE throughout

    // Constructor
    WeatherInfoOptimized();
    
    // Core methods (same interface as original WeatherInfo)
    uint32_t nextPrecip();
    uint32_t nextRain();
    uint32_t nextSnow();
    
    int8_t getTemperature(uint32_t dt=0, bool wetbulb=false, bool asindex=false);
    int8_t getHumidity(uint32_t dt=0);
    int16_t getWeatherID(uint32_t dt=0);
    int8_t getPoP(uint32_t dt=0);
    int16_t getRain(uint32_t dt=0);
    int16_t getSnow(uint32_t dt=0);
    int16_t getIce(uint32_t dt=0);
    int8_t getDewPoint(uint32_t dt);
    int16_t getWindSpeed(uint32_t dt);

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

    TimeInterval parseNOAAInterval(String tm);
    int16_t breakIconLink(String icon, TimeInterval ti);
    bool nameWeatherIcon(uint16_t icon, char* weathername);
    String nameWeatherIcon(uint16_t icon);
    bool updateWeather(uint16_t synctime = 3600);
    bool initWeather();
    
    // New optimization methods
    byte updateWeatherOptimized(uint16_t synctime = 3600);
    void getPerformanceStats(uint32_t& total_calls, uint32_t& failed_calls, uint32_t& avg_response_time);
    bool clearCache();
    bool isCacheValid();
    
    // Memory management
    void optimizeMemoryUsage();
    size_t getMemoryUsage();

    bool loadNextWeatherAlert();
    bool getWeatherAlertDetails(String& event, String& headline, String& description, String& severity, String& certainty, String& urgency);

    
    // Grid coordinates access (for compatibility)
    int16_t getGridX() const { return Grid_x; }
    int16_t getGridY() const { return Grid_y; }
    const char* getGridId() const { return Grid_id; }
    
    
};

extern String WEBHTML;

#endif 
#endif
