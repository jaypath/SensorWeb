#ifndef WEATHER_HPP
#define WEATHER_HPP

//#define _DEBUG 0
#include <Arduino.h>
#include <globals.hpp>

#ifdef _DEBUG
extern uint16_t TESTRUN;
#endif


#define NUMWTHRDAYS 5 //get hourly data for this many days

class WeatherInfo
{
private:
    //keep hourly for next 24 hours, and daily (really day and night, so 12 hour interval)
    uint32_t dT[NUMWTHRDAYS*24] = {0}; //time for each element
    int8_t temperature[NUMWTHRDAYS*24] = {0}; //degrees in F
    uint8_t humidity[NUMWTHRDAYS*24] = {0};
    int16_t weatherID[NUMWTHRDAYS*24] = {0};
    uint8_t PoP[NUMWTHRDAYS*24] = {0};
    int8_t dewPoint[NUMWTHRDAYS*24] = {0};
    uint8_t windspeed[NUMWTHRDAYS*24] = {0};
    int8_t wetBulbTemperature[NUMWTHRDAYS*24] = {0};
    uint8_t rainmm[NUMWTHRDAYS*24] = {0};
    uint8_t icemm[NUMWTHRDAYS*24] = {0};
    uint8_t snowmm[NUMWTHRDAYS*24] = {0};

    uint32_t daily_dT[14] = {0}; //time for daily elements
    int8_t daily_tempMax[14] = {0};
    int8_t daily_tempMin[14] = {0};
    int16_t daily_weatherID[14] = {0};
    uint8_t daily_PoP[14] = {0};
    char Grid_id[10] = ""; //station name... not sure how long this could be, but I've seen 3
    int16_t Grid_x = 0; //station name
    int16_t Grid_y = 0; //station name

    uint8_t getIndex(time_t dT = 0);
public:
    uint32_t lastUpdateT = 0; //last time an update succeeded
    uint32_t lastUpdateAttempt =0; //last time an update was attempted
    
    uint32_t sunrise; //next  sunrise for TODAY
    uint32_t sunset; //next  sunset for TODAY
    bool flag_rain;
    bool flag_snow;
    bool flag_ice;

    double lat,lon;    

    uint32_t nextPrecip();
    uint32_t nextRain();
    uint32_t nextSnow();
    
    int8_t getTemperature(uint32_t dt=0, bool wetbulb=false,bool asindex = false);    
    
    uint8_t getHumidity(uint32_t dt);
    int16_t getWeatherID(uint32_t dt);
    
    uint8_t getPoP(uint32_t dt=0);
    uint8_t getRain(uint32_t dt=0);
    uint8_t getSnow(uint32_t dt=0);
    uint8_t getIce(uint32_t dt=0);
    int8_t getDewPoint(uint32_t dt);
    uint8_t getWindSpeed(uint32_t dt);

    int16_t getDailyWeatherID(uint8_t intsfromnow,bool indays=false);
    void getDailyTemp(uint8_t daysfromnow, int8_t* temp);
    uint8_t getDailyPoP(uint8_t intsfromnow,bool indays=false);
    uint16_t getDailyRain(uint8_t daysfromnow);
    uint16_t getDailyRain(uint32_t starttime, uint32_t endtime);
    uint16_t getDailySnow(uint8_t daysfromnow);
    uint16_t getDailySnow(uint32_t starttime, uint32_t endtime);
    uint16_t getDailyIce(uint8_t daysfromnow);
    uint16_t getDailyIce(uint32_t starttime, uint32_t endtime);
    String getGrid(uint8_t fc=0);


    time_t breakNOAATimestamp(String tm);
    int16_t breakIconLink(String icon,uint32_t starttime, uint32_t endtime);
    bool updateWeather(uint16_t synctime = 3600);        
    bool initWeather();
    
};

extern String WEBHTML;


#endif
