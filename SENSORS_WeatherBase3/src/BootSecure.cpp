
#include "BootSecure.hpp"
#include "esp_random.h"


int8_t initBootSecure() {

  if (Prefs.PROCID!= ESP.getEfuseMac()) {
    #ifdef SETSECURE
      return false;

    #endif
    Prefs.PROCID= ESP.getEfuseMac();
  }

  if (getPrefs()==false) {

    #ifdef SETSECURE
      return false;

    #endif
    
    return setPrefs();

  }
    

  return true;
  
    
}


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


  

bool getPrefs() {

  Preferences p;
  p.begin("STARTUP",true);
  
  if (p.isKey("Boot") == true) {

    uint16_t p_length = sizeof(STRUCT_PrefsH) + sizeof(STRUCT_PrefsH)%16 + 16; //padding + iv
    uint8_t tempPrefs[p_length] = {0};

    p.getBytes("Boot",tempPrefs,p_length);

    unsigned char decodedPrefs[p_length];
    decrypt(tempPrefs, PMK_KEY_STR, decodedPrefs, p_length);
    
    memcpy(&Prefs,decodedPrefs,sizeof(STRUCT_PrefsH));

    p.end();

  } else {
    return false;
  }

return true;



}

bool setPrefs() {
  if (Prefs.PROCID!= ESP.getEfuseMac()) {
    
    #ifdef SETSECURE
      return false;
    #endif

    Prefs.PROCID= ESP.getEfuseMac();
  }

  Preferences p;
  if (p.begin("ORIGIN",false)!=true) return false;

  uint16_t p_length; //padding + iv
  uint8_t tempPrefs[p_length] = {0};
 
  if (encrypt((const unsigned char*) &Prefs, sizeof(STRUCT_PrefsH), (char *) PMK_KEY_STR, (unsigned char*) tempPrefs,  &p_length)!=0) return false;



  p.putBytes("Boot", tempPrefs, p_length);

  p.end();


  return true;
}

  
  int8_t encrypt(const unsigned char* input, uint16_t inputlength, char* key, unsigned char* output,  uint16_t* outputlength)
  {
    //input text and length, output vector and length. output length will be up to 31 bytes greater than input length:
    //if input vector is not a multiple of 16 bytes, pads to get to a multiple of 16 bytes - which means add up to 15 bytes
    //another 16 bytes needed for IV
    //therefore note that output must be able to handle a buffer of inputlength + 31
    //key must be 128, 192 or 256 bits long

    byte localinput[inputlength+15] = {0}; //store a local copy of the input vector. it needs to be large enough to hold necessary padding, which will be 0 to 15 extra bytes
    memcpy(localinput, input, inputlength);
      
    if (inputlength==0 || inputlength % 16 != 0) {
      inputlength+=16-inputlength % 16; //this is the padding amount
    }
    
    outputlength = inputlength + 16; //need another 16 bytes for init vector (IV) and note that inputlength may have increased


    uint8_t iv[16];
    esp_fill_random(iv,16);
    

    memcpy(output, iv, 16);
  

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    if (ret != 0) {
        storeError("mbedtls_aes_setkey_enc failed, -2");
        mbedtls_aes_free(&aes);
        return -2;
    }
    
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, inputlength, iv, localinput, output+16); //output+16 is the first block where encrypted data will go
    if (ret != 0) {
      storeError("mbedtls_aes_crypt_cbc failed, -3");
      mbedtls_aes_free(&aes);
      return -3;
    }


    mbedtls_aes_free(&aes);
    return 0;
  }
  
  int8_t decrypt(unsigned char* input, char* key, unsigned char* output, uint16_t datalength)
  //datalength is the length of the encrypted data packet + 16 bytes for IV 
  {
    byte iv[16] = {0};
    
    memcpy(iv, input, 16);

  
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key, strlen(key) * 8);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, datalength-16, iv, (const unsigned char*)input+16, output); //input +16 is the start of the encrypted data; datalength-16 is the length of the encrypted data (without IV)
    mbedtls_aes_free(&aes);
    return 0;
  }
  

  void initCreds(WiFi_type *w) {
  
    for (byte j=0;j<33;j++) w->SSID[j]=0;
    for (byte j=0;j<65;j++) w->PWD[j]=0;
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
  