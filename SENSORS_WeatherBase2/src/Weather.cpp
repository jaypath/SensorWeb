#include <Weather.hpp>
#include <ArduinoJson.h>
#include <timesetup.hpp>
#include <server.hpp>



uint8_t WeatherInfo::getIndex(time_t dT)
{
    if (dT == 0) dT = now();
    for (byte j=0;j<NUMWTHRDAYS*24;j++) {
        if  (this->dT[j] == dT) return j; //found the exact timestamp
        if (this->dT[j] > dT) { //found the index that is beyond this i value, so the correct index was teh one before
            if (j>0 ) j--;
            return j;
        }
    }
    return 255; //did not find index!
}

int8_t WeatherInfo::getTemperature(uint32_t dt, bool wetbulb,bool asindex)
{
    if (asindex) 
    {
        if (wetbulb) return this->wetBulbTemperature[dt];

        //if not wetbulb temp, then see if I should return local temp, which is index 0
        if (dt==0 && I.localWeather<255) {
            if (now()-Sensors[I.localWeather].timeLogged<1800) { //localweather is only useful if <30 minutes old 
                return I.currentTemp;
            }
        }

        return this->temperature[dt];
    }
    
    if (dt==0) dt = now();
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return -120;

    if (wetbulb) return this->wetBulbTemperature[i];
    else     return this->temperature[i];
}

uint8_t WeatherInfo::getHumidity(uint32_t dt)
{
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->humidity[i];

}
int16_t WeatherInfo::getWeatherID(uint32_t dt)
{

    //return hourly ID, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->weatherID[i];

}


uint8_t WeatherInfo::getPoP(uint32_t dt)
{
    //special request... dt is zero, then get the total pop for next 24h
    if (dt==0) {
        double totalPOP=1;
        for (byte j=0;j<24;j++) totalPOP *= (1 - (this->PoP[j]/100)); //this is the probability of NO precip
        return  (uint8_t) ((1- totalPOP)*100);
    }
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->PoP[i];

}
uint8_t WeatherInfo::getRain(uint32_t dt)
{
    //special request... dt is zero, then get the total rain for next 24h
    if (dt==0) {
        uint8_t totalrain = 0;
        for (byte j=0;j<24;j++) totalrain+=this->rainmm[j]; 
        return totalrain;
    }
    //return hourly , so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->rainmm[i];
}

uint16_t WeatherInfo::getDailyRain(uint8_t daysfromnow)
{
    //use the hourly rain forecast to determine how much rain will fall for the specifed day (in days from now)
    //what is midnight TONIGHT?
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t totalrain = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) totalrain+=this->rainmm[j]; 
    
    return totalrain;
}
uint16_t WeatherInfo::getDailyRain(uint32_t starttime, uint32_t endtime)
{

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t total = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= starttime && this->dT[j] < endtime) total+=this->rainmm[j]; 
    
    return total;
}


uint16_t WeatherInfo::getDailySnow(uint8_t daysfromnow)
{
    //use the hourly rain forecast to determine how much rain will fall for the specifed day (in days from now)
    //what is midnight TONIGHT?
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t total = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) total+=this->snowmm[j]; 
    
    return total;
}
uint16_t WeatherInfo::getDailySnow(uint32_t starttime, uint32_t endtime)
{

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t total = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= starttime && this->dT[j] < endtime) total+=this->snowmm[j]; 
    
    return total;
}

uint16_t WeatherInfo::getDailyIce(uint8_t daysfromnow)
{
    //use the hourly rain forecast to determine how much rain will fall for the specifed day (in days from now)
    //what is midnight TONIGHT?
    uint32_t MN0 = makeUnixTime(year()-2000,month(),day(),0,0,0);

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t total = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= (MN0 + daysfromnow*86400) && this->dT[j] < (MN0 + daysfromnow*86400 + 86400)) total+=this->icemm[j]; 
    
    return total;
}
uint16_t WeatherInfo::getDailyIce(uint32_t starttime, uint32_t endtime)
{

    //calculate the rainfall for the time that is within dT: [MN0+ daysfromnow*86400] to [MN0 + daysfromnow*86400 + 86400]
    uint16_t total = 0; //this number is generally <100, but could be as high as 400+
    for (byte j=0;j<NUMWTHRDAYS*24;j++) if (this->dT[j] >= starttime && this->dT[j] < endtime) total+=this->icemm[j]; 
    
    return total;
}


