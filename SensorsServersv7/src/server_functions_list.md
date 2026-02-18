# server.cpp – Function List and Purpose

Reference for **SensorsServersv7/src/server.cpp**. Use this to identify candidates for trimming to reduce compiled size (e.g. ESP32-S3 flash).

**RAM (est.)** = rough **stack** use when the function is active (frame + locals). Values are approximate; ESP32 frame is ~48–64 B. *Heap: high/med* = function builds large strings/JSON (heap usage, not stack). *Static* = uses static buffers (BSS). Peak stack = sum of deepest call chain.

---

## WiFi / network

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `WiFiEvent(WiFiEvent_t event)` | WiFi event callback; logs event and updates `I.WiFiLastEvent`. | ~0.1 KB |
| `WiFiEventtoString(WiFiEvent_t event)` | Maps `WiFiEvent_t` to a human-readable string (many string literals). | ~0.2 KB (1 String) |
| `CheckWifiStatus(bool trytoconnect)` | Returns connection state (1=OK, 2=OK but status mismatch, 0/negative = error codes). Optionally triggers reconnect. | ~0.1 KB |
| `tryWifi(uint16_t delayms, bool checkCredentials)` | Single attempt to connect using stored credentials; waits up to `delayms`. | ~0.1 KB |
| `connectWiFi(uint8_t retryLimit)` | Retries WiFi connection up to `retryLimit`; initializes UDP multicast when connected. | ~0.1 KB |
| `connectToWiFi(const String& ssid, const String& password, const String& lmk_key)` | Connect with given SSID/password; used by setup wizard. | ~0.3 KB (refs + locals) |
| `APStation_Mode()` | Starts AP (access point) for initial configuration; large HTML/JS for setup UI. | ~0.3 KB; heap: high (WEBHTML) |
| `urlEncode(const String& str)` | URL-encodes a string for query/form use. | ~0.2 KB (1 String) |
| `getWiFiModeString()` | Returns current WiFi mode as string (STA, AP, etc.). | ~0.1 KB |
| `generateAPSSID()` | Builds AP SSID from MAC (e.g. "SensorNet-xxxxxx"). | ~0.1 KB; static char ssid[25] |
| `connectSoftAP(String* wifiID, String* wifiPWD, IPAddress* apIP)` | Configures soft AP with given ID/password and IP. | ~0.2 KB |

---

## HTTP / TLS / encoding

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `base64_dec_len(const char* input, int length)` | Computes decoded length for base64 input. | ~64 B |
| `base64_decode(const char* input, uint8_t* output)` | Decodes base64 into bytes (used for certs etc.). | ~0.1 KB (ints + loop) |
| `getCert(String filename)` | Reads certificate file from SD card and returns contents as string. | ~0.2 KB; heap: med (file read) |
| `formatBytes(uint64_t bytes)` | Formats byte count as "X KB/MB/GB" string. | ~0.1 KB |
| `Server_SecureMessageEx(...)` | HTTPS request with custom method, headers, body, and CACert from SD. | ~0.5 KB (many String refs + HTTPClient); heap: med |
| `Server_Message(String& URL, String& payload, int& httpCode)` | Simple HTTP GET; no HTTPS. | ~0.2 KB; heap: med (payload) |

---

## Setup wizard – API and helpers

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `lookupLocationFromAddress(...)` | Geocodes street/city/state/zip to lat/lon (external API). | ~0.3 KB; heap: med (HTTP + JSON) |
| `autoDetectTimezone(...)` | Detects timezone (and DST params) from device location. | ~0.2 KB |
| `saveTimezoneSettings(...)` | Persists timezone/DST settings. | ~0.1 KB |
| `apiConnectToWiFi()` | POST `/api/wifi` – apply WiFi credentials from setup. | ~0.4 KB (4 Strings); heap: med |
| `apiLookupLocation()` | POST `/api/location` – geocode address. | ~0.4 KB (4 Strings + json); heap: med |
| `apiDetectTimezone()` | GET `/api/timezone` – auto-detect timezone. | ~0.3 KB (uint8_t×4 + Strings) |
| `apiSaveTimezone()` | POST `/api/timezone` – save timezone. | ~0.3 KB |
| `apiGetSetupStatus()` | GET `/api/setup-status` – report setup progress. | ~0.3 KB; heap: med (JSON) |
| `handleApiCompleteSetup()` | GET/POST `/api/complete-setup` – finish setup. | ~0.2 KB |
| `apiScanWiFi()` | GET `/api/wifi-scan` – return list of SSIDs. | ~0.4 KB; heap: high (large JSON) |
| `apiClearWiFi()` | POST `/api/clear-wifi` – clear stored WiFi credentials. | ~0.2 KB |
| `handleInitialSetup()` | GET `/InitialSetup` – serves full setup wizard HTML/JS (large string payload). | ~0.3 KB; heap: high (WEBHTML) |

