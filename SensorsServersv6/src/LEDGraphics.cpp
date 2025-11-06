#include "globals.hpp"

#ifdef _USELED
#include <FastLED.h>
#include "LEDGraphics.hpp"
#include "Devices.hpp"

#ifdef _USE8266
  #define PI 3.14159265
#endif

CRGB LEDARRAY[_USELED_SIZE];

void Animation_type::LED_update(void) {
  uint32_t m = millis();

  if (m-this->last_LED_update<this->LEDredrawRefreshMS) return;

  this->last_LED_update = m;

  double L1,T1;
  int8_t DIR = 0;

  for (byte j=0;j<_USELED_SIZE;j++) {

    if (this->animation_style==1) {
      DIR=-1;
      L1 = (double) 1/this->sin_L;
      T1 = (double) 1/this->sin_T;
    }
    if (this->animation_style==2) {
      DIR=1;
      L1 = (double) 1/this->sin_L;
      T1 = (double) 1/this->sin_T;
    }
    if (this->animation_style==3) {
      DIR=1;
      L1=0;
      T1 = (double) 1/this->sin_T;
    }
    if (this->animation_style==4) {
      DIR=0;
      L1=0;
      T1 = 0;
    }
    if (this->animation_style==5) {
      DIR=0;
      L1=_USELED_SIZE*300; //use L1 for limit of random chance
      T1=750; //use T1 for standard deviaiton of guassian, msec          
    }
    if (this->animation_style==6) {
      DIR=0;
      if (j>=this->sin_L && this->sin_T) {
        L1=1; //use L1 to show or not 
      } else L1=2;      //2 will be off, 1 will be on
    }


    LEDARRAY[j] = this->LED_setLED(j,L1,T1,DIR,m);
  
    
  }


  FastLED.show();

}

uint32_t Animation_type::LED_setLED(byte j, double L1, double T1, int8_t DIR, uint32_t m) {
  //given an LED position, info on timing such as cosine cycle, animation direction, etc - set the color and brightness of that LED 
  //L1 T1 can have differing values, but if L1==0 then it is just static color
  uint32_t temp;
  byte brightness;

  if (L1==0 && j>0) return this->LED_BASE[0]; //all LEDs are the same for this animation style, just return the first base value

  if (this->animation_style==3 || this->animation_style==4) { //got here because j=0 so we completed a draw cycle. Choose new colors
    temp = LED_choose_color(this->MaxBrightness); //note that this will be the same color if color1 = color2
    this->LED_BASE[0] = temp;
    return temp;
  }
  else if (this->animation_style==6) {
    //we don't use LED_BASE for this animation style
    if (L1==1) return LED_choose_color(this->MaxBrightness); //use L1 to show or not 
    else  return 0;
  }
  else if (this->animation_style==5) {
    //guassian shimmering... LED_TIMING holds the times of the peak display
    if (this->LED_TIMING[j]==0) {
      //this LED position has completed a cycle. Reset the next peak time and choose a new color
      this->LED_TIMING[j]=m + random(25,25+L1);
      this->LED_BASE[j]=LED_choose_color(this->MaxBrightness);
      return 0;
    }

    //determine the brightness as a guassian distribution:
    brightness = this->MaxBrightness * ((double) pow(2.71828,-1*((pow((double) m-this->LED_TIMING[j],2)/(2*pow(T1,2)))))); //m is current time, led_array is time of peak, T1 is standrad dev
    if (brightness < this->MinBrightness && this->LED_TIMING[j]<m) { //the peak is in the past and the brightness has tapered to below threshold, set the timing to 0 to reset on next cycke
      this->LED_TIMING[j]=0;
      return 0;
    }
    else {
      return LED_scale_brightness(this->LED_BASE[j],brightness);
    }
  }

  //if we got here then it is a cosine wave

  brightness = (uint8_t) ((double) (this->MaxBrightness-this->MinBrightness) * (cos((double) 2*PI*(j*L1 +(DIR)* m*T1)) + 1)/2 + this->MinBrightness);
  if (brightness == this->MinBrightness) this->LED_BASE[0] = LED_choose_color(this->MaxBrightness); //completed a cycle of the cosine wave, choose new color. Only the first element needs to change.

  temp =  LED_scale_brightness(this->LED_BASE[0],brightness); //only need the first value of LED_BASE, all the colors are the same

/*
  #ifdef _DEBUG
    Serial.printf("LED_setLED: color = %d %d %d\n",temp.red,temp.green,temp.blue);
  #endif 
*/
  
  return temp;
}

