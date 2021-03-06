/* Written by Tim Mann and for the purpose of replacing the non functional thermostat for my propane oven.
I have scoured parts from here and there to put this together so the cost to me is minimal.
*** PLEASE NOTE THAT THE TEMPURATURE USED IN THIS SCRIPT WAS SET IN DEGREES FARENHEIT ***
I set this up to run from a 12 volt source being as I have that available and cause my fuel control solonoid is designed for 12 volt.
Here is a list of parts as similar to what I am using as can be:
1 - 16 x 2 LCD display with serial adaptor
1 - Arduino Nano or similar
1 - mini bread board
1 - Gas propane 12 volt electric valve (about $16 USD); http://www.ebay.com/itm/261887206478
1 - bi-color led for indication of state (on/off) and heater state
1 - peizo buzzer
1 - rotary encoder
1 - LM7805 positive voltage regulator
1 - 2SA733 PNP transistor
1 - PC123 optoisolator
1 - STP60NF06 N-CHANNEL 60V 0.014 OHM 60A TO-220 STRIPFET POWER MOSFET
1 - 500mA fuse
6 - 1/8 watt through-hole carbon film resistors
  2 @ 100 ohm
  1 @ 1 k ohm
  1 @ 3k3 ohm
  1 @ 4k7 ohm
A schmatic is posted with this and if you cannot find it then message me here.
This has taken me a number of hours and some trial of various code snippets posted to various forums to make this up.
There are a number of things I didn't know when beginning this but with the right search criteria I was able to find the random peices that total this amalgumation.
I have quite a few notes and even with that this currently takes up 372 bytes (18%) of dynamic memory.
Sketch total is 8,998 bytes (29%) of program storage.
All this has been tested and written in AVRDude v1.6.4
I wanted it to be silent on and off besides the solonoid operating the gas so I used an optoisolator driving a FET rather than a relay for the switching.
This also reduces the load on the *uino (Nano in my case for size reasons).
A list of setable variables for your enjoyment:
*** these are default values set
int minTemp = 100;
int maxTemp = 550;
int hysteresis = 10; This is the swing each side of the set temp eg. 10 = if set point is 225 degrees then it will range a swing of 20 degrees total and so 215 at low end and 235 at high
*** comments attached for the following explane them ***
boolean beepStep = false;
boolean beepOnOff = true;
boolean beepUptoTemp = true;
unsigned long debounceDelay = 200; // As stated below, if you are having debounce issues with your button then increase this number
*/
#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <max6675.h>
#include <Adafruit_GFX.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // set the LCD address to 0x27 for a 16 chars and 2 line display

#define LCD_Backlight 10 // Define backlight driver pin

//Pins  for encoder define
// Half-step mode?
#define HALF_STEP
// Arduino pins the encoder is attached to. Attach the center to ground.
#define ROTARY_PIN1 2
#define ROTARY_PIN2 3
// define to enable weak pullups.
#define ENABLE_PULLUPS

#define DIR_CCW 0x10
#define DIR_CW 0x20

#ifdef HALF_STEP
// Use the half-step state table (emits a code at 00 and 11)
const unsigned char ttable[6][4] = {
  {0x3 , 0x2, 0x1,  0x0}, {0x23, 0x0, 0x1,  0x0},
  {0x13, 0x2, 0x0,  0x0}, {0x3 , 0x5, 0x4,  0x0},
  {0x3 , 0x3, 0x4, 0x10}, {0x3 , 0x5, 0x3, 0x20}
};
#else
// Use the full-step state table (emits a code at 00 only)
const unsigned char ttable[7][4] = {
  {0x0, 0x2, 0x4,  0x0}, {0x3, 0x0, 0x1, 0x10},
  {0x3, 0x2, 0x0,  0x0}, {0x3, 0x2, 0x1,  0x0},
  {0x6, 0x0, 0x4,  0x0}, {0x6, 0x5, 0x0, 0x10},
  {0x6, 0x5, 0x4,  0x0},
};
#endif
volatile unsigned char rotaryState = 0;
const int buttonPin = 4;       // the number of the pushbutton pin
int buttonState;             // the current reading from the input pin
int lastButtonState = HIGH;   // the previous reading from the input pin

