#include "BootSecure.hpp"


static const char* PMK_KEY_STR = "KiKa.yaas1anni!~";
static const char* LMK_KEY_STR = "REPLACE_WITH_LMK_KEY";


uint16_t CRCCalculator(uint8_t * data, uint16_t length)
{
   uint16_t curr_crc = 0x0000;
   uint8_t sum1 = (uint8_t) curr_crc;
   uint8_t sum2 = (uint8_t) (curr_crc >> 8);
   int index;
   for(index = 0; index < length; index = index+1)
   {
      sum1 = (sum1 + data[index]) % 255;
      sum2 = (sum2 + sum1) % 255;
   }
   return (sum2 << 8) | sum1;
}


void initCreds() {
    for (byte j=0;j<33;j++) WIFI_INFO.SSID[j]=0;
    for (byte j=0;j<65;j++) WIFI_INFO.PWD[j]=0;
  }
  
  
  bool getWiFiCredentials() {
  
    initCreds();
    uint16_t sCRC =0;
    uint16_t pCRC=0;
  
    Preferences p;
    p.begin("credentials",true);
    
    if (p.isKey("SSID") == true) {
  
      p.getBytes("SSID",WIFI_INFO.SSID,33);
      sCRC = p.getUInt("SSIDCRC",0);
  
  
      p.getBytes("PWD",WIFI_INFO.PWD,65);
      pCRC = p.getUInt("PWDCRC",0);
    } 
  
    p.end();
  
    WIFI_INFO.HAVECREDENTIALS = false;
  
    if (sCRC==0 && pCRC==0) return false; //cannot be 0 crc
  
    if (sCRC!=CRCCalculator((uint8_t*) WIFI_INFO.SSID,32)) return false;
    if (pCRC!=CRCCalculator((uint8_t*) WIFI_INFO.PWD,64)) return false;
    
    WIFI_INFO.HAVECREDENTIALS = true;
    return true;
  
  }
  
  bool putWiFiCredentials() {
  
    bool isGood = false;
    uint16_t sCRC = CRCCalculator((uint8_t*) WIFI_INFO.SSID,32);
    uint16_t pCRC = CRCCalculator((uint8_t*) WIFI_INFO.PWD,64);
  
    Preferences p;
    p.begin("credentials",false);
    p.clear();
  
  
    //if SSID is blank, delete credentials 
    if (WIFI_INFO.SSID[0]=='\0' || sCRC==0) {    
      initCreds();
      sCRC=0;
      pCRC=0;    
    } else isGood=true;
  
  
    p.putBytes("SSID",WIFI_INFO.SSID,33);
    p.putUInt("SSIDCRC",sCRC);
  
    p.putBytes("PWD",WIFI_INFO.PWD,65);
    p.putUInt("PWDCRC",pCRC);
    
    p.end();
  
    return isGood;
  
  
  }
  
  
  

  bool initESPNOW() {
    if (esp_now_init() != ESP_OK) return false;

    esp_now_register_send_cb(esp_now_send_cb_t(OnESPNOWDataSent));
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (addESPNOWPeer(broadcastAddress,false)!=ESP_OK) return false;
    return true;
    
}

esp_err_t addESPNOWPeer(byte* macad,bool doEncrypt) {
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, macad, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (doEncrypt) {
        peerInfo.channel = 3;  
        memcpy(peerInfo.lmk, LMK_KEY_STR, 16);
        peerInfo.encrypt = true;
    } else {
        peerInfo.channel = 0;  
        peerInfo.encrypt = false;        
    }
    
    return esp_now_add_peer(&peerInfo);
}


void OnESPNOWDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        I.lastESPNOW = I.currentTime;    
        return;
    }
    else {
        storeError("ESPNow: Failed to send data");
    } 
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  }
  
  void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    WiFi_type t;
    memcpy(&t, incomingData, sizeof(t));
    
    //received a message from the unit listed...
    //add that unit as a peer
    if (addESPNOWPeer(t.MAC,true)!=ESP_OK) {
        storeError("ESPNow: Failed to add a peer");
        return;
    } 

    //now send that peer our wifi creds!
    sendESPNOW(t.MAC,WIFI_INFO);
  
  }
  
  bool sendESPNOW(byte* MAC, struct WiFi_type w) {
    if (MAC==nullptr) {
        //this is a special case, where I will broadcast my own info minus login credentials
        for (byte i=0;i<6;i++) MAC[i] = 0xFF;

        //clear PWD and SSID
        for (byte j=0;j<33;j++) w.SSID[j] = '\0';
        for (byte j=0;j<65;j++) w.PWD[j]=0;
    }

    //add the peer


    esp_err_t result = esp_now_send(MAC, (uint8_t *) &w, sizeof(w));

      
  
  
  }
  
  #ifdef _USEENCRYPTION
  
  void encrypt(char* input, char* key, unsigned char* output, unsigned char* iv)
  {
  
      mbedtls_aes_context aes;
      mbedtls_aes_init(&aes);
      mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 16, iv, (const unsigned char*)input, output);
      mbedtls_aes_free(&aes);
  
  }
  
  void decrypt(unsigned char* input, char* key, unsigned char* output, unsigned char* iv)
  {
  
      mbedtls_aes_context aes;
      mbedtls_aes_init(&aes);
      mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 16, iv, (const unsigned char*)input, output); //16 is input length
      mbedtls_aes_free(&aes);
  }
  #endif
  