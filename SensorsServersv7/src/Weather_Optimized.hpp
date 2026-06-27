#ifdef _USEWEATHER
#ifndef WEATHER_OPTIMIZED_HPP
#define WEATHER_OPTIMIZED_HPP


#include "globals.hpp"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>


#ifdef _USESDCARD

#include "SDCard.hpp"
#endif


#define NUMWTHRDAYS 6
#define NUM_HOURLY (NUMWTHRDAYS * 24)
#define NUM_PERIODS (NUMWTHRDAYS * 2)
#define WEATHER_CACHE_SIZE 3
#define MAX_RETRY_ATTEMPTS 3
#define WEATHER_HTTP_TIMEOUT_MS 60000
#define WEATHER_HTTP_TIMEOUT_SHORT_MS 30000
#define WEATHER_STORE_VERSION 3
#define WEATHER_INVALID_TEMP -120
#define WEATHER_UNKNOWN_ID 999
//#define PARALLEL_REQUESTS_ENABLED 1

struct HourlySlot {
    int8_t temperature = WEATHER_INVALID_TEMP;
    int8_t humidity = 0;
    int8_t PoP = 0;
    int8_t dewPoint = WEATHER_INVALID_TEMP;
    int16_t weatherID = WEATHER_UNKNOWN_ID;
    int16_t windSpeed = WEATHER_INVALID_TEMP;
    int8_t wetBulb = WEATHER_INVALID_TEMP;
    int16_t rainMm = WEATHER_INVALID_TEMP;
    int16_t iceMm = WEATHER_INVALID_TEMP;
    int16_t snowMm = WEATHER_INVALID_TEMP;

    void invalidate() {
        temperature = WEATHER_INVALID_TEMP;
        humidity = 0;
        PoP = 0;
        dewPoint = WEATHER_INVALID_TEMP;
        weatherID = WEATHER_UNKNOWN_ID;
        windSpeed = WEATHER_INVALID_TEMP;
        wetBulb = WEATHER_INVALID_TEMP;
        rainMm = WEATHER_INVALID_TEMP;
        iceMm = WEATHER_INVALID_TEMP;
        snowMm = WEATHER_INVALID_TEMP;
    }

    bool isValid() const { return temperature != WEATHER_INVALID_TEMP; }
};

struct DailyPeriodSlot {
    uint32_t start = 0;
    uint32_t end = 0;
    int8_t temp = WEATHER_INVALID_TEMP;
    int16_t weatherID = WEATHER_UNKNOWN_ID;
    uint8_t PoP = 0;
    bool isDaytime = false;

    void invalidate() {
        start = 0;
        end = 0;
        temp = WEATHER_INVALID_TEMP;
        weatherID = WEATHER_UNKNOWN_ID;
        PoP = 0;
        isDaytime = false;
    }

    bool isValid() const { return start != 0; }
};


/*
struct PsramAllocator {
    void* allocate(size_t size) { return ps_malloc(size); }
    void deallocate(void* ptr) { free(ptr); }
    void* reallocate(void* ptr, size_t new_size) { return ps_realloc(ptr, new_size); }
  };
  
  // Type alias for an ArduinoJson 7 doc in PSRAM
  using PsramJsonDocument = basicJsonDocument<PsramAllocator>;

struct WeatherResult {
    char* rawBuffer = nullptr; 
    PsramJsonDocument* doc = nullptr;
    size_t PSRAM_size;


    bool initialize(size_t buffer_size=100*1024) {

        if (this->isValid()) return false; //already initialized

        this->rawBuffer = (char*)ps_malloc(buffer_size);
        this->PSRAM_size = buffer_size;
        if (!this->rawBuffer) return false; //failed to allocate

        this->doc = new PsramJsonDocument();
        if (!this->doc) return false; //failed to allocate

        return true;
    }

    DeserializationError deserialize() {
        //zero copy is used here, so the json string is not copied to the PSRAM buffer
        return deserializeJson(*(this->doc), this->rawBuffer);
    }

    // The key to Zero-Copy is keeping BOTH alive until this is called
    void cleanup() {
        if (doc) {
            delete doc; 
            doc = nullptr;
        }
        if (rawBuffer) {
            free(rawBuffer);
            rawBuffer = nullptr;
            this->PSRAM_size = 0;
        }
    }

    // Check if we actually have data
    bool isValid() {
        return (doc != nullptr && rawBuffer != nullptr);
    }
};
*/

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

struct WeatherAlertSummary {
    uint8_t eventnumber;
    WeatherSeverity E_severity;
    WeatherCertainty E_certainty;
    WeatherUrgency E_urgency;
    char phenomenon[3]; //for example WW = winter weather
    int16_t iconID;
    time_t time_start;
    time_t time_end;
    char event[33];

    // Constructor: Runs automatically when the struct is created
    WeatherAlertSummary() {
        initAlertInfo();
    }

    void initAlertInfo() {
        this->eventnumber = 0;
        this->E_severity = WeatherSeverity::UNKNOWN;
        this->E_certainty = WeatherCertainty::UNCERTAIN;
        this->E_urgency = WeatherUrgency::INDETERMINATE;
        memset(this->phenomenon, 0, sizeof(this->phenomenon));
        this->iconID = 0;
        this->time_start = 0;
        this->time_end = 0;
        memset(this->event, 0, sizeof(this->event));
    }


};

struct dailyWeatherForecast {
    uint32_t dT_start;
    uint32_t dT_end;
    int8_t tempF;
    int16_t weatherID;
    bool isDaytime;
    uint8_t PoP;
    char details[400];

