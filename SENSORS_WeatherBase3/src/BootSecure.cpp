
#include "BootSecure.hpp"



union PrefstoBytes {
  STRUCT_PrefsH prefs;
  uint8_t bytes[sizeof(STRUCT_PrefsH)];

  PrefstoBytes() {};
};

union IV_b {
  uint8_t b[16];
  uint32_t l[4];    
};


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


void initCreds(WiFi_type *w) {
  
  for (byte j=0;j<33;j++) w->SSID[j]=0;
  for (byte j=0;j<65;j++) w->PWD[j]=0;
}
  

bool getPrefs() {

  Preferences p;
  p.begin("ORIGIN",true);
  
  if (p.isKey("CHAIN") == true) {
    union PrefstoBytes P; 
    p.getBytes("BLOB",P.bytes,sizeof(STRUCT_PrefsH));
    sCRC = p.getUInt("SSIDCRC",0);


    p.getBytes("PWD",WIFI_INFO.PWD,65);
    pCRC = p.getUInt("PWDCRC",0);
  } 

  p.end();


}

bool getWiFiCredentials() {
  
    initCreds(&WIFI_INFO);
    uint16_t sCRC =0;
    uint16_t pCRC=0;
  
  
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
      initCreds(&WIFI_INFO);
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
    
    if (doEncrypt && ESPNOWkeys.LMK_isSet) { //if LMK is not null
        peerInfo.channel = 3;  
        memcpy(peerInfo.lmk, ESPNOWkeys.LMK_KEY_STR, 16);
        peerInfo.encrypt = true;
    } else {
        peerInfo.channel = 0;  
        peerInfo.encrypt = false;        
    }
    
    
    return esp_now_add_peer(&peerInfo);
    

}

esp_err_t delESPNOWPeer(byte* macad) {
  return esp_now_del_peer(macad);
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
    if (t.statusCode==0) { //ping, no sensitive info requested... broadcast insecure WIFI_INFO

      WiFi_type w;
      memcpy(&w,&WIFI_INFO,sizeof(w));
      w.HAVECREDENTIALS = false;
      snprintf((char*) w.PWD,65,"");
      w.statusCode = 0; //no sensitive info and no response required

      sendESPNOW(nullptr,&w); //broadcast my public info

    } else {
    //add that unit as a peer
      if (addESPNOWPeer(t.MAC,true)!=ESP_OK) {
        storeError("ESPNow: Failed to add a peer");
        return;
      } 

      //now send that peer our wifi creds!
      sendESPNOW(t.MAC,&WIFI_INFO);

      //now drop that peer!
      delESPNOWPeer(t.MAC);
    }



  }
  

  bool sendESPNOW(byte* MAC, struct WiFi_type *w_info) {
    struct WiFi_type w;
    memcpy(&w,w_info,sizeof(w));
    esp_err_t result;


    w.statusCode=1; //includes sensitive info unless otherwise specified

    if (MAC==nullptr) {
      //this is a special case, where I will broadcast without login credentials
      for (byte i=0;i<6;i++) MAC[i] = 0xFF;

      //clear PWD and SSID
      for (byte j=0;j<33;j++) w.SSID[j] = '\0';
      for (byte j=0;j<65;j++) w.PWD[j]=0;
      w.statusCode=0;
    } 
    
    result = esp_now_send(MAC, (uint8_t *) &w, sizeof(w));
    
    if (result!=ESP_OK) {
      storeError("ESPNow: Failed to send data");
         
      return false;
    }

    return true;
  }
  


  #ifdef _USEENCRYPTION
  
  bool encrypt(char* input, uint16_t* inputlength, unsigned char* output,  uint16_t* outputlength, char* key)
  {
    //input text and length, output vector and length. output length should be 16+1+2 greater than input length:
    //returns the encrypted value within the IV state (initialization vector: a random 16 byte array that prevents the output from being the same everytime) and additional info as follows:
    //first 8 bytes of IV, then the input data, then the last 8 bytes of the IV, then isodd (1 if original input was of odd length), and finally a 0 byte
    //therefore note that output must be a buffer of size input + 16 + 1 + 2 to accept any output (and datalength will be adjusted for the new output). Note that actual output data legnth may be shorter than this
    //key must be 128, 192 or 256 bits long
    //take an input vector, determine if it is a multiple of 16 bytes, and if not add an extra byte
    
    byte localinput[datalength+1] = {0}; //original datalength, plus 1 in case datalength is odd
    bool isodd = false;
    memcpy(localinput, input, datalength);

    if (datalength % 2 != 0) {
      isodd = true;
    }

    byte localoutput[datalength+1+16+2] = {0}; //original datalength, IV state,  1 in case datalength is odd, isodd state, and a tailing 0
    
    if (isodd) datalength+=1;
    

    union IV_b iv;
      

    esp_fill_random(*iv.l,4);


    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, datalength, iv.b, (const unsigned char*)input, output);
    mbedtls_aes_free(&aes);
    return true;
  }
  
  bool decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength)
  {

    byte iv[16] = {0};
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength, iv, (const unsigned char*)input, output); 
    mbedtls_aes_free(&aes);
    return true;
  }
  #endif
  