uint32_t Animation_type::LED_scale_brightness(CRGB colorin, byte BRIGHTNESS_SCALE) {
  //scales brightness of the provided color
  double r = (double) colorin.red*BRIGHTNESS_SCALE/100;
  double g = (double) colorin.green*BRIGHTNESS_SCALE/100;
  double b = (double) colorin.blue*BRIGHTNESS_SCALE/100;

/*
  #ifdef _DEBUG
    Serial.printf("scale brightness: r_in = %d, g_in = %d, b_in = %d, scalereq = %d, r=%d, g=%d, b=%d, and u32 is %d\n",colorin.red, colorin.green, colorin.blue, BRIGHTNESS_SCALE,(byte) r,(byte) g,(byte) b,(uint32_t) ((byte) r << 16 | ((byte) g << 8 |  ((byte) b) )));
  #endif 
*/     
  return  (uint32_t) ((byte) r << 16 | (byte) g << 8 |  ((byte) b));
}

uint32_t Animation_type::LED_choose_color(byte BRIGHTNESS_SCALE=100) {      
  //chooses a color between 1 and 2 (and sets brightness)
  byte r1 = (byte) ((double) this->color1.red *BRIGHTNESS_SCALE/100);
  byte g1 = (byte) ((double) this->color1.green *BRIGHTNESS_SCALE/100);
  byte b1 = (byte) ((double) this->color1.blue *BRIGHTNESS_SCALE/100);

  byte r2 = (byte) ((double) this->color2.red *BRIGHTNESS_SCALE/100);
  byte g2 = (byte) ((double) this->color2.green *BRIGHTNESS_SCALE/100);
  byte b2 = (byte) ((double) this->color2.blue *BRIGHTNESS_SCALE/100);


  return (uint32_t) (this->LED_scale_color(r1,r2)<<16) | (this->LED_scale_color(g1,g2)<<8) | (this->LED_scale_color(b1,b2));
}

byte Animation_type::LED_scale_color(byte c1, byte c2) {
  //random uniformly choose a color that is between c1 and c2
  if (c1==c2) return c1;
  randomSeed(micros()); 
  if (c1>c2) return random(c2,c1+1);
  return random(c1,c2+1);
}

void Animation_type::LED_set_color(uint32_t color1, uint32_t color2) {
  this->color1 = color1;
  this->color2 = color2;
}


void Animation_type::LED_set_color(byte r1, byte g1, byte b1, byte r2, byte g2, byte b2) {
  this->color1 =   (uint32_t) ((byte) r1 << 16 | (byte) (g1) << 8 | (byte) (b1));
  this->color2 =   (uint32_t) ((byte) r2 << 16 | (byte) (g2) << 8 | (byte) (b2));

  #ifdef _USESERIAL
    Serial.printf("LED_set_color R color1 is now %d.\n",this->color1.red);
  #endif 

}

