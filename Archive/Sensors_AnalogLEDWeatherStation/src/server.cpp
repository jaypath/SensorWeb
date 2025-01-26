#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "server.hpp"
#include "animator.hpp"
#include "weather.hpp"


#include "util.hpp"

//Server requests time out after 2 seconds
#define TIMEOUT_TIME 2000

// Set web server port number to 80
//WiFiServer server(80);
WebServer server(80);
WiFiClient wfclient;
HTTPClient http;
  

//const char HTTP_OK_response_header[60] = "HTTP/1.1 200 OK\nContent-type:text/html\nConnection: close\n\n\0";
extern unsigned long last_weather_update;


char* strPad(char* str, char* pad, byte L)     // Simple C string function
{
  byte clen = strlen(str);
 
  for (byte j=clen;j<L;j++) {
    strcat(str,pad);
  }

  return str;
}


void handleUPDATEWEATHER() {
  last_weather_update = 4294967290;

  
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page

}
void handleSETWEATHER(weather_info_t& weather_info, LED_Weather_Animation& animator) {
  int weatherval = server.arg("WeatherVal").toInt();
  byte currenthour = server.arg("HourNow").toInt();
  if (weatherval==0) {
    last_weather_update = 0;
    get_weather_data(weather_info);
    animator.update_weather_status(weather_info.get_weather_id(), weather_info.get_time());
  }
  else {
    animator.update_weather_status(weatherval, currenthour);
    weather_info.current_hour =  currenthour;
  }

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Updated-- Press Back Button");  //This returns to the main page


}
void handleRoot(weather_info_t& weather_info) {
#ifdef DEBUG
  Serial.println("html root here");
  #endif

char temp[30] = "";

String currentLine = "<!DOCTYPE html><html><head><title>LED Weather Station</title></head><body>";

//currentLine += "<h1></h1>";

currentLine += "<br>-----------------------<br>";

currentLine += "Current weatherID: " + String(weather_info.get_weather_id());
currentLine += "<br>";
  
currentLine += "Current temp: " + String(weather_info.get_current_temp());
currentLine += "<br>";

currentLine += "Current next temp: " + String(weather_info.get_next_temp());
currentLine += "<br>";

currentLine += "Current hour: " + String(weather_info.get_time());
currentLine += "<br>";

currentLine += "Last weather update (ms): " + String(millis() - last_weather_update);


currentLine += "<br>-----------------------<br>";
currentLine += "<br>-----------------------<br>";
currentLine += "<br>Set Weather to: <br>";
currentLine += "<br>800 = sunny<br>";
currentLine += "<br>801 = partly sunny<br>";
currentLine += "<br>804 = cloudy<br>";
currentLine += "<br>501 = light rain<br>";
currentLine += "<br>502 = heavy rain<br>";
currentLine += "<br>601 = light snow<br>";
currentLine += "<br>602 = heavy snow<br>";
currentLine += "<br>201 = T-storm<br>";


  currentLine += "<FORM action=\"/SETWEATHER\" method=\"get\">";
  currentLine += "<p style=\"font-family:monospace\">";

  char padder[2] = ".";

  sprintf(temp,"%s","Set weather ID");
  strPad(temp,padder,25);
  currentLine += "<label for=\"CurrentWeatherID\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"WeatherID\" name=\"WeatherVal\" value=\"" + String(weather_info.get_weather_id()) + "\" maxlength=\"30\"><br>";  

  sprintf(temp,"%s","Set currenthour");
  strPad(temp,padder,25);
  currentLine += "<label for=\"CurrentHour\">" + (String) temp + "</label>";
  currentLine += "<input type=\"text\" id=\"WeatherID\" name=\"HourNow\" value=\"" + String(weather_info.get_time()) + "\" maxlength=\"30\"><br>";  

 
  currentLine += "<button type=\"submit\">Submit</button><br><br>";
  currentLine += "<button type=\"submit\" formaction=\"/UPDATEWEATHER\">Recheck weather</button><br><br>";
  currentLine += "</p>";
  currentLine += "</form>";
  
  currentLine += "<br>";
  currentLine += "-----------------------<br>";


currentLine += "</p></body></html>";


    //IF USING PROGMEM: use send_p   !!
  server.send(200, "text/html", currentLine);   // Send HTTP status 200 (Ok) and send some text to the browser/client
}