// Pin for beeper
int piezoPin = 12;


// Rotery encoder vars
volatile byte aFlag = 0; // let's us know when we're expecting a rising edge on pinA to signal that the encoder has arrived at a detent
volatile byte bFlag = 0; // let's us know when we're expecting a rising edge on pinB to signal that the encoder has arrived at a detent (opposite direction to when aFlag is set)
volatile int tempSet = 100; //this variable stores our current value of encoder position. Change to int or uin16_t instead of byte if you want to record a larger range than 0-255
volatile int oldEncPos = 0; //stores the last encoder position value so we can compare to the current reading and see if it has changed (so we know when to print to the serial monitor)
volatile byte reading = 0; //somewhere to store the direct values we read from our interrupt pins before checking to see if we have moved a whole detent

// Oven set points
int increment = 25; // This is the temp step value.  25 seemed normal for an oven
int minTemp = 100; // Minimum temp set point
int maxTemp = 550; // Maximum temp set point
int hysteresis = 10; // Swing degrees between trun on and turn off
int state = 0; // Used to control on and off state
int firstTemp = 0; // used in conjunction with beepUpToTemp var to know when to beep
boolean beepStep = false; // Make beep at each change of temp set point
boolean beepOnOff = true; // Beep when turning on and off
boolean beepUptoTemp = true; // Beep when up to temp first time

// Stuff for thermocouple
int thermoDO = 5; // Pin number
int thermoCS = 6; // Pin number
int thermoCLK = 7; // Pin number
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO); // Associate vars connecting program with hardware
int vccPin = 8; // Pin number
int gndPin = 9; // Pin number

// Timer vars for timing to allow heater to come on set as initial on delay
unsigned long onTime; // Takes the millis() stamp for calculation
const long timeOut = 5000; // Rated in miliseconds
boolean firstOn = true; // Used for managing delay time for heater from first time on so it is not on right away

// Read cycle timer vars
unsigned long previousMillis = 0;
const long cycleInterval = 500;
float tempF = 0.00;

// --- Debounce for button --- //
// The following variables are unsigned long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

#define HEATER_OUTPUT_PIN 11 // define heater drive pin. Actually drives FET in my case
int HEATER_ON = 1;  //used because may use sink current at some point
int HEATER_OFF = 0;  //used because may use sink current at some point

/* Call this once in setup(). */
void rotary_init() {
  pinMode(ROTARY_PIN1, INPUT);
  pinMode(ROTARY_PIN2, INPUT);
#ifdef ENABLE_PULLUPS
  digitalWrite(ROTARY_PIN1, HIGH);
  digitalWrite(ROTARY_PIN2, HIGH);
#endif
}

/* Read input pins and process for events. Call this either from a
 * loop or an interrupt (eg pin change or timer).
 *
 * Returns 0 on no event, otherwise 0x80 or 0x40 depending on the direction.
 */
unsigned char rotary_process() {
  unsigned char pinstate = (digitalRead(ROTARY_PIN2) << 1) | digitalRead(ROTARY_PIN1);
  rotaryState = ttable[rotaryState & 0xf][pinstate];
  return (rotaryState & 0x30);
}

void setup() {
  lcd.init(); // Initialize LCD
  lcd.backlight(); // Initialize backlight
  lcd.off(); // Start with display off
  rotary_init(); // Initialize encoder
  pinMode(LCD_Backlight, OUTPUT); // Setup ready to drive backlight to desired brightness
  pinMode (buttonPin, INPUT_PULLUP); // uses internal pullup resistor alturnatly you can set this externally
  // Heat Relay Output or drive to Optoisolator
  pinMode(HEATER_OUTPUT_PIN, OUTPUT); // drive output from *uino
  digitalWrite(HEATER_OUTPUT_PIN, HEATER_OFF); // Set initial state of heater
  pinMode(vccPin, OUTPUT); digitalWrite(vccPin, HIGH); // For thermocouple
  pinMode(gndPin, OUTPUT); digitalWrite(gndPin, LOW); // For thermocouple
  delay(500); // wait for MAX chip to stabilize
}