void Animation_type::LED_set_color_soil(struct SnsType *sns) {
  //while log(resistivity) is linearly correlated with moiusture, it is roughly linear in our range
  //let's scale from blue to green to yellow to red

  byte r1=0,g1=0,b1=0,r2=0,g2=0,b2=0;      
  byte animStyle = 0;

  // Get the sensor limits from Prefs
  int16_t prefs_index = Sensors.getPrefsIndex(sns->snsType, sns->snsID);
  double limitUpper = Prefs.SNS_LIMIT_MAX[prefs_index];
  double limitLower = Prefs.SNS_LIMIT_MIN[prefs_index];
  uint16_t  snspct =  (100*((double)sns->snsValue - limitLower) / (limitUpper - limitLower)); 
  uint8_t snslim = 0;
  byte animStylestep = 0;

  do {
    animStylestep++;
    if (snspct <= (snslim++)*10) animStyle=animStylestep;        
    if (snspct>100) animStyle=11;
  } while (animStyle ==0);

  animStyle--; //added one extra

  switch (animStyle) {
    case 11: //very high... pulse red.
      LED_animation_defaults(3);
      this->MaxBrightness = 100;
      this->MinBrightness = 10;
      this->sin_T = 1000;
      r1=255;
      g1=0;
      b1=0;
      r2=255;
      g2=0;
      b2=0;
      
      break;
    case 10: //high sparkle red
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      this->sin_T = 1000;
      r1=255;
      g1=0;
      b1=0;
      r2=255;
      g2=150;
      b2=0;
      break;
    case 9: 
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=255;
      g1=100;
      b1=0;
      r2=255;
      g2=255;
      b2=0;
      break;
    case 8:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=255;
      g1=200;
      b1=0;
      r2=255;
      g2=255;
      b2=0;
      break;
    case 7:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=255;
      g1=255;
      b1=0;
      r2=175;
      g2=255;
      b2=0;
      break;
    case 6:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=200;
      g1=255;
      b1=0;
      r2=125;
      g2=255;
      b2=0;
      break;
    case 5:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=175;
      g1=255;
      b1=0;
      r2=75;
      g2=255;
      b2=0;
      break;
    case 4:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=100;
      g1=255;
      b1=0;
      r2=100;
      g2=255;
      b2=50;
      break;
    case 3:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=50;
      g1=255;
      b1=100;
      r2=50;
      g2=255;
      b2=0;
      break;
    case 2:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=0;
      g1=255;
      b1=100;
      r2=0;
      g2=255;
      b2=200;
      break;
    case 1:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=0;
      g1=255;
      b1=150;
      r2=0;
      g2=255;
      b2=255;
      break;
    case 0:
      LED_animation_defaults(5);
      this->MaxBrightness = 50;
      this->MinBrightness = 5;
      r1=0;
      g1=0;
      b1=0;
      r2=0;
      g2=150;
      b2=255;
      break;

  }
  this->LED_set_color(r1,g1,b1,r2,g2,b2);
}

void Animation_type::LED_choose_animation_style(String style) {
  if (style == "wave_clockwise") {
    this->LED_animation_defaults(1);
  } else if (style == "wave_counter") {
    this->LED_animation_defaults(2);
  } else if (style == "pulse") {
    this->LED_animation_defaults(3);
  } else if (style == "constant") {
    this->LED_animation_defaults(4);
  } else if (style == "random_gaussian") {
    this->LED_animation_defaults(5);
  }
}


void Animation_type::LED_animation_defaults(byte anim) {
  this->animation_style = anim;
  
  // Initialize LED_BASE and LED_TIMING arrays to prevent garbage values
  for (byte i = 0; i < _USELED_SIZE; i++) {
    this->LED_BASE[i] = 0; // Initialize to black
    this->LED_TIMING[i] = 0; // Initialize timing array
  }
  
  switch (anim) {
    case 1: //wave clockwise
      this->sin_T = 1500; //in other words, T, ms to move through one wavelength
      this->sin_L = _USELED_SIZE/2; //wavelength, in number of LEDs
      this->MaxBrightness = 10; //sin amp - 10% brightness
      this->MinBrightness=5; 
      break;
    case 2: //wave counter-clockwise
      this->sin_T = 1500; //in other words, T, ms to move through one wavelength
      this->sin_L = _USELED_SIZE/2; //wavelength, in number of LEDs
      this->MaxBrightness = 10; //sin amp
      this->MinBrightness=1; 
      break;
    case 3: //pulse
      this->sin_T = 30000; //in other words, T ms to move through one wavelength
      this->sin_L = _USELED_SIZE/2; //wavelength, in number of LEDs
      this->MaxBrightness = 10; //sin amp
      this->MinBrightness=5; 
      break;
    case 4: //const
      this->MaxBrightness = 5;
      this->MinBrightness = 5;
      break;
    case 5: //random gaussian
      this->MaxBrightness = 50;
      this->MinBrightness = 5;        
      break;
    case 6: //progress bar [WORK IN PROGRESS - NOT TESTED]
      this->MaxBrightness = 50;
      this->MinBrightness = 0;
      break;

  }
}

Animation_type LEDs;

void initLEDs() {
  #ifdef _USELED
  FastLED.addLeds<WS2813, _USELED, GRB>(LEDARRAY, _USELED_SIZE).setCorrection(TypicalLEDStrip);
  LEDs.LED_animation_defaults(1);
  LEDs.LEDredrawRefreshMS = 20;
  LEDs.LED_set_color(255, 255, 255, 255, 255, 255); // default is white
  #ifdef _USESERIAL
    Serial.println("LED strip initialized");
  #endif
   #endif
}


#endif // _USELED