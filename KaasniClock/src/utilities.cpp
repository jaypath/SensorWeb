
#include "utilities.hpp"


void fcnDrawPic() {

    if (I.time_lastPic==0 || I.t - I.time_lastPic > I.int_Pic_MIN*60) {
        //choose a new random picture
        I.lastPic = random(0,I.num_images-1);
        I.time_lastPic=I.t;
    }

    String fn;
    fn= "/PICS/" + getNthFileName("/PICS",I.lastPic);
    drawBmp(fn.c_str(),0,0,-1);

}

void fcnDrawClock() {
    if (I.time_lastClock>0 && I.t-I.time_lastClock<I.int_Clock_SEC) return;

    int Ypos=210,Xpos=270;
    String st;

    tft.fillScreen(I.BG_COLOR);
    fcnDrawPic();
    tft.setTextFont(8);
    tft.setTextDatum(TR_DATUM);
    tft.setCursor(Xpos,Ypos);
    tft.setTextColor(I.FG_COLOR);
    tft.drawString(dateify(I.t,"hh:nn"),50,Ypos);

    Ypos = 10 + tft.fontHeight(8)+4;
    tft.setCursor(Xpos,Ypos);
    tft.setTextFont(4);
    tft.drawString(dateify(I.t,"mm/dd"),Xpos,Ypos);

    Ypos += tft.fontHeight(4)+4;
    tft.setTextFont(4);
    st = "Current temp: " + (String) I.wthr_currentTemp;
    tft.drawString(st,Xpos,Ypos);

    Ypos += tft.fontHeight(2)+4;
    tft.setTextFont(2);
    st = "High/Low: " + (String) I.wthr_DailyHigh + "/" + (String) I.wthr_DailyLow;
    tft.drawString(st,Xpos,Ypos);

    Ypos += tft.fontHeight(2)+4;
    tft.setTextFont(2);
    st = "%%Precip: " + (String) I.wthr_DailyPoP;
    tft.drawString(st,Xpos,Ypos);

    tft.setTextDatum(TL_DATUM);
    I.time_lastClock=I.t;
}


void drawBmp(const char *filename, int16_t x, int16_t y,int32_t transparent) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  File bmpFS;

    // Open requested file on SD card
  bmpFS = SD.open(filename, "r");

  if (!bmpFS)
  {
    tft.print(filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row;
  uint8_t  r, g, b;

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
        
          *tptr = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
          if (transparent>=0 && *tptr==transparent) *tptr = I.BG_COLOR; //convert background pixels to background
          *tptr++;
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      //Serial.print("Loaded in "); Serial.print(millis() - startTime);
      //Serial.println(" ms");
    }
    else tft.println("BMP format not recognized.");
  }
  bmpFS.close();
}


String breakString(String *inputstr,String token) //take a pointer to input string and break it to the token, and shorten the input string at the same time. Remove token used.
{
  String outputstr = "";
  String orig = *inputstr;

  int strOffset = orig.indexOf(token,0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return outputstr; //did not find the token, just return nothing and do not touch inputstr
  } else {
    outputstr= orig.substring(0,strOffset); //end index is NOT inclusive
    orig.remove(0,strOffset+1);
  }

  *inputstr = orig;
  return outputstr;

}