**Embedded JavaScript (inside HTML in `handleInitialSetup`):**  
`toggleStep`, `showStatus`, `completeStep`, `checkSetupComplete`, `skipLocation`, `completeSetup` – client-side only; no C++ code beyond the string they live in.

---

## Route handlers – main and 404

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleRoot()` | GET `/` – main dashboard; calls `handlerForRoot(false)`. | ~64 B (thin wrapper) |
| `handleALL()` | GET `/ALLSENSORS` – same as root but all sensors; calls `handlerForRoot(true)`. | ~64 B (thin wrapper) |
| `serverTextHeader(String pagename)` | Builds common HTML head + title, device name, time, version, IP (shared by many pages). | ~0.2 KB; heap: med (WEBHTML +=) |
| `serverTextClose(int htmlcode, bool asHTML)` | Appends `</body></html>` and sends `WEBHTML` as HTML or plain. | ~0.1 KB |
| `rootTableFill(byte j)` | Appends one sensor row to `WEBHTML` (device, MAC, name, value, type, flags, links, etc.). | ~0.3 KB (flagsString + many +=); heap: med |
| `handlerForRoot(bool allsensors)` | Builds full root/ALL page: header, table of sensors (via `rootTableFill`), footer. Large HTML. | ~1.2 KB (byte used[NUMSENSORS] + frame); heap: high (WEBHTML) |
| `handleNotFound()` | 404 handler; sends simple "Not Found" response. | ~0.1 KB |

---

## Route handlers – actions and data

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleReboot()` | GET `/REBOOT` – triggers device reboot. | ~64 B |
| `handleCLEARSENSOR()` | Clears sensor state (e.g. reset displayed data). | ~0.1 KB |
| `handleREQUESTUPDATE()` | Triggers refresh/poll of sensor data. | ~0.3 KB (payload, URL, loop vars); heap: med |
| `handleREQUESTWEATHER()` | Triggers weather fetch/refresh. | ~0.2 KB |
| `handleTIMEUPDATE()` | Triggers time sync/update. | ~0.1 KB |
| `handleSTATUS()` | GET `/STATUS` – status page (WiFi, UDP/HTTP stats, sensors, etc.); lots of HTML. | ~0.5 KB; heap: high (WEBHTML) |
| `handleRETRIEVEDATA()` | GET `/RETRIEVEDATA` – raw time-series data for a sensor (query params). | ~1.2 KB (byte N=100, uint8_t f[100], Strings, loop); heap: med |
| `handleRETRIEVEDATA_MOVINGAVERAGE()` | GET `/RETRIEVEDATA_MOVINGAVERAGE` – moving-average data for a sensor. | ~1.2 KB (same pattern as RETRIEVEDATA); heap: med |
| `addPlotToHTML(uint32_t t[], double v[], byte N, ...)` | Injects a small plot (e.g. sparkline) into HTML for one series. | ~0.5 KB (N up to 100, loop); heap: med (WEBHTML +=) |

---

## Route handlers – config and core flags

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleCONFIG()` | GET `/CONFIG` – configuration page (WiFi, device name, etc.); large form HTML. | ~0.2 KB; heap: high (WEBHTML) |
| `handleCONFIG_POST()` | POST `/CONFIG` – apply config form (credentials, device name, etc.). | ~0.3 KB (newDeviceName, char ssid[25]); heap: med |
| `handleCONFIG_DELETE()` | POST `/CONFIG_DELETE` – delete/clear configuration. | ~0.1 KB |
| `handleREADONLYCOREFLAGS()` | GET `/READONLYCOREFLAGS` – read-only view of core flags/settings. | ~0.3 KB; heap: high (large HTML) |

---

## Route handlers – peripheral (sensor) config

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleSENSOR_UPDATE_POST()` | POST `/SENSOR_UPDATE` – update sensor name, limits, intervals (peripheral only). | ~0.4 KB (uint8_t×2, newName, flagName in loop); heap: med |
| `handleSENSOR_READ_SEND_NOW()` | POST `/SENSOR_READ_SEND_NOW` – trigger immediate read/send for a sensor. | ~0.2 KB (resultMsg, uint8_t×2); heap: low |

---

