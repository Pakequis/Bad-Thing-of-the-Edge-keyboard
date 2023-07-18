/*
  Bad Thing of the Edge Mechanical Keyboard Firmware
  by Rodrigo Feliciano
  https://www.github.com/pakequis
  https://www.youtube.com/pakequis
*/

#include <Arduino.h>
#include <EEPROM.h>
/*
  Adafruit_TinyUSB library and example from:
  https://github.com/adafruit/Adafruit_TinyUSB_Arduino
*/
#include "Adafruit_TinyUSB.h"

#define LED1  0   /* Left LED   */
#define LED2  28  /* Center LED */
#define LED3  16  /* Top LED    */
#define LED4  11  /* Right LED  */

/* LED Effects */
#define LED_BREATH    0 
#define LED_KEYPRESS  1
#define LED_NOISE     2
#define LED_QUAKE     3
#define LED_BREATH2   4
#define LED_NONE      5

#define KEYNUMBER 20  /* Number of keys */
#define ACTIVESTATE false

/* Key status for keys (core 0) to LED effects (core 1) */
#define NOKEY         0
#define KEYPRESS      1
#define CHANGE_EFFECT 2

uint8_t const effects[] = {LED_NONE, LED_BREATH, LED_BREATH2, LED_NOISE, LED_QUAKE, LED_KEYPRESS};
uint8_t effects_size = sizeof(effects) / sizeof(effects[0]);  

/* Effects arrays */

/* LED breath effect */
uint8_t const breath_array[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120,
                                  130, 140, 155, 165, 175, 175, 165, 155, 140, 130, 
                                  120, 110, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};

uint8_t breath_size = sizeof(breath_array) / sizeof(breath_array[0]);                       

/* Quake flicker light effect */
/* Original quake flicker style from https://github.com/id-Software/Quake/blob/master/qw-qc/world.qc 
   Values from the style 10 and tick of 100 ms: 
   // 10 FLUORESCENT FLICKER
	 lightstyle(10, "mmamammmmammamamaaamammma");
*/
uint8_t const quake_array[] = {255, 255, 0, 255, 0, 255, 255, 255, 255, 0, 255, 255, 
                         0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 255, 255, 0};

uint8_t quake_size = sizeof(quake_array) / sizeof(quake_array[0]);              

/* Key press effect */
uint8_t const keypress_array[] = { 0, 10, 20, 30, 40, 50, 60, 80, 100, 120, 140, 160, 180,
                                  200, 220, 240, 255, 255, 240, 220, 200, 180, 160, 
                                  140, 120, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};

uint8_t keypress_size = (uint8_t) (sizeof(keypress_array) / sizeof(keypress_array[0]));   

// HID report descriptor using TinyUSB's template
// Single Report (no ID) descriptor
uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD()
};

// USB HID object.
// desc report, desc len, protocol, interval, use out endpoint
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD, 2, false);

/*
  Key Map:
  [0] [1] [2] [3] [4]
  [5] [6] [7] [8] [9]
    [a]   [b]
                 [c]    
    [d] [e] [f]
                 [g]
    [h]          [i]
                 [j]

  0 SW5   GPIO4   | a SW13  GPIO26
  1 SW2   GPIO1   | b SW15  GPIO21
  2 SW1   GPIO7   | c SW10  GPIO13
  3 SW17  GPIO19  | d SW12  GPIO27
  4 SW19  GPIO17  | e SW14  GPIO22
  5 SW6   GPIO5   | f SW16  GPIO20
  6 SW3   GPIO2   | g SW9   GPIO12
  7 SW4   GPIO3   | h SW11  GPIO6
  8 SW18  GPIO18  | i SW8   GPIO10
  9 SW20  GPIO14  | j SW7   GPIO9
*/

/* Key pins */
int key_pins[] = { D4, D1, D7, D19, D17, D5, D2, D3, D18, D14, 
                  D26, D21, D13, D27, D22, D20, D12, D6, D10, D9 };

/* keycodes for Diablo III Game */
uint8_t hidcode[] = { HID_KEY_NONE, HID_KEY_PRINT_SCREEN, HID_KEY_Z, HID_KEY_O, HID_KEY_Y,
                      HID_KEY_ENTER, HID_KEY_SPACE, HID_KEY_G, HID_KEY_F, HID_KEY_S, 
                      HID_KEY_ESCAPE, HID_KEY_I, HID_KEY_M, HID_KEY_SHIFT_LEFT, HID_KEY_2, 
                      HID_KEY_3, HID_KEY_4, HID_KEY_1, HID_KEY_ALT_LEFT, HID_KEY_Q
                    };

int led_count = 0;
int led_count2 = breath_size / 2;

bool fifo_flag = false;
bool effect_change_flag = false;

uint32_t core1_var = NOKEY;

byte EEPROM_value = 0;

/********************************
  Core 0 - USB Keyboard 
 ********************************/
void setup() 
{
  TinyUSB_Device_Init(0);
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_KEYBOARD);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("TinyUSB Keyboard");

  usb_hid.begin();

  // Set up pin as input
  for (byte i=0; i< KEYNUMBER; i++)
  {
    pinMode(key_pins[i], INPUT_PULLUP);
  }

  delay(100);
  
  // wait until device mounted
  while( !TinyUSBDevice.mounted() ) delay(1);
};

