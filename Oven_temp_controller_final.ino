/* Written by Tim Mann and for the purpose of replacing the non functional thermostat for my propane oven.
I have scoured parts from here and there to put this together so the cost to me is minimal.

*** PLEASE NOTE THAT THE TEMPURATURE USED IN THIS SCRIPT WAS SET IN DEGREES FARENHEIT ***

I set this up to run from a 12 volt source being as I have that available and cause my fuel control solonoid is designed for 12 volt.

Here is a list of parts as similar to what I am using:
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
static int encoderPinA = 2; // Our first hardware interrupt pin is digital pin 2
static int encoderPinB = 3; // Our second hardware interrupt pin is digital pin 3
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

// --- Debounce for button --- //
// The following variables are unsigned long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flickers

#define HEATER_OUTPUT_PIN 11 // define heater drive pin. Actually drives FET in my case
int HEATER_ON = 1;  //used because may use sink current at some point
int HEATER_OFF = 0;  //used because may use sink current at some point

void setup() {
  //Serial.begin(9600);
  lcd.init(); // Initialize LCD
  lcd.backlight(); // Initialize backlight
  lcd.off();
  pinMode(LCD_Backlight, OUTPUT); // Setup ready to drive backlight to desired brightness
  // Encoder setup
  pinMode (encoderPinA, INPUT);
  pinMode (encoderPinB, INPUT);
  pinMode (buttonPin, INPUT_PULLUP); // uses internal pullup resistor alturnatly you can set this externally
  attachInterrupt(0, PinA, RISING); // for encoder
  attachInterrupt(1, PinB, RISING); // for encoder
  // Heat Relay Output or drive to Optoisolator
  pinMode(HEATER_OUTPUT_PIN, OUTPUT); // drive output from *uino
  digitalWrite(HEATER_OUTPUT_PIN, HEATER_OFF); // Set initial state of heater
  pinMode(vccPin, OUTPUT); digitalWrite(vccPin, HIGH); // For thermocouple
  pinMode(gndPin, OUTPUT); digitalWrite(gndPin, LOW); // For thermocouple
  delay(500); // wait for MAX chip to stabilize
}

void PinA() {
  cli(); //stop interrupts happening before we read pin values
  reading = PIND & 0xC; // read all eight pin values then strip away all but pinA and pinB's values
  if (reading == B00001100 && aFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    if (tempSet < (minTemp + increment)) {
      tempSet = minTemp;
    } else {
      tempSet = tempSet - increment; //decrement the temp set point
      if (beepStep == true) {
        tone(piezoPin, 3000, 50);
      }
    }
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00000100) bFlag = 1; //signal that we're expecting pinB to signal the transition to detent from free rotation
  sei(); //restart interrupts
}

void PinB() {
  cli(); //stop interrupts happening before we read pin values
  reading = PIND & 0xC; //read all eight pin values then strip away all but pinA and pinB's values
  if (reading == B00001100 && bFlag) { //check that we have both pins at detent (HIGH) and that we are expecting detent on this pin's rising edge
    //tempSet = tempSet + increment; //increment the encoder's position count
    if (tempSet > (maxTemp - increment)) {
      tempSet = maxTemp;
    } else {
      tempSet = tempSet + increment; //decrement the temp set point
      if (beepStep == true) {
        tone(piezoPin, 3000, 50);
      }
    }
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00001000) aFlag = 1; //signal that we're expecting pinA to signal the transition to detent from free rotation
  sei(); //restart interrupts
}

void loop() {
  // read the state of the switch into a local variable:
  int buttonReading = digitalRead(buttonPin);
  //unsigned long currentMillis = millis();
  if (state == 1) {
    if (oldEncPos != tempSet) {
      lcd.setCursor(4, 1); // Position cursor to refresh set value only
      lcd.print(tempSet);
      oldEncPos = tempSet;
    }
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
          lcd.setCursor(0, 0);
          lcd.print("On:");
          lcd.setCursor(4, 0);
          lcd.print(thermocouple.readFahrenheit());
          lcd.print("\337F");
          lcd.setCursor(0, 1);
          lcd.print("Set:");
          lcd.print(tempSet);
          lcd.print("\337F");
          analogWrite(LCD_Backlight, 64); // Set backlight brightness value: 0-128
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
    if (thermocouple.readFahrenheit() <= tempSet - hysteresis) {
      if (digitalRead(HEATER_OUTPUT_PIN) == LOW) {
        if(firstOn == true) {
          if (millis() - onTime > timeOut) {
            heaterOn();
            onTime = 0;
            firstOn = false; // Set to false so we know it is in first on warm-up mode
          }
        }else{
          heaterOn();
        }
      }
    } else if (thermocouple.readFahrenheit() >= tempSet + hysteresis) { // do heater On Off cycling
      if (beepUptoTemp == true) {
        tone(piezoPin, 2000, 200);
      }
      if (firstTemp == 0) {
        firstTemp = 1;
        firstOn = false;
      }
      if (digitalRead(HEATER_OUTPUT_PIN) == HIGH) {
        heaterOff();
      }
    }
  }
  // set the LED:
  //digitalWrite(ledPin, ledState);

  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}

//------------------------------------------
// Heater controls
void heaterOn() {
  digitalWrite(HEATER_OUTPUT_PIN, HEATER_ON);
}

void heaterOff() {
  digitalWrite(HEATER_OUTPUT_PIN, HEATER_OFF);
}