## Route handlers – Google Sheets

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleGSHEET()` | GET `/GSHEET` – Google Sheets config/status page. | ~0.3 KB; heap: high (WEBHTML) |
| `handleGSHEET_POST()` | POST `/GSHEET` – save Sheets config. | ~0.2 KB |
| `handleGSHEET_UPLOAD_NOW()` | POST – trigger one-shot upload to Sheets. | ~0.2 KB (String msg); heap: low |
| `handleGSHEET_SHARE_ALL()` | POST – share/export to Sheets. | ~0.2 KB (String msg) |
| `handleGSHEET_DELETE_ALL()` | POST – delete/clear Sheets data. | ~0.2 KB (String msg) |
| `handleREQUEST_BROADCAST()` | POST `/REQUEST_BROADCAST` – trigger ESP-NOW broadcast. | ~0.2 KB (String msg) |

---

## Route handlers – timezone (legacy UI)

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `getTimezoneInfo(...)` | Fills struct with current timezone/DST settings from storage. | ~0.1 KB |
| `handleTimezoneSetup()` | GET `/TimezoneSetup` – timezone config page. | ~0.4 KB (uint8_t×4 + Strings); heap: high (WEBHTML) |
| `handleTimezoneSetup_POST()` | POST – save timezone from that page. | ~0.3 KB (uint8_t×4); heap: med |
| `handleUPDATETIMEZONE()` | GET `/UPDATETIMEZONE` – legacy timezone update page. | ~0.3 KB; heap: high (WEBHTML) |
| `handleUPDATETIMEZONE_POST()` | POST – save from legacy timezone page. | ~0.4 KB (JsonDocument + Strings); heap: med |

---

## Route handlers – weather

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleWeather()` | GET `/WEATHER` – weather config/display page. | ~0.3 KB; heap: high (WEBHTML) |
| `handleWeather_POST()` | POST – save weather config. | ~0.2 KB |
| `handleWeatherRefresh()` | POST – refresh weather data. | ~0.1 KB |
| `handleWeatherZip()` | POST – set location by zip. | ~0.3 KB (zipCode, jsonResponse); heap: med |
| `handleWeatherAddress()` | POST – set location by address. | ~0.4 KB (street, city, state, zipCode); heap: med |
| `handlerForWeatherAddress(...)` | Shared logic: geocode address and save weather location. | ~0.3 KB; heap: med (HTTP + JSON) |

---