void loop() 
{
  // poll gpio once each 2 ms
  delay(2);

  // used to avoid send multiple consecutive zero report for keyboard
  static bool keyPressedPreviously = false;

  uint8_t count = 0;
  uint8_t keycode[6] = { 0 };

  /* Check LED effect key */
  if (ACTIVESTATE == digitalRead(key_pins[0]))
  {
    while(ACTIVESTATE == digitalRead(key_pins[0]))
    {
      delay(10); /* debounce */
    }

    fifo_flag = rp2040.fifo.push_nb(CHANGE_EFFECT);
    delay(100);
  }

  //scan normal key and send report
  for(uint8_t i = 1; i < KEYNUMBER; i++)
  {
    if ( ACTIVESTATE == digitalRead(key_pins[i]) )
    {
      // if pin is active (low), add its hid code to key report
      keycode[count++] = hidcode[i];

      // 6 is max keycode per report
      if (count == 6) break;
    }
  }

  if ( TinyUSBDevice.suspended() && count )
  {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    TinyUSBDevice.remoteWakeup();
  }

  // skip if hid is not ready e.g still transferring previous report
  if ( !usb_hid.ready() ) return;

  if ( count )
  {
    // Send report if there is key pressed
    uint8_t const report_id = 0;
    uint8_t const modifier = 0;

    /* Inform Core 1 */
    fifo_flag = rp2040.fifo.push_nb(KEYPRESS);

    keyPressedPreviously = true;
    usb_hid.keyboardReport(report_id, modifier, keycode);
  } 
  else
  {
    // Send All-zero report to indicate there is no keys pressed
    // Most of the time, it is, though we don't need to send zero report
    // every loop(), only a key is pressed in previous loop()
    if ( keyPressedPreviously )
    {
      keyPressedPreviously = false;
      usb_hid.keyboardRelease(0);
    }
  }
}

/***************************************
  Core 1 - LED control
****************************************/
void setup1() 
{
  /* EEPROM for save the current LED effect */
  EEPROM.begin(256);

  /* All LED pins as outputs */
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);

  /* All LED off */
  analogWrite(LED1, 0);
  analogWrite(LED2, 0);
  analogWrite(LED3, 0);
  analogWrite(LED4, 0);

  delay(250);

  /* Get the LED effect from EEPROM */
  EEPROM_value = EEPROM.read(0);
}

void loop1() 
{
  /* Check if Core 0 send anything */
  fifo_flag = rp2040.fifo.pop_nb(&core1_var);

  /* LED Effect change */
  if(core1_var == CHANGE_EFFECT)
  {
        EEPROM_value++;
        if (EEPROM_value >= effects_size)
        {
          EEPROM_value = 0;
        }

        /* Save the new effect in EEPROM */
        EEPROM.write(0, EEPROM_value);
        EEPROM.commit();

        /* Blink the center LED to inform the change */
        analogWrite(LED2, 255);
        delay(100);
        analogWrite(LED2, 0);
        delay(100);

        /* Clean the FIFO */
        while(rp2040.fifo.available()) 
        {
          fifo_flag = rp2040.fifo.pop_nb(&core1_var);
        }
        core1_var = NOKEY;
        delay(100);
  }

  /* Check the current LED effect */
  switch (effects[EEPROM_value])
  {
    /* All LEDs up and down bright */
    case LED_BREATH:
      analogWrite(LED1, breath_array[led_count]);
      analogWrite(LED2, breath_array[led_count]);
      analogWrite(LED3, breath_array[led_count]);
      analogWrite(LED4, breath_array[led_count]);
      led_count++;
      if(led_count >= breath_size) led_count = 0;
      delay(100);
      break;
  
    /* Random noise in all LEDs */
    case LED_NOISE:
      led_count = (int)random(127) + 100;
      analogWrite(LED1, led_count);
      analogWrite(LED2, led_count);
      analogWrite(LED3, led_count);
      analogWrite(LED4, led_count);
      delay(random(100));
      break;

    /* LEDs Up and Down, first the center */
    case LED_BREATH2:
      analogWrite(LED1, breath_array[led_count2]);
      analogWrite(LED2, breath_array[led_count]);
      analogWrite(LED3, breath_array[led_count2]);
      analogWrite(LED4, breath_array[led_count2]);
      led_count++;
      led_count2++;
      if(led_count >= breath_size) led_count = 0;
      if(led_count2 >= breath_size) led_count2 = 0;
      delay(100);
      break;

    /* Effect activate on keypress */
    case LED_KEYPRESS:
      //fifo_flag = rp2040.fifo.pop_nb(&core1_var);
      if(core1_var == KEYPRESS)
      {
        for (led_count = 0; led_count < keypress_size; led_count++)
        {
          analogWrite(LED1, keypress_array[led_count]);
          analogWrite(LED2, keypress_array[led_count]);
          analogWrite(LED3, keypress_array[led_count]);
          analogWrite(LED4, keypress_array[led_count]);
          delay(10);
        }

        /* Clean the FIFO */
        while(rp2040.fifo.available()) 
        {
          fifo_flag = rp2040.fifo.pop_nb(&core1_var);
        }
        core1_var = NOKEY;
        delay(100);
        led_count = 0;
      }
      break;

    /* Classical Quake flicker light */
    case LED_QUAKE:
      int value;
      value = quake_array[led_count];
      analogWrite(LED1, value);
      analogWrite(LED2, value);
      analogWrite(LED3, value);
      analogWrite(LED4, value);

      delay(100);
      led_count++;
      if (led_count >= quake_size) led_count = 0;
      break;

    /* No Effect */
    default:
      analogWrite(LED1, 0);
      analogWrite(LED2, 0);
      analogWrite(LED3, 0);
      analogWrite(LED4, 0);
      break;
  }
}
