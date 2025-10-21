#ifdef _USELED
#include <FastLED.h>

#ifdef _USE8266
  #define PI 3.14159265
#endif

CRGB LEDARRAY[LEDCOUNT];

//LED Drawing
typedef struct  {
  //cos(kx +(DIR)* wt) + 0.5; where k - is 2*pi/L [L, lambda, is wavelength]; and w is 2*pi/T [T, period, is time it takes to cycle through one wavelength)so speed is v = w/k; here I am using cos because you can set the inputs to 0 to obtain the static val
  //DIR is -1 for right rotation, +1 for left rotation
  // k = 2*pi/L, where L is in units of number of LEDs
  //w = 2*pi/T where T is seconds to cycle through a wavelenghth [L]
  //y(x,t) = (MaxBrightness-MinBrightness) * (cos(kx - wt) + 1)/2 + MinBrightness; here x is LED number, t is time
  uint8_t  animation_style; //1 = counter clockwise rotation (sin) // 2 = clockwise rotation (sin) // 3 = non-moving pulse // 4 = static
  uint16_t sin_T; //in other words, T, ms to move through one wavelength
  uint8_t sin_L; //wavelength, in number of LEDs
  uint8_t MaxBrightness; //sin amp
  uint8_t MinBrightness; //sin amp
  uint32_t last_LED_update;
  byte LEDredrawRefreshMS;
  CRGB color1; //current color start
  CRGB color2; //current color end //color will be a random uniformly distributed value between color 1 and 2
  uint32_t  LED_TIMING[LEDCOUNT]; //keeps track of the time of each LED in animations where each LED is in a different cycle (like sparkling gaussians, type 5)
  uint32_t  LED_BASE[LEDCOUNT]; //keeps track of the base color of each LED (generated as a random between color1 and color2), necessary where LEDs are cycling independently. In other cases, LED[0] will be the base color. Note this resets when LED completes a cycle
  

  void LED_update(void) {
    uint32_t m = millis();

    if (m-this->last_LED_update<this->LEDredrawRefreshMS) return;

    this->last_LED_update = m;

    double L1,T1;
    int8_t DIR = 0;

    for (byte j=0;j<LEDCOUNT;j++) {

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
        L1=LEDCOUNT*300; //use L1 for limit of random chance
        T1=750; //use T1 for standard deviaiton of guassian, msec          
      }


      LEDARRAY[j] = this->LED_setLED(j,L1,T1,DIR,m);
    
      
    }


    #ifdef _DEBUG
      Serial.printf("global color is %d to %d. LED0 color is %d, 10 %d, 20 %d, 30 %d, 40 %d\n",this->color1,this->color2,LEDARRAY[0],LEDARRAY[10],LEDARRAY[20],LEDARRAY[30],LEDARRAY[40]);
    #endif

    FastLED.show();

  }

  uint32_t LED_setLED(byte j, double L1, double T1, int8_t DIR, uint32_t m) {
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

    if (this->animation_style==5) {
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

    temp =  LED_scale_brightness(this->LED_BASE[0],brightness); //only need the first value of LED_VASE, all the colors are the same

/*
    #ifdef _DEBUG
      Serial.printf("LED_setLED: color = %d %d %d\n",temp.red,temp.green,temp.blue);
    #endif 
*/
    
    return temp;
  }

  uint32_t LED_scale_brightness(CRGB colorin, byte BRIGHTNESS_SCALE) {
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

  uint32_t LED_choose_color(byte BRIGHTNESS_SCALE=100) {      
    //chooses a color between 1 and 2 (and sets brightness)
    byte r1 = (byte) ((double) this->color1.red *BRIGHTNESS_SCALE/100);
    byte g1 = (byte) ((double) this->color1.green *BRIGHTNESS_SCALE/100);
    byte b1 = (byte) ((double) this->color1.blue *BRIGHTNESS_SCALE/100);

    byte r2 = (byte) ((double) this->color2.red *BRIGHTNESS_SCALE/100);
    byte g2 = (byte) ((double) this->color2.green *BRIGHTNESS_SCALE/100);
    byte b2 = (byte) ((double) this->color2.blue *BRIGHTNESS_SCALE/100);


    return (uint32_t) (this->LED_scale_color(r1,r2)<<16) | (this->LED_scale_color(g1,g2)<<8) | (this->LED_scale_color(b1,b2));
  }

  byte LED_scale_color(byte c1, byte c2) {
    //random uniformly choose a color that is between c1 and c2
    if (c1==c2) return c1;
    randomSeed(micros()); 
    if (c1>c2) return random(c2,c1+1);
    return random(c1,c2+1);
  }


  void LED_set_color(byte r1, byte g1, byte b1, byte r2, byte g2, byte b2) {
    this->color1 =   (uint32_t) ((byte) r1 << 16 | (byte) (g1) << 8 | (byte) (b1));
    this->color2 =   (uint32_t) ((byte) r2 << 16 | (byte) (g2) << 8 | (byte) (b2));

    #ifdef _DEBUG
      Serial.printf("LED_set_color R color1 is now %d.\n",this->color1.red);
    #endif 

  }

  void LED_set_color_soil(struct SensorVal *sns) {
    //while log(resistivity) is linearly correlated with moiusture, it is roughly linear in our range
    //let's scale from blue to green to yellow to red

    byte r1=0,g1=0,b1=0,r2=0,g2=0,b2=0;      
    byte animStyle = 0;

    byte  snspct = (byte) (100*((double)sns->snsValue / sns->limitUpper)); 
    byte snslim = 0;
    byte animStylestep = 0;

    do {
      animStylestep++;
      snslim += 10;
      if (snspct <= snslim) animStyle=animStylestep;        
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
        this->MaxBrightness = 30;
        this->MinBrightness = 5;
        this->sin_T = 1000;
        r1=255;
        g1=0;
        b1=0;
        r2=255;
        g2=0;
        b2=0;
        break;
      case 9: 
        LED_animation_defaults(5);
        this->MaxBrightness = 30;
        this->MinBrightness = 5;
        r1=255;
        g1=0;
        b1=0;
        r2=255;
        g2=50;
        b2=0;
        break;
      case 8:
        LED_animation_defaults(5);
        this->MaxBrightness = 25;
        this->MinBrightness = 5;
        r1=255;
        g1=50;
        b1=0;
        r2=255;
        g2=100;
        b2=0;
        break;
      case 7:
        LED_animation_defaults(5);
        this->MaxBrightness = 25;
        this->MinBrightness = 5;
        r1=255;
        g1=255;
        b1=0;
        r2=150;
        g2=255;
        b2=0;
        break;
      case 6:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=150;
        g1=255;
        b1=0;
        r2=50;
        g2=255;
        b2=0;
        break;
      case 5:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=50;
        g1=255;
        b1=0;
        r2=0;
        g2=255;
        b2=0;
        break;
      case 4:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=0;
        g1=255;
        b1=0;
        r2=0;
        g2=255;
        b2=100;
        break;
      case 3:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=0;
        g1=255;
        b1=50;
        r2=0;
        g2=255;
        b2=200;
        break;
      case 2:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=0;
        g1=255;
        b1=100;
        r2=0;
        g2=150;
        b2=255;
        break;
      case 1:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
        this->MinBrightness = 5;
        r1=0;
        g1=255;
        b1=100;
        r2=0;
        g2=200;
        b2=255;
        break;
      case 0:
        LED_animation_defaults(5);
        this->MaxBrightness = 15;
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

  void LED_animation_defaults(byte anim) {
    this->animation_style = anim;
    switch (anim) {
      case 1: //wave clockwise
        sin_T = 1500; //in other words, T, ms to move through one wavelength
        sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
        MaxBrightness = 15; //sin amp
        MinBrightness=5; 
        break;
      case 2: //wave counter-clockwise
        sin_T = 1500; //in other words, T, ms to move through one wavelength
        sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
        MaxBrightness = 10; //sin amp
        MinBrightness=1; 
        break;
      case 3: //pulse
        sin_T = 30000; //in other words, T ms to move through one wavelength
        sin_L = LEDCOUNT/2; //wavelength, in number of LEDs
        MaxBrightness = 10; //sin amp
        MinBrightness=5; 
        break;
      case 4: //const
        MaxBrightness = 5;
        MinBrightness = 5;
        break;
      case 5: //random gaussian
        MaxBrightness = 50;
        MinBrightness = 5;
          
        break;
    }
  }


} Animation_type;



Animation_type LEDs;


#endif
