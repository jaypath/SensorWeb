#include <Arduino.h>

#include "server.hpp"

#include <WiFiClient.h>




//this server
#ifdef _USE8266

    ESP8266WebServer server(80);
#endif
#ifdef _USE32

    WebServer server(80);
#endif





bool getWeather() {
  if (myScreen.time_lastWeather>0 && myScreen.t-myScreen.time_lastWeather < myScreen.int_Weather_MIN*60 ) return false; //not time to update

  WiFiClient wfclient;
  HTTPClient http;
 
  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String tempstring;
    int httpCode=404;
    

    tempstring = "http://192.168.68.93/REQUESTWEATHER?hourly_temp=0&hourly_weatherID=0&daily_weatherID=0&daily_tempMax=0&daily_tempMin=0&daily_pop=0&sunrise=0&sunset=0";

    http.useHTTP10(true);    
    http.begin(wfclient,tempstring.c_str());
    httpCode = http.GET();
    payload = http.getString();
    http.end();

    if (!(httpCode >= 200 && httpCode < 300)) return false;
    
    myScreen.wthr_currentTemp = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    
    myScreen.wthr_currentWeatherID = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    myScreen.wthr_DailyWeatherID = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    
    myScreen.wthr_DailyHigh = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    
    myScreen.wthr_DailyLow = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    
    myScreen.wthr_DailyPoP = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter
    
    myScreen.wthr_sunrise = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    payload.remove(0, payload.indexOf(";",0) + 1); //+1 is for the length of delimiter

    myScreen.wthr_sunset = payload.substring(0, payload.indexOf(";",0)).toInt(); 
    
    myScreen.time_lastWeather = myScreen.t;
    return true;
  }
return false;
}