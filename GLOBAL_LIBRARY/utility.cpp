#include "utility.hpp"

bool SerialPrint(String S,bool newline) { return SerialPrint(S.c_str(),newline); }

bool SerialPrint(const char* S,bool newline) {
  bool printed = false;
#ifdef SERIAL_PORT
  SERIAL_PORT.print(S);
  if (newline) SERIAL_PORT.println();
  printed = true;
#else
  Serial.print(S);
  if (newline) Serial.println();
  printed = true;
#endif
  return printed;
}

void storeError(const char* E, ERRORCODES CODE, bool /*writeToSD*/) {
  if (E && strlen(E) < 75) { strncpy(I.lastError, E, 74); I.lastError[74] = '\0'; }
  else if (E) { strncpy(I.lastError, E, 74); I.lastError[74] = '\0'; }
  else { I.lastError[0] = '\0'; }
  I.lastErrorCode = CODE;
  I.lastErrorTime = I.currentTime;
}

String breakString(String *inputstr,String token,bool reduceOriginal) {
  String output = ""; int pos = inputstr->indexOf(token);
  if (pos>-1) { output = inputstr->substring(0,pos); if (reduceOriginal) { *inputstr = inputstr->substring(pos+token.length()); } }
  else { output = *inputstr; if (reduceOriginal) { *inputstr = ""; } }
  return output;
}

uint16_t countSubstr(String orig, String token) {
  uint16_t count = 0; int pos = 0;
  while ((pos = orig.indexOf(token, pos)) != -1) { count++; pos += token.length(); }
  return count;
}

String IPToString(uint32_t ip) {
  return String((ip >> 24) & 0xFF) + "." + String((ip >> 16) & 0xFF) + "." + String((ip >> 8) & 0xFF) + "." + String(ip & 0xFF);
}

uint32_t StringToIP(String str) {
  int parts[4] = {0}; uint32_t ip = 0;
  for (int i = 3; i >=0; i--)  { parts[i] = breakString(&str,".",true).toInt(); ip += parts[i]<<(i*8); }
  return ip;
}

uint32_t IPToUint32(byte* ip) { return (ip[3]<<24) + (ip[2]<<16) + (ip[1]<<8) + ip[0]; }

void uint32toIP(uint32_t ip32, byte* ip) {
  ip[3] = (ip32>>24) & 0xFF; ip[2] = (ip32>>16) & 0xFF; ip[1] = (ip32>>8) & 0xFF; ip[0] = (ip32) & 0xFF;
}

String IPbytes2String(byte* IP,byte len) {
  String output = ""; char holder[10] = "";
  for (int i=len-1; i>=0; i--) { if (i!=len-1) output += "."; snprintf(holder,9,"%d",IP[i]); output += holder; }
  return output;
}

bool IPString2ByteArray(String IPstr,byte* IP) {
  String temp = IPstr; String token; byte i=0;
  while (temp.length()>0 && i<4) { token = breakString(&temp,".",true); if (token.length()>0) { IP[i] = token.toInt(); i++; } }
  return (i==4);
}

uint64_t IPToMACID(uint32_t ip) { return (uint64_t)ip; }

uint64_t IPToMACID(byte* ip) { return IPToMACID(IPToUint32(ip)); }

void uint64ToMAC(uint64_t mac64, byte* macArray) { memcpy(macArray, &mac64, 6); }

uint64_t MACToUint64(byte* macArray) { uint64_t mac64 = 0; memcpy(&mac64, macArray, 6); return mac64; }

String MACToString(uint64_t mac64) {
  byte macArray[6]; uint64ToMAC(mac64, macArray);
  String output=""; for(int i=5;i>=0;i--){ if(i!=5) output += ":"; char buf[3]; snprintf(buf,3,"%02X",macArray[i]); output += buf; }
  return output;
}

String MACToString(uint8_t* mac) {
  String output=""; for(int i=5;i>=0;i--){ if(i!=5) output += ":"; char buf[3]; snprintf(buf,3,"%02X",mac[i]); output += buf; }
  return output;
}