uint8_t WeatherInfo::getSnow(uint32_t dt)
{ //get snow in this hour
        //special request... dt is zero, then get the total rain for next 24h
    if (dt==0) {
        uint8_t total = 0;
        for (byte j=0;j<24;j++) total+=this->snowmm[j]; 
        return total;
    }
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->snowmm[i];

}

uint8_t WeatherInfo::getIce(uint32_t dt)
{
            //special request... dt is zero, then get the total ice for next 24h
    if (dt==0) {
        uint8_t total = 0;
        for (byte j=0;j<24;j++) total+=this->icemm[j]; 
        return total;
    }
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->icemm[i];

}


uint32_t WeatherInfo::nextPrecip() {
    for (byte j=0;j<24;j++) {
        if (this->PoP[j]>50) return this->dT[j];
    }

    //didn't find rain in the next 24 hours, check the next week
    for (byte j=0;j<14;j++) {
        if (this->daily_PoP[j]>50) return this->daily_dT[j];
    }

    return -1;

}


uint32_t WeatherInfo::nextRain() {
    for (byte j=0;j<24;j++) {
        if (this->rainmm[j]>4) return this->dT[j];
    }

    //didn't find rain in the next 24 hours
    for (byte j=0;j<14;j++) {
        if (this->daily_weatherID[j]>=500 && this->daily_weatherID[j]<600 ) return this->daily_dT[j];
    }

    return -1; //this rolls over to a really large number...

}

uint32_t WeatherInfo::nextSnow() {
    for (byte j=0;j<24;j++) {
        if (this->snowmm[j]>10 || this->icemm[j]>2) return this->dT[j];
    }

    //didn't find snow/ice in the next 24 hours
    for (byte j=0;j<14;j++) {
        if ((this->daily_weatherID[j]>=600 && this->daily_weatherID[j]<700 ) || (this->daily_weatherID[j]>=300 && this->daily_weatherID[j]<400 )) return this->daily_dT[j];
    }

    return -1;

}


int8_t WeatherInfo::getDewPoint(uint32_t dt)
{
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->dewPoint[i];

}
uint8_t WeatherInfo::getWindSpeed(uint32_t dt)
{
    //return hourly temperature, so long as request is within limits
    byte i = getIndex(dt);
    if (i==255) //did not find this index/time 
        return 255; //failed

     return this->windspeed[i];

}
time_t WeatherInfo::breakNOAATimestamp(String tm) 
{
    uint8_t y = (byte) ((int) tm.substring(0,4).toInt()-2000);
    uint8_t m = (byte) tm.substring(5,7).toInt();
    uint8_t d = (byte) tm.substring(8,10).toInt();
    uint8_t h = (byte) tm.substring(11,13).toInt();
    uint8_t n = (byte) tm.substring(14,16).toInt();
    uint8_t s = (byte) tm.substring(17,19).toInt();


    return makeUnixTime(y,m,d,h,n,s);

}