void handleNotFound(){
  server.send(404, "text/plain", "Arduino says 404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}


/*
void handleClient(WiFiClient& client, LED_Weather_Animation& animator) {
  String header;

  unsigned long currentTime = millis();
  unsigned long previousTime = currentTime;

  bool lineIsEmpty = true;                // make a String to hold incoming data from the client

  #ifdef DEBUG
    Serial.println("Connection started: ");
  #endif

  //First, get header
  while (client.connected() && currentTime - previousTime <= TIMEOUT_TIME) {  // loop while the client's connected
    currentTime = millis();

    if (!client.available()) continue;

    char c = client.read();
    
    //All we care about is the URL in the HTTP request
    //When we encounter a newline, that URL is done, so we don't need any new characters.
    if (c == '\n') break;

    header += c;
  }

  #ifdef DEBUG
    Serial.println("Request header: " + header);
  #endif

  //The server only handles GET requests. If another request type was made, end the connection
  if (header.indexOf("GET ") != 0) {
    #ifdef DEBUG
      Serial.println("Client didn't make a GET request. Ending connection.");
    #endif
    client.stop();
    return;
  }

  String URL = "";
  //We start at the character after "GET "
  int char_ind = 4;

  while (header[char_ind] != ' ') URL += header[char_ind++];

  #ifdef DEBUG
    Serial.println("Got a URL of " + URL);
  #endif

  if (URL.compareTo("/sunny") == 0) animator.set_sunny_status();

  if (URL.compareTo("/cloudy/heavy") == 0 || URL.compareTo("/cloudy") == 0) animator.set_cloudy_status(HEAVY_CLOUDS);
  if (URL.compareTo("/cloudy/light") == 0) animator.set_cloudy_status(LIGHT_CLOUDS);

  if (URL.compareTo("/rain/heavy") == 0 || URL.compareTo("/rain") == 0) animator.set_rainy_status(HEAVY_RAIN);
  if (URL.compareTo("/rain/light") == 0) animator.set_rainy_status(LIGHT_RAIN);

  if (URL.compareTo("/snow/heavy") == 0) animator.set_snowy_status(HEAVY_SNOW);
  if (URL.compareTo("/snow/light") == 0) animator.set_snowy_status(LIGHT_SNOW);

  client.println("Got it! Processed your request with the URL: " + URL);

  // Close the connection
  client.stop();

  #ifdef DEBUG
    Serial.println("Client disconnected.");
    Serial.println("");
  #endif
}
*/

bool SendData() {
  bool isGood = false;

  if(WiFi.status()== WL_CONNECTED){
    String payload;
    String URL;
    String tempstring;
    int httpCode=404;
    tempstring = "/POST?IP=" + WiFi.localIP().toString() + "," + "&varName=LEDSTATUS";
    tempstring = tempstring + "&varValue=";
    tempstring = tempstring + "&Flags=0";
    tempstring = tempstring + "&logID=1.99.1";
    tempstring = tempstring + "&isFlagged=0" ;

    IPAddress SERVERIP[2];
  SERVERIP[0] = {192,168,68,93};
  SERVERIP[1] = {192,168,68,100};

    byte ipindex=0;
    do {
      URL = "http://" + SERVERIP[ipindex].toString();
      URL = URL + tempstring;
    
      http.useHTTP10(true);
      http.begin(wfclient,URL.c_str());
      httpCode = (int) http.GET();
      payload = http.getString();
      http.end();

        #ifdef DEBUG
          Serial.print("Received: ");
          Serial.println(payload);
          Serial.print("Code: ");
          Serial.println(httpCode);
          Serial.println("*****************");
          Serial.println(" ");
        #endif


        if (httpCode == 200) {
          isGood = true;
        } 

      ipindex++;

    } while(ipindex<2);
  
    
  }
  return isGood;
}