void loop() {
  unsigned char result = rotary_process();
  if(result) {
    if(result == DIR_CW) {
      if (tempSet > (maxTemp - increment)) {
        tempSet = maxTemp;
      } else {
        tempSet = tempSet + increment; //decrement the temp set point
        if (beepStep == true) {
          tone(piezoPin, 3000, 50);
        }
      }
    }else if(result == DIR_CCW) {
      if (tempSet < (minTemp + increment)) {
        tempSet = minTemp;
      } else {
        tempSet = tempSet - increment; //decrement the temp set point
        if (beepStep == true) {
          tone(piezoPin, 3000, 50);
        }
      }
    }
    //Serial.println(result == DIR_CCW ? "LEFT" : "RIGHT");   
  }
  // read the state of the switch into a local variable:
  int buttonReading = digitalRead(buttonPin);
  unsigned long currentTime = millis(); // grab current time
  if ((unsigned long)(currentTime - previousMillis) >= cycleInterval) {
    tempF = thermocouple.readFahrenheit();
    if (state == 1) {
      lcd.setCursor(4, 0);
      lcd.print("        ");
      lcd.setCursor(4, 0);
      lcd.print(tempF);
      lcd.print("\337F");
    }
    previousMillis = millis();
  }
  if (state == 1) {
    if (oldEncPos != tempSet) {
      lcd.setCursor(4, 1); // Position cursor to refresh set value only
      lcd.print(tempSet);
      oldEncPos = tempSet;
    }

    lcd.setCursor(0, 0);
    lcd.print("On:");
    lcd.setCursor(0, 1);
    lcd.print("Set:");
    lcd.print(tempSet);
    lcd.print("\337F");
    analogWrite(LCD_Backlight, 64); // Set backlight brightness value: 0-128
  } else {
    tempSet = 100; // Preset to minium value
    digitalWrite(HEATER_OUTPUT_PIN, HEATER_OFF); // Make sure heater is off to start
  }

  // check to see if you just pressed the button
  // (i.e. the input went from HIGH to LOW),  and you've waited
  // long enough since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    lastDebounceTime = millis(); // reset the debouncing timer
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (buttonReading != buttonState) {
      buttonState = buttonReading;
      // only toggle the heater and display if the new button state is LOW
      if (buttonState == LOW) {
        if (state == 0) {
          state = 1;
          if (beepOnOff == true) {
            tone(piezoPin, 3000, 200); // tone(Pin,Freq,Duration) Duration in miliseconds and Freq in Hz
          }
          lcd.on(); // turn on display
          onTime = millis(); // set time to start count from for delay heater
        } else if (state == 1) {
          state = 0;
          if (beepOnOff == true) {
            tone(piezoPin, 3000, 100);
          }
          firstTemp = 0; // Reset to 0 when truning off
          firstOn = true; // Reset to false when truning of
          tempSet = 100; // Reset temp to default lowest
          // clear screen
          lcd.setCursor(0, 0);
          lcd.print("                ");
          lcd.setCursor(0, 1);
          lcd.print("                ");
          lcd.setCursor(0, 0);
          analogWrite(LCD_Backlight, LOW); // turn off back light
          lcd.off(); // turn off display
          digitalWrite(HEATER_OUTPUT_PIN, 0); // make sure heater is turned off regardless of state
        }
      }
    }
  }
  if (state == 1) {
    if (tempF <= (tempSet - hysteresis)) {
      if (firstOn == true) {
        if (millis() - onTime > timeOut) { // one-shot timer for delaying on time a few seconds
          heaterOn();
          onTime = 0;
          firstOn = false; // Set to false so we know it is in first on warm-up mode
        }
      } else {
        heaterOn();
      }
    } else if (tempF >= (tempSet + hysteresis)) { // do heater On Off cycling
      if (firstTemp == 0) {
        if (beepUptoTemp == true) {
          tone(piezoPin, 3000, 100);
        }
        firstTemp = 1;
        firstOn = false;
      }
      heaterOff();
    }
  }

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}

//------------------------------------------
// Heater controls
void heaterOn() {
  digitalWrite(HEATER_OUTPUT_PIN, HIGH);
}

void heaterOff() {
  digitalWrite(HEATER_OUTPUT_PIN, LOW);
}