int16_t WeatherInfo::breakIconLink(String icon,uint32_t starttime, uint32_t endtime)
{
    //starttime is the start of the icon interval. endtime is the end of the interval, though can be zero to imply 1 hr
    //convert NOAA icon id to openweather API ID for weather type. I am doing this for backward compatibility
    //icons and definitions found here: https://www.weather.gov/forecast-icons/
    //icon names found here: 

    //start with the worst weather, working to mildest
    if (icon.indexOf("hurricane",0)>-1) return 700;
    if (icon.indexOf("tropical_storm",0)>-1) return 503;
    if (icon.indexOf("blizzard",0)>-1) return 601;

    if (icon.indexOf("snow",0)>-1) {
        
        if (icon.indexOf("rain_snow",0)>-1) return 611;
        if (icon.indexOf("snow_sleet",0)>-1) return 611;
        if (icon.indexOf("snow_fzra",0)>-1) return 611;

        //hourly snow of 0.5 in / 12.7 mm= low, 0.5-1.5 / 12.7 / 38 = med, >1.5 / 38 is high
        //daily snow of <1 or <25mm in is low, 1-4 {25-100} in med, >4 {>100} in high       

        if (endtime <= starttime + 3600 ) { //1 hour is the minimum
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
        if (endtime <= starttime + 3600 ) { //1 hour is the minimum
            byte total = this->getRain(starttime);             
            if (total<4) return 200;
            return 201;
        } else {
            byte total = this->getDailyRain(starttime,endtime);
            
            //note that these are 1/2 day totals
            if (total<5) return 200;
            return 201;
        }

        if (icon.indexOf("tsra_hi",0)>-1) return 201;        //thunderstorm
        return 200;
    }


    if (icon.indexOf("rain",0)>-1) {
        if (icon.indexOf("rain_sleet",0)>-1) return 611;
        if (icon.indexOf("rain_fzra",0)>-1) return 611;

        if (endtime <= starttime + 3600 ) { //1 hour is the minimum
            byte total = this->getRain(starttime);             
            if (total<1) return 301;
            if (total<2) return 500;
            if (total<4) return 501;
            if (total<6) return 502;
            return 503;
        } else {
            byte total = this->getDailyRain(starttime,endtime);
            //note that these are 1/2 day totals
            if (total<2) return 301;
            if (total<5) return 500;
            if (total<15) return 501;
            if (total<25) return 502;
            return 503;
        }
        return 500;
    }


    if (icon.indexOf("dust",0)>-1) return 804;
    if (icon.indexOf("hot",0)>-1) return 800;
    if (icon.indexOf("cold",0)>-1) return 602;


    //clear weather
    if (icon.indexOf("ovc",0)>-1) return 804;
    if (icon.indexOf("bkn",0)>-1) return 803;
    if (icon.indexOf("sct",0)>-1) return 801;
    if (icon.indexOf("few",0)>-1) return 801;
    if (icon.indexOf("skc",0)>-1) return 800;

    return 999;    
}

bool WeatherInfo::initWeather() 
{
    this->lastUpdateT = 0;
    this->lastUpdateStatus = false;
    this->lastUpdateAttempt =0;

    for (byte i=0;i<NUMWTHRDAYS*24;i++) {        
        this->dT[i] = 0; //time for each element
        this->temperature[i] = 0; //degrees in F
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

    for (byte i=0;i<14;i++) {        
        this->daily_dT[i] = 0; //time for daily elements
        this->daily_tempMax[i] = 0;
        this->daily_tempMin[i] = 0;
        this->daily_weatherID[i] = 0;
        this->daily_PoP[i] = 0;
    }

    this->sunrise = 0;
    this->sunset=0;


    return true;
}


uint8_t WeatherInfo::getDailyPoP(uint8_t intsfromnow, bool indays)
{
    if (indays) {
        if (this->daily_tempMax[0] == -125) { //starts at night
            if (intsfromnow>0) intsfromnow = (intsfromnow*2)-1;            

        } else {
            intsfromnow = (intsfromnow*2);
        }
    }

    return this->daily_PoP[intsfromnow];
}
int16_t WeatherInfo::getDailyWeatherID(uint8_t intsfromnow,bool indays) {
    if (indays) {
        if (this->daily_tempMax[0] == -125) { //starts at night
            if (intsfromnow>0) intsfromnow = (intsfromnow*2)-1;            
        } else {
            intsfromnow = (intsfromnow*2);
        }
    }
    return this->daily_weatherID[intsfromnow];
}

void WeatherInfo::getDailyTemp(uint8_t daysfromnow, int8_t* temp){
    //must pass a 2 element int8_t array to hold temp
    int8_t iDay,iNight; //indices to the day and night temps for THE SPECIFIED day

    if (this->daily_tempMax[0] == -125) { //it is night
        if (hour()>12) {
            //it is still evening / night
            iDay = daysfromnow*2-1;
            iNight = daysfromnow*2;        
        } else {
            //it is the next day (after mn)
            iDay = daysfromnow*2+1;
            iNight = daysfromnow*2+2;        
        }
    } else {
        iDay = daysfromnow*2;
        iNight = daysfromnow*2+1;        
    }

    if (iDay<0) { //this can happen if daysfromnow is 0 and it is night.
      temp[0] = WeatherData.getTemperature(); //it is the evening of the same day, tmax is current temp by definition    
    }
    else temp[0] = this->daily_tempMax[iDay];

    temp[1] = this->daily_tempMin[iNight];
}


bool WeatherInfo::updateWeather(uint16_t synctime) 
{

    time_t tnow = now();
    if (this->lastUpdateT+synctime > tnow && this->lastUpdateStatus==true) return false; //not time to update
    if ((uint32_t) this->lastUpdateAttempt+30 > tnow) return false; //last update failed and was at least 30s ago

    #ifdef _DEBUG
Serial.printf("Updateweather: init...");
    #endif

    initWeather();

    this->lastUpdateStatus=false;
    this->lastUpdateAttempt = tnow;

    String url;
    JsonDocument doc;
    int httpCode;

    #ifdef _DEBUG
Serial.printf(" done at %s.\n",dateify(tnow));
    #endif


    if (this->Grid_x == 0 && this->Grid_y == 0) {
        #ifdef _DEBUG
            Serial.printf("updateweather: get grid points\n");                
        #endif

        //get NOAA grid coordinates by API call ( NOAA uses specific geolocation, not zip or lon/lat)
        //https://api.weather.gov/points/lat,lon
        char cbuf[100];
        snprintf(cbuf,99,"https://api.weather.gov/points/%.4f,%.4f",this->lat,this->lon);
        url =  (String) cbuf;
        String ca_cert = getCert("/Certificates/NOAA.crt");

        if (Server_SecureMessage(url, WEBHTML, httpCode, ca_cert)==false) {

            #ifdef _DEBUG
                    Serial.printf("updateweather: failed to update grid points\n");                
            #endif
            
            return false; //failed

        }

        DeserializationError error = deserializeJson(doc, WEBHTML);
        if (error) {
            #ifdef _DEBUG
                Serial.printf("updateWeather: json error: %d\n",error.c_str());
                while(true);
            #endif
            
            return false;
        }

        strcpy(this->Grid_id, doc["properties"]["gridId"]);
        this->Grid_x = doc["properties"]["gridX"];
        this->Grid_y = doc["properties"]["gridY"];
            

    }
    

    //pull forecasts from NOAA API
    //there are three separate API calls with slightly different content, and unfortunately we will need to pull all 3...
    //note that temp unit might be F or C, so we need to check for that

    //forecast/hourly
    //this has hourly data including wind,temp, dewpoint, humidity, PoP, weatherID
    // EX: https://api.weather.gov/gridpoints/BOX/64,86/forecast/hourly
    
    

    url = this->getGrid(2); //forecast/hourly
    #ifdef _DEBUG
    Serial.printf("updateweather: get hourly used this URL: %s\n", url.c_str());                
    #endif

    //due to the size of return, will need to process the stream rather than a stored copy
    
    if(WiFi.status()== WL_CONNECTED){
        HTTPClient http;
        WiFiClientSecure wfclient;
        String ca_cert = getCert("/Certificates/NOAA.crt");

        wfclient.setCACert(ca_cert.c_str());
        I.wifi=10;
        http.useHTTP10(true); //downgrade http so that the below stream capture works. Otherwise must use StreamUtils library to chunk decode the stream
        http.begin(wfclient,url.c_str());
        
        httpCode = http.GET();
        //see https://arduinojson.org/v6/how-to/use-arduinojson-with-httpclient/
        DeserializationError error = deserializeJson(doc, http.getStream()); //process the stream in place

        if (error) {
            #ifdef _DEBUG
                Serial.printf("updateWeather: json error: %d\n",error.c_str());
                while(true);
            #endif
            
            return false;
        }
        JsonObject properties = doc["properties"];
    
        uint8_t cnt = 0;
        for (JsonObject properties_period : properties["periods"].as<JsonArray>()) {
            String tmp = properties_period["startTime"];
            this->dT[cnt] = breakNOAATimestamp(tmp);

            this->temperature[cnt] = (int8_t) properties_period["temperature"]; // 50, 52, 52, 53, 53, 51, 48, 45, ...

            const char* tu =  properties_period["temperatureUnit"]; // "F", "F", "F", ...
            tmp = (String) tu;
            if (tmp=="C") this->temperature[cnt] = (9*this->temperature[cnt]/5) +32;

            this->PoP[cnt] = (uint8_t) properties_period["probabilityOfPrecipitation"]["value"];
            
            this->dewPoint[cnt] = (int8_t) properties_period["dewpoint"]["value"]; // -5.555555555555555, ...

            this->humidity[cnt] = (uint8_t)  properties_period["relativeHumidity"]["value"]; // 33, ...

            const char* ws =  properties_period["windSpeed"]; // "9 mph", "10 mph", "10 ...
            tmp = (String) ws;
            this->windspeed[cnt] = (uint8_t)  (tmp.substring(0,tmp.indexOf(" mph")).toInt());

            const char* properties_period_icon = properties_period["icon"];
            tmp = (String) properties_period_icon;
            this->weatherID[cnt] = breakIconLink(tmp,this->dT[cnt],0);

            cnt++;
            if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!
        }            

        http.end();

    }


    

    //next, pull grid from NOAA API. 
    //ex: https://api.weather.gov/gridpoints/BOX/64,86
    //grid has detailed info including wetbulbtemp, pop, snowmm, rainmm, sky cover
    //grid also has daily max and min temp - but will need to get that from daily page
    //note that temp unit might be C
    //however, grid is not hourly... it is arbitrary intervals based on data change
    //will take [snow/ice/rain accumulation ] from this pull



    if(WiFi.status()== WL_CONNECTED){
        url = getGrid(0);
        #ifdef _DEBUG
        Serial.printf("updateweather: get forecast used this URL: %s\n", url.c_str());                
        #endif

        HTTPClient http;
        WiFiClientSecure wfclient;
        String ca_cert = getCert("/Certificates/NOAA.crt");

        wfclient.setCACert(ca_cert.c_str());
        I.wifi=10;
        http.useHTTP10(true); //downgrade http so that the below stream capture works. Otherwise must use StreamUtils library to chunk decode the stream
        http.begin(wfclient,url.c_str());
        
        httpCode = http.GET();
        //see https://arduinojson.org/v6/how-to/use-arduinojson-with-httpclient/
        DeserializationError error = deserializeJson(doc, http.getStream()); //process the stream in place

        if (error) {
            #ifdef _DEBUG
                Serial.printf("updateWeather: json error: %d\n",error.c_str());
                while(true);
            #endif
            
            return false;
        }

        JsonObject properties = doc["properties"];

        byte cnt=0; 
        for (JsonObject properties_temperature_value : properties["wetBulbGlobeTemperature"]["values"].as<JsonArray>()) {
            const char* value_validTime = properties_temperature_value["validTime"];
            String tmp = value_validTime;
            uint32_t dt = breakNOAATimestamp(tmp);
    
            byte i = getIndex(dt); //note that this may be in the future, and may have gaps
//            this->wetBulbTemperature[i] = int8_t ((double) (9*properties_temperature_value["value"])/5+32); // in C, convert to F

            while (cnt<=i) { //do this because there may be gaps in the table... elements may only be listed when there is a change, or step may not be static
                
                if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!
                this->wetBulbTemperature[cnt] = int8_t ((double) (properties_temperature_value["value"])*9/5+32);
                cnt++;
            }

            if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

        }

        cnt=0; 
        for (JsonObject properties_value : properties["quantitativePrecipitation"]["values"].as<JsonArray>()) {
            const char* value_validTime = properties_value["validTime"];
            String tmp = value_validTime;
            uint32_t dt = breakNOAATimestamp(tmp);
    
            byte i = getIndex(dt);
            while (cnt<=i) { //do this because there may be gaps in the table... elements may only be listed when there is a change, or step may not be static
                if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

                this->rainmm[cnt] = uint8_t ((double) properties_value["value"]); // 17.7777777777778, ...
                if (this->rainmm[cnt]>0) this->flag_rain = true;
                else this->flag_rain = false;
                cnt++;
            }
            if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

        }

        cnt=0; 
        for (JsonObject properties_value : properties["iceAccumulation"]["values"].as<JsonArray>()) {
            const char* value_validTime = properties_value["validTime"];
            String tmp = value_validTime;
            uint32_t dt = breakNOAATimestamp(tmp);
    
            byte i = getIndex(dt);

            while (cnt<=i) { //do this because there may be gaps in the table... elements may only be listed when there is a change, or step may not be static
                if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

                this->icemm[cnt] = uint8_t ((double) properties_value["value"]); // 17.7777777777778, ...
                if (this->icemm[cnt]>0) this->flag_ice = true;
                else this->flag_ice = false;
                cnt++;
            }
            if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

        }


        cnt=0; 
        for (JsonObject properties_value : properties["snowfallAmount"]["values"].as<JsonArray>()) {
            const char* value_validTime = properties_value["validTime"];
            String tmp = value_validTime;
            uint32_t dt = breakNOAATimestamp(tmp);
    
            byte i = getIndex(dt);
            while (cnt<i) { //do this because there may be gaps in the table... elements may only be listed when there is a change, or step may not be static
                if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!
                this->snowmm[cnt] = uint8_t ((double) properties_value["value"]); 
                if (this->snowmm[cnt]>1) this->flag_snow = true; //snow flag only for measurable amount of snow
                else this->flag_snow = false;
                cnt++;                            
            }
            if (cnt>=NUMWTHRDAYS*24) break; //the loop will go on for the number of elements, but we can only hold this many!

        }            

        http.end();

    }


    //NOAA weather API forecast (daily) has daily max min temp and pop and icon (pop and icon are only  in this file)
    // EX: https://api.weather.gov/gridpoints/BOX/64,86/forecast
    //pull forecast, which has day and night weatherID in icon, dailyTmax, dailyTmin, PoP (with 0 as Null)
        url = getGrid(1);
        #ifdef _DEBUG
    Serial.printf("updateweather: get daily used this URL: %s\n", url.c_str());                
    #endif

    if(WiFi.status()== WL_CONNECTED){
        HTTPClient http;
        WiFiClientSecure wfclient;
        String ca_cert = getCert("/Certificates/NOAA.crt");

        wfclient.setCACert(ca_cert.c_str());
        I.wifi=10;
        http.useHTTP10(true); //downgrade http so that the below stream capture works. Otherwise must use StreamUtils library to chunk decode the stream
        http.begin(wfclient,url.c_str());
        
        httpCode = http.GET();
        //see https://arduinojson.org/v6/how-to/use-arduinojson-with-httpclient/
        DeserializationError error = deserializeJson(doc, http.getStream()); //process the stream in place

        if (error) {
            #ifdef _DEBUG
                Serial.printf("updateWeather: json error: %d\n",error.c_str());
                while(true);
            #endif
            
            return false;
        }

        JsonObject properties = doc["properties"];

        byte cnt=0; 

        for (JsonObject properties_value : properties["periods"].as<JsonArray>()) {
            const char* value_validTime = properties_value["startTime"];
            String tmp = value_validTime;
            this->daily_dT[cnt] = breakNOAATimestamp(tmp);

        //    const char* value_endTime = properties_value["endTime"];
        //    String tmp = value_endTime;
        //    uint32_t et = breakNOAATimestamp(tmp);

            if (properties_value["isDaytime"]) {
                this->daily_tempMax[cnt] = int8_t ((double) properties_value["temperature"]);
                this->daily_tempMin[cnt] = -125; //some arbitary place holder
            } else {
                this->daily_tempMax[cnt] = -125;
                this->daily_tempMin[cnt] = int8_t ((double) properties_value["temperature"]);
            }

            this->daily_PoP[cnt] = (uint8_t) ((double) properties_value["probabilityOfPrecipitation"]["value"]);
            
            const char* properties_period_icon = properties_value["icon"];
            tmp = properties_period_icon;

            this->daily_weatherID[cnt] =  breakIconLink(tmp,this->daily_dT[cnt],this->daily_dT[cnt]+43200);

            cnt++;
            if (cnt>=14) break; //the loop will go on for the number of elements, but we can only hold this many!
        }

        http.end();
    }

    if (this->lastUpdateT > this->sunset || this->lastUpdateT == 0) //update sunrise/sunset when we are past the sunset time
    {
        #ifdef _DEBUG
            Serial.printf("updateweather: get sunrise/sunset\n");                
        #endif

        char cbuf[100];
        snprintf(cbuf,99,"https://api.sunrisesunset.io/json?lat=%f&lng=%f",this->lat,this->lon);
        url = (String) cbuf;
        String cacert = getCert("/Certificates/sunrisesunset.crt");
        
        if (Server_SecureMessage(url, WEBHTML, httpCode,cacert)) //this is a small doc, can fully download and process here
        {
            //failure here does not result in critical failure
            DeserializationError error = deserializeJson(doc, WEBHTML);
            #ifdef _DEBUG
                if (error) {
                    Serial.printf("updateWeather: sunriseio failed deserialize: %s\n",error.c_str());
                }                
            #endif
            if (!error) {
                JsonObject properties = doc["results"];
                const char* srise = properties["sunrise"];
                const char* sset = properties["sunset"];
                const char* sdat = properties["date"];

                String sun = (String) sdat + " " + (String) srise;
                this->sunrise = convertStrTime(sun);

                sun = (String) sdat + " " + (String) sset;
                this->sunset = convertStrTime(sun);
            }
        } 
    }

    this->lastUpdateT = tnow; //mark the last successful update
    this->lastUpdateStatus = true;
    
    return true;
}

String WeatherInfo::getGrid(uint8_t fc) {
    
    if (fc==1) return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y + (String) "/forecast";      
    if (fc==2) return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y + (String) "/forecast/hourly";      

    return (String) "https://api.weather.gov/gridpoints/" + (String) this->Grid_id + "/" + (String) this->Grid_x + "," + (String) this->Grid_y ;

}