## Route handlers – SD card and logs

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleSDCARD()` | GET `/SDCARD` – main SD card management page (very large HTML). | ~0.6 KB (fileCount, filename, fileType, fileCategory, lastUpdated, timeStr[25]×2, SensorDataPoint, loops); heap: high (WEBHTML) |
| `handleSDCARD_DELETE_DEVICES()` | POST – delete stored devices. | ~0.1 KB |
| `handleSDCARD_DELETE_SENSORS()` | POST – delete stored sensors. | ~0.1 KB |
| `handleSDCARD_STORE_DEVICES()` | POST – store current devices to SD. | ~0.1 KB |
| `handleSDCARD_DELETE_SCREENFLAGS()` | POST – delete screen flags. | ~0.1 KB |
| `handleSDCARD_DELETE_WEATHERDATA()` | POST – delete weather data. | ~0.1 KB |
| `handleSDCARD_SAVE_SCREENFLAGS()` | POST – save screen flags. | ~0.1 KB |
| `handleSDCARD_SAVE_WEATHERDATA()` | POST – save weather data. | ~0.1 KB |
| `handleSDCARD_TIMESTAMPS()` | GET – view/edit timestamps. | ~0.5 KB (filename, content, htmlContent, line, lineStart/End, entryCount, filename/timestamp/readableTime); heap: high (content + HTML) |
| `handleERROR_LOG()` | GET `/ERROR_LOG` – view error log. | ~0.5 KB (htmlContent, content, line, lineStart/End, entryCount, timestamp, errorCode); heap: high (content + HTML) |
| `handleSDCARD_DELETE_ERRORLOG()` | POST – delete error log. | ~0.1 KB |
| `handleSDCARD_DELETE_TIMESTAMPS()` | POST – delete timestamps. | ~0.1 KB |
| `handleSDCARD_DELETE_GSHEET()` | POST – delete Google Sheets data from SD. | ~0.1 KB |
| `handleREBOOT_DEBUG()` | GET `/REBOOT_DEBUG` – reboot from debug menu. | ~0.1 KB |

---

## Route handlers – device viewer

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `handleDeviceViewer()` | GET `/DEVICEVIEWER` – list devices and details (paged). | ~0.4 KB; heap: high (WEBHTML) |
| `handleDeviceViewerNext()` | GET – next device in viewer. | ~0.1 KB |
| `handleDeviceViewerPrev()` | GET – previous device. | ~0.1 KB |
| `handleDeviceViewerPing()` | GET – ping a device. | ~0.2 KB |
| `handleDeviceViewerDelete()` | GET – delete a device from the list. | ~0.3 KB; heap: med |

---

## Route registration

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `setupServerRoutes()` | Registers all `server.on(...)` routes; single place that ties URLs to handlers above. | ~0.1 KB (no locals) |

---

## JSON – build and encode

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `JSONbuilder_device(ArborysDevType* device)` | Builds JSON object for one device (MAC, IP, name, devType). | ~0.2 KB (String build); heap: med |
| `JSONbuilder_sensorData(ArborysSnsType* S)` | Wraps one sensor as `"sensorData": { ... }`. | ~0.1 KB; heap: med (calls sensorObject) |
| `JSONbuilder_sensorObject(ArborysSnsType* S)` | Builds sensor JSON object (type, id, name, value, time, flags, etc.). | ~0.2 KB (String); heap: med |
| `JSONbuilder_sensorMSG(...)` | Fills buffer with JSON message for one sensor (HTTP or UDP format). | ~0.1 KB (uses caller’s buffer) |
| `JSONbuilder_sensorMSG_all(...)` | Fills buffer with JSON for all sensors. | ~0.2 KB (uses caller’s buffer; loop over sensors) |
| `JSONbuilder_DataRequestMSG(...)` | Fills buffer with "request data" message for one or all sensors. | ~0.1 KB (uses caller’s buffer) |
| `JSONbuilder_pingMSG(...)` | Fills buffer with ping or ping-ack message. | ~0.1 KB (uses caller’s buffer) |
| `JSONbuilder_encodeHTTP(String& jsonBuffer)` | Encodes JSON for HTTP (e.g. wrapping/escaping). | ~0.2 KB; heap: med (String) |

---

## JSON – parse and POST handler

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `processJSONMessage(String& postData, String& responseMsg)` | Top-level JSON handler; dispatches by message type (device, ping, data request, sensor data). | ~0.6 KB (StaticJsonDocument<512> + char jsonBuffer[512]); heap: med (doc) |
| `processJSONMessage_addDevice(JsonObject root, String& responseMsg)` | Parses "add device" message; registers device. | ~0.2 KB |
| `processJSONMessage_ping(JsonObject root, String& responseMsg, bool isAck)` | Handles ping / ping-ack. | ~0.1 KB |
| `processJSONMessage_DataRequest(JsonObject root, String& responseMsg)` | Handles "send me data" request. | ~0.1 KB |
| `processJSONMessage_sensorData(JsonObject root, String& responseMsg)` | Handles incoming sensor data; may call `handleSingleSensor` per sensor. | ~0.2 KB |
| `handleSingleSensor(ArborysDevType* dev, JsonObject sensor, String& responseMsg)` | Processes one sensor entry in a sensorData message; calls `registerSensorData` etc. | ~0.2 KB |
| `handlePost()` | POST `/POST` – receives JSON body, calls `processJSONMessage`, returns OK or error. | ~0.3 KB (responseMsg); stack adds processJSONMessage (~0.6 KB) → ~1 KB peak |

---

## Sensor send logic and timing

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `checkThisSensorTime(ArborysSnsType* Si)` | True if this sensor is due to send (interval elapsed). | ~64 B |
| `isSensorSendTime(int16_t snsIndex)` | Same by index. | ~64 B |
| `isDeviceSendTime(ArborysDevType* D, bool forceSend)` | True if any sensor on device is due (or force). | ~64 B |
| `wrapupSendData(ArborysSnsType* S)` | Post-send bookkeeping (last send time, etc.). | ~64 B |
| `sendHTTPJSON(int16_t deviceIndex, ...)` | Sends JSON to device by index over HTTP. | ~0.2 KB (static urlBuffer[64]); heap: med (HTTPClient) |
| `sendHTTPJSON(IPAddress& ip, ...)` | Sends JSON to IP over HTTP. | ~0.6 KB (char jsonBuffer[512] + urlBuffer); heap: med |
| `sendAllSensors(bool forceSend, int16_t sendToDeviceIndex, bool useUDP)` | Sends all sensors (to one device or broadcast); HTTP or UDP. | ~0.1 KB (calls SendData in loop) |
| `SendData(int16_t snsIndex, bool forceSend, ...)` | Sends one sensor’s data; picks HTTP or UDP. | ~1.1 KB (static char jsonBuffer[1024] in path); heap: med |
| `sendMSG_ping(IPAddress& ip, bool viaHTTP)` | Sends ping to IP (HTTP or UDP). | ~0.6 KB (char jsonBuffer[512]); heap: low |
| `sendMSG_DataRequest(int16_t deviceIndex, int16_t snsIndex, bool viaHTTP)` | Sends data-request by device index. | ~64 B (thin wrapper) |
| `sendMSG_DataRequest(ArborysDevType* d, int16_t snsIndex, bool viaHTTP)` | Sends data-request to device struct; snsIndex -1 = all. | ~0.6 KB (char jsonBuffer[512]); heap: low |

---

## Geocoding (used by weather and setup)

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `getCoordinatesFromAddress(const String& street, ...)` | Geocode street/city/state/zip to lat/lon (API call). | ~0.3 KB; heap: high (HTTP + JsonDocument) |
| `getCoordinatesFromZipCode(const String& zipCode)` | Geocode zip to lat/lon. | ~0.4 KB; heap: high (HTTP + JsonDocument) |
| `getCoordinatesFromZipCodeFallback(const String& zipCode)` | Fallback geocoder when primary fails. | ~0.4 KB; heap: high (HTTP + JsonDocument) |

---

## UDP and telemetry

| Function | Purpose | RAM (est.) |
|----------|---------|------------|
| `receiveUDPMessage()` | Receives one UDP packet; parses and dispatches (ping, data request, sensor data, add device). | ~0.5 KB (packet buffer + JsonDocument); heap: med (doc) |
| `sendUDPMessage(const uint8_t* buffer, IPAddress ip, ...)` | Sends UDP packet to IP; used for sensor/device messages. | ~0.2 KB |
| `registerUDPMessage(IPAddress ip, const char* messageType)` | Records last UDP receive (IP + type). | ~64 B |
| `registerUDPSend(IPAddress ip, const char* messageType)` | Records last UDP send. | ~64 B |
| `registerHTTPMessage(const char* messageType)` | Records last HTTP request (type + client IP). | ~64 B |
| `registerHTTPSend(IPAddress ip, const char* messageType)` | Records last HTTP send to IP. | ~64 B |
| `delayWithNetwork(uint16_t delayTime, uint8_t maxChecks)` | Delay loop that also calls `receiveUDPMessage()` and `server.handleClient()`. | ~0.1 KB (call chain can add ~0.5 KB) |

---

## Summary for size reduction

- **Heaviest on strings/HTML (flash):**  
  `handleInitialSetup` (wizard HTML+JS), `APStation_Mode`, `handleSDCARD`, `handleSTATUS`, `handleCONFIG`, `handlerForRoot`, `rootTableFill`, `serverTextHeader`, `handleREADONLYCOREFLAGS`, weather/timezone/device-viewer pages, and `WiFiEventtoString`.
- **Highest stack (RAM) when active:**  
  `handlerForRoot` (~1.2 KB, `used[NUMSENSORS]`), `handleRETRIEVEDATA` / `handleRETRIEVEDATA_MOVINGAVERAGE` (~1.2 KB, `f[100]`), `handleSDCARD` (~0.6 KB), `SendData` path (~1.1 KB with 1 KB static buffer), `processJSONMessage` (~0.6 KB), `sendHTTPJSON` (~0.6 KB), geocoding + `receiveUDPMessage` (~0.5 KB). Peak = sum of deepest call chain (e.g. handleRoot → handlerForRoot → rootTableFill).
- **Heap: high** (large String/JSON build): setup wizard, `apiScanWiFi`, root/STATUS/CONFIG/SDCARD/weather/timezone/device-viewer HTML, geocoding, `Server_SecureMessageEx`/`Server_Message`.
- **Route registration:**  
  All routes are in `setupServerRoutes()`. Removing a handler and its `server.on(...)` removes that code path and its strings from the binary.
- **Optional feature clusters:**  
  - Setup wizard: `handleInitialSetup`, `api*` setup endpoints, `APStation_Mode` (if you can configure WiFi another way).  
  - Google Sheets: all `handleGSHEET*`.  
  - SD card UI: all `handleSDCARD*`, `handleERROR_LOG`.  
  - Device viewer: all `handleDeviceViewer*`.  
  - Legacy timezone UI: `handleTimezoneSetup`, `handleUPDATETIMEZONE*`.  
  - Weather UI: `handleWeather*`, `handlerForWeatherAddress`, geocoding helpers if not used elsewhere.

Use this list to choose which handlers to disable (e.g. via `#ifdef`) or remove, then re-run `setupServerRoutes()` so only the handlers you keep are registered.