    // Constructor: Runs automatically when the struct is created
    dailyWeatherForecast() {
        initDailyWeatherForecast();
    }

    void initDailyWeatherForecast() {
        this->dT_start = 0;
        this->dT_end = 0;
        this->tempF = 0;
        this->weatherID = 0;
        this->isDaytime = false;
        this->PoP = 0;
        memset(this->details, 0, sizeof(this->details));
    }

    bool getPeriod(uint8_t periodNumber) {
        char filename[26];
        snprintf(filename, 25, "/Data/DailyWthr/%d.bin", periodNumber);
        return readAnythingFromSD(filename, this, sizeof(*this));
    }
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

    // Constructor: Runs automatically when the struct is created
    WeatherEventFile() {
        initWeatherEventFile();
    }

    void initWeatherEventFile() {
        this->iconID = 0;
        this->time_start = 0;
        this->time_end = 0;
        this->office[0] = '\0';
        this->phenomenon[0] = '\0';
        this->significance[0] = '\0';
        this->event_number = 0;
        memset(this->severity, 0, sizeof(this->severity));
        memset(this->certainty, 0, sizeof(this->certainty));
        memset(this->urgency, 0, sizeof(this->urgency));
        memset(this->event, 0, sizeof(this->event));
        memset(this->headline, 0, sizeof(this->headline));
        memset(this->description, 0, sizeof(this->description));
    }

    bool getEvent(uint16_t eventNumber) {
        char filename[26];
        snprintf(filename, 25, "/Data/Events/%d.txt", eventNumber);
        return readAnythingFromSD(filename, this, sizeof(*this));
    }
};

// Optimized WeatherInfo class - maintains same interface as original
class WeatherInfoOptimized {
private:
    uint32_t hourBase = 0;
    HourlySlot hourly[NUM_HOURLY];

    uint32_t periodBaseStart = 0;
    int32_t periodAnchorDayOffset = 0;
    bool periodBaseIsDaytime = true;
    uint8_t firstFilledPeriod = 0;
    DailyPeriodSlot periods[NUM_PERIODS];

    char Grid_id[10] = "";
    int16_t Grid_x = 0;
    int16_t Grid_y = 0;

    // Optimization features
    //WeatherResult weatherResult; //memory storage for json trees
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
    
    // Per-domain init (full reset via initWeather() only)
    void initHourlyForecastData();
    void initHourlyGridData();
    void initDailyPeriodData();
    void initSunTimes();

    // Helper methods
    void initAlertInfo();
    static uint32_t localMidnightToday();
    static int32_t nwsDayOffsetFromMidnight(time_t t);
    static time_t floorToHour(time_t t);
    void ensureHourBaseFromTimestamp(time_t t);
    bool isHourlyValid() const { return hourBase != 0; }
    int16_t hourSlot(time_t targetTime) const;
    time_t hourTime(int16_t slot) const;
    int16_t periodSlotForDayDiff(int32_t dayDiff, bool wantDaytime) const;
    void setupPeriodAnchor(time_t firstPeriodStart, bool firstIsDaytime);
    bool fetchGridCoordinates();
    bool fetchGridCoordinatesHelper();
    bool fetchHourlyForecast();
    bool fetchGridForecast();
    bool fetchDailyForecast();
    bool fetchSunriseSunset();
    bool fetchWeatherAlerts();

    
    // Caching methods
    bool isGridCoordinatesValid();
    void updateGridCoordinatesCache();
    bool loadFromCache();
    void saveToCache();
    
    // Data processing optimizations
    bool processHourlyData(JsonObject& properties);
    bool processGridData(JsonObject& properties);
    bool processDailyData(JsonObject& properties);
    enum class PrecipField : uint8_t { Rain, Ice, Snow };
    void processPrecipitationData(JsonObject& properties, const char* field_name,
                                 PrecipField field, bool& flag);
    
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
    time_t vtecTimeToUnix(const char* vtecPart);
public:
    WeatherAlertSummary alertInfo;
    uint8_t NumWeatherEvents=0;
    uint32_t lastAlertFetchTime = 0;
    uint32_t lastAlertUpdateTime = 0;

    uint32_t lastUpdateT = 0;
    uint32_t lastUpdateAttempt = 0;
    uint32_t lastUpdateError = 0;
    uint32_t fetchedAt = 0;
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

    int16_t getHourSlot(time_t t) const { return hourSlot(t); }
    int16_t getPeriodSlotForDaysFromToday(uint8_t daysfromnow, bool wantDaytime) const;

    int16_t getDailyWeatherID(uint8_t daysfromnow, bool indays=false);
    void getDailyTemp(uint8_t daysfromnow, int8_t* temp);
    uint8_t getDailyPoP(uint8_t daysfromnow, bool indays=false);
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
    byte updateWeatherOptimized(uint16_t synctime = 3600, bool setupProgress = false);
    void getPerformanceStats(uint32_t& total_calls, uint32_t& failed_calls, uint32_t& avg_response_time);
    bool clearCache();
    bool isCacheValid();
    
    // Memory management
    void optimizeMemoryUsage();
    size_t getMemoryUsage();

    String getAlertName(const char* phenomenon = nullptr);
    bool loadNextWeatherAlert();
    
    
    // Grid coordinates access (for compatibility)
    int16_t getGridX() const { return Grid_x; }
    int16_t getGridY() const { return Grid_y; }
    const char* getGridId() const { return Grid_id; }
    
    
};

extern String WEBHTML;

#endif 
#endif
