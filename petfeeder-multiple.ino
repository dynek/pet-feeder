// required libraries
#include <hd44780.h>
#include <hd44780ioClass/hd44780_pinIO.h>
#include "RTClib.h"
#include <Stepper.h>
#include <Wire.h>

// LCD & keypad
const uint8_t rs = 8, en = 9, db4 = 4, db5 = 5, db6 = 6, db7 = 7, bl = 10, blLevel = HIGH;
hd44780_pinIO lcd(rs, en, db4, db5, db6, db7, bl, blLevel);
#define LCD_STR_LEN 16
bool quickRefresh = true;
bool editMode = false;
uint8_t editPosition = 0; // 0 = hours & 3 = minutes
uint8_t debounceDelay = 150;
unsigned long previousMillis = millis();
const unsigned long noDisplayPeriod = 30000;  // the value is a number of milliseconds

// keypad buttons
#define btnRight  0
#define btnUp     1
#define btnDown   2
#define btnLeft   3
#define btnSelect 4
#define btnNone   5

// RTC
RTC_DS1307 rtc;
DateTime now;
#define DEFAULT_TIMER_HOURS   "6"
#define DEFAULT_TIMER_MINUTES "0"
#define DEFAULT_RECURRENCE    "6"
#define DEFAULT_QUANTITY      "1"
#define ADDRESS_TIMER         0x0
#define ADDRESS_RECURRENCE    0x2
#define ADDRESS_QUANTITY      0x3

// Stepper
#define STEPPER_RPM 30 // rotations per minute
#define STEPS_PER_REVOLUTION 200 // number of steps of the motor (360 / 1.8°)
const uint8_t foodForward = STEPS_PER_REVOLUTION/4;
const uint8_t foodBackward = foodForward/3;
Stepper stepper(STEPS_PER_REVOLUTION, 2, 3, 11, 12);

// Menu
typedef struct Menu {
  void (* display) ();
  char title[(LCD_STR_LEN+1)];
  void (* leftAction) ();
  void (* upAction) ();
  void (* downAction) ();
  void (* rightAction) ();
  struct Menu *leftMenu;
  struct Menu *rightMenu;
} Menu;

#define SUMMARY_TITLE F("Il est")
#define DOSAGE_TITLE F("Heure 1er dosage")
#define RECURRENCE_TITLE F("Recurrence")
#define QUANTITY_TITLE F("Quantite")
#define MANUAL_TITLE F("Dosage manuel")
#define SET_TIME_TITLE F("Reglage heure")
Menu menu1, menu2, menu3, menu4, menu5, menu6;
Menu* currentMenu;
Menu* todisplayMenu;

void displaySummary(void) {
  if(quickRefresh) {
    previousMillis = millis();
    lcd.setCursor(0,0);
    lcd.print(currentMenu->title);
  }
  
  if(quickRefresh || (millis() - previousMillis >= 1000)) {
    // display current time
    char buffer[9];
    sprintf(buffer, "%02hhu:%02hhu:%02hhu", now.hour(), now.minute(), now.second());
    lcd.setCursor(strlen(currentMenu->title) + 1,0);
    lcd.print(buffer);

    // display feeding time
    sprintf(buffer, "1-%02d:%02d", rtc.readnvram(ADDRESS_TIMER), rtc.readnvram((ADDRESS_TIMER+1)));
    lcd.setCursor(0,1);
    lcd.print(buffer);

    // display recurrence
    sprintf(buffer, "*/%dh", rtc.readnvram(ADDRESS_RECURRENCE));
    lcd.setCursor((LCD_STR_LEN-strlen(buffer)),1);
    lcd.print(buffer);

    previousMillis = millis();
  }
}

void displaySetTime(void) {
  static bool blink = false;
  if(quickRefresh || editMode) {
    if(quickRefresh) {
      previousMillis = millis();
      lcd.setCursor(0,0);
      lcd.print(currentMenu->title);
    }

    if(quickRefresh || (editMode && (millis() - previousMillis >= 250))) {
      if(quickRefresh || !blink) {
        char time[6];
        if(currentMenu == &menu2) {
          sprintf(time, "%02d:%02d", rtc.readnvram(ADDRESS_TIMER), rtc.readnvram((ADDRESS_TIMER+1)));
        } else {
          sprintf(time, "%02hhu:%02hhu", now.hour(), now.minute());
        }
        lcd.setCursor(0,1);
        lcd.print(time);
      } else if(blink) {
        lcd.setCursor(editPosition,1);
        lcd.print(F("  "));
      }

      if(editMode) {
        blink = !blink;
        previousMillis = millis();
      }
    }
  }
}

void moveTimeLeft(void) { editPosition = 0; }
void moveTimeRight(void) { editPosition = 3; }

void increaseTime(void) {
  uint8_t address, value;
  
  if(currentMenu == &menu2) {
    address = ADDRESS_TIMER;
    if(editPosition == 3) { address++; }
    value = rtc.readnvram(address);
  } else {
    if(editPosition == 0) { value = now.hour(); }
    if(editPosition == 3) { value = now.minute(); }
  }

  if(editPosition==0 && value < 23) { value++; }
  if(editPosition==3 && value < 55 && currentMenu == &menu2) { value = value + 5; }
  if(editPosition==3 && value < 59 && currentMenu != &menu2) { value = value + 1; }

  if(currentMenu == &menu2) {  
    rtc.writenvram(address, value);
  } else {
    if(editPosition==0) {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), value, now.minute(), 0));
    } else {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), value, 0));
    }
  }
}

void decreaseTime(void) {
  uint8_t address, value;
  
  if(currentMenu == &menu2) {
    address = ADDRESS_TIMER;
    if(editPosition == 3) { address++; }
    value = rtc.readnvram(address);
  } else {
    if(editPosition == 0) { value = now.hour(); }
    if(editPosition == 3) { value = now.minute(); }
  }

  if(editPosition==0 && value > 0) { value--; }
  if(editPosition==3 && value > 0 && currentMenu == &menu2) { value = value - 5; }
  if(editPosition==3 && value > 0 && currentMenu != &menu2) { value = value - 1; }

  if(currentMenu == &menu2) {  
    rtc.writenvram(address, value);
  } else {
    if(editPosition==0) {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), value, now.minute(), 0));
    } else {
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), value, 0));
    }
  }
}

void displaySetQuantity(void) {
  static bool blink = false;
  if(quickRefresh || editMode) {
    if(quickRefresh) {
      previousMillis = millis();
      blink = false;
      lcd.setCursor(0,0);
      lcd.print(currentMenu->title);
    }
    
    if(quickRefresh || (editMode && (millis() - previousMillis >= 200))) {
      lcd.setCursor(0,1);
      if(quickRefresh || !blink) {
        uint8_t quantity = rtc.readnvram(ADDRESS_QUANTITY);
        for(uint8_t i = 0; i < quantity; i++) { lcd.print(F("O")); }
      } else if(blink) {
        for(uint8_t n = 0; n <= LCD_STR_LEN; n++) { lcd.print(F(" ")); }
      }

      if(editMode) {
        blink = !blink;
        previousMillis = millis();
      }
    }
  }
}

void increaseQuantity() {
  uint8_t quantity;
  quantity = rtc.readnvram(ADDRESS_QUANTITY);
  if(quantity < LCD_STR_LEN) { quantity++; }
  rtc.writenvram(ADDRESS_QUANTITY, quantity);
}

void decreaseQuantity() {
  uint8_t quantity;
  quantity = rtc.readnvram(ADDRESS_QUANTITY);
  if(quantity > 1) { quantity--; }
  rtc.writenvram(ADDRESS_QUANTITY, quantity);
}

void displaySetRecurrence() {
  static bool blink = false;
  if(quickRefresh || editMode) {
    if(quickRefresh) {
      previousMillis = millis();
      lcd.setCursor(0,0);
      lcd.print(currentMenu->title);
    }

    if(quickRefresh || (editMode && (millis() - previousMillis >= 250))) {
      if(quickRefresh || !blink) {
        char buffer[3];
        sprintf(buffer, "%02dh", rtc.readnvram(ADDRESS_RECURRENCE));
        lcd.setCursor(0,1);
        lcd.print(buffer);
      } else if(blink) {
        lcd.setCursor(editPosition,1);
        lcd.print(F("  "));
      }

      if(editMode) {
        blink = !blink;
        previousMillis = millis();
      }
    }
  }
}

void increaseRecurrence() {
  uint8_t address, value;
  address = ADDRESS_RECURRENCE;
  value = rtc.readnvram(address);
  if(value < 10) { value++; }
  rtc.writenvram(address, value);
}

void decreaseRecurrence() {
  uint8_t address, value;
  address = ADDRESS_RECURRENCE;
  value = rtc.readnvram(address);
  if(value > 4) { value--; }
  rtc.writenvram(address, value);
}

void displayTrigger() {
  if(quickRefresh) {
    lcd.setCursor(0,0);
    lcd.print(currentMenu->title);
  }
}

void feedAnimal(uint8_t quantity = 1) {
  uint8_t volume = 10 * quantity;
  uint8_t counter = 0;

  while(counter < volume){
    stepper.step(foodForward);
    delay(100);
    stepper.step(-foodBackward);
    delay(100);
    counter++;
  }
  
  digitalWrite(2,LOW);
  digitalWrite(3,LOW);
  digitalWrite(11,LOW);
  digitalWrite(13,LOW);
}

void navigateMenu(uint8_t button) {
  switch (button) {
    case btnSelect:
      if(currentMenu != &menu1 && currentMenu != &menu6) {
        editMode = !editMode;
        if(!editMode) quickRefresh = true; else editPosition = 0;
      }
      if(currentMenu == &menu6) feedAnimal();
      break;
    case btnUp:
      if (editMode && currentMenu->upAction) currentMenu->upAction();
      break;
    case btnDown:
      if (editMode && currentMenu->downAction) currentMenu->downAction();
      break;
    case btnLeft:
      if (editMode && currentMenu->leftAction) currentMenu->leftAction();
      if (!editMode && currentMenu->leftMenu) todisplayMenu = currentMenu->leftMenu;
      break;
    case btnRight:
      if (editMode && currentMenu->rightAction) currentMenu->rightAction();
      if (!editMode && currentMenu->rightMenu) todisplayMenu = currentMenu->rightMenu;
      break;
  }
}

void setup() {
  Serial.begin(115200);

  if (! rtc.begin()) {
    // Couldn't find RTC
    while (1);
  }

  if (! rtc.isrunning()) {
    // RTC is NOT running!
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    // clear RAM
    for (uint8_t i = 0; i <= 55; i++) { rtc.writenvram(i, 0x00); }

    // write default feeding time & quantity
    rtc.writenvram(ADDRESS_TIMER, atoi(DEFAULT_TIMER_HOURS));
    rtc.writenvram((ADDRESS_TIMER+1), atoi(DEFAULT_TIMER_MINUTES));
    rtc.writenvram(ADDRESS_RECURRENCE, atoi(DEFAULT_RECURRENCE));
    rtc.writenvram(ADDRESS_QUANTITY, atoi(DEFAULT_QUANTITY));
  }

  // Stepper ports as output
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(12, OUTPUT);
  stepper.setSpeed(STEPPER_RPM);

  // setup the menu and links
  menu1.leftMenu = &menu6;
  menu1.rightMenu = &menu2;
  menu1.display = displaySummary;
  strcpy_PF(menu1.title, SUMMARY_TITLE);

  menu2.leftMenu = &menu1;
  menu2.rightMenu = &menu3;
  menu2.display = displaySetTime;
  menu2.upAction = increaseTime;
  menu2.downAction = decreaseTime;
  menu2.leftAction = moveTimeLeft;
  menu2.rightAction = moveTimeRight;
  strcpy_PF(menu2.title, DOSAGE_TITLE);

  menu3.leftMenu = &menu2;
  menu3.rightMenu = &menu4;
  menu3.display = displaySetRecurrence;
  menu3.upAction = increaseRecurrence;
  menu3.downAction = decreaseRecurrence;
  strcpy_PF(menu3.title, RECURRENCE_TITLE);

  menu4.leftMenu = &menu3;
  menu4.rightMenu = &menu5;
  menu4.display = displaySetQuantity;
  menu4.leftAction = decreaseQuantity;
  menu4.rightAction = increaseQuantity;
  strcpy_PF(menu4.title, QUANTITY_TITLE);

  menu5.leftMenu = &menu4;
  menu5.rightMenu = &menu6;
  menu5.display = displaySetTime;
  menu5.upAction = increaseTime;
  menu5.downAction = decreaseTime;
  menu5.leftAction = moveTimeLeft;
  menu5.rightAction = moveTimeRight;
  strcpy_PF(menu5.title, SET_TIME_TITLE);

  menu6.leftMenu = &menu5;
  menu6.rightMenu = &menu1;
  menu6.display = displayTrigger;
  strcpy_PF(menu6.title, MANUAL_TITLE);

  //for (uint8_t i = 0; i <= 5; i++) { printnvram(i); }
  
  // Initialize LCD
  lcd.begin(16, 2);
}

/*void printnvram(uint8_t address) {
  Serial.print("Address 0x");
  Serial.print(address, HEX);
  Serial.print(" = 0x");
  Serial.println(rtc.readnvram(address), HEX);
}*/

uint8_t read_LCD_buttons(){
  uint16_t adc_key_in = analogRead(0); // read the value from the sensor
  // my buttons when read are centered at these values: 0, 144, 329, 504, 741
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNone; // We make this the 1st option for speed reasons since it will be the most likely result
  // For V1.1 us this threshold
  if (adc_key_in < 50)   return btnRight;
  if (adc_key_in < 195)  return btnUp;
  if (adc_key_in < 380)  return btnDown;
  if (adc_key_in < 555)  return btnLeft;
  if (adc_key_in < 790)  return btnSelect;
  return btnNone; // when all others fail, return this...
}

void loop() {
  uint8_t button = read_LCD_buttons();
  static unsigned long lastInteractionMillis = 0;
  static bool lcdOn = true;
  
  // Time to feed the cats?
  now = rtc.now();
  if(!editMode && now.second() == 0) {
    uint8_t minute = rtc.readnvram((ADDRESS_TIMER+1));
    if(now.minute() == minute) {
      uint8_t hour = rtc.readnvram(ADDRESS_TIMER);
      uint8_t recurrence = rtc.readnvram((ADDRESS_RECURRENCE));
      while(hour < 24) {
        if(now.hour() == hour) feedAnimal(rtc.readnvram(ADDRESS_QUANTITY));
        hour += recurrence;
      }
    }
  }
  
  // button was pressed
  if (button != btnNone) {
    // Dismiss bounces, yet keeping it possible to keep pushing a button
    if ((millis() - lastInteractionMillis) <= debounceDelay) { return; }

    // Reset last interaction millis
    lastInteractionMillis = millis();

    if (lcdOn) {
      navigateMenu(button);
    } else {
      lcdOn = true;
      lcd.backlight();
      return;
    }
  } else {
    // turn backlight off
    if (((millis() - lastInteractionMillis) > noDisplayPeriod) && lcdOn == true) {
      currentMenu = NULL;
      todisplayMenu = &menu1;
      lcdOn = editMode = false;
      quickRefresh = true;
      lcd.clear();
      lcd.noBacklight();
    }
  }

  if(lcdOn) {
    // Display menu
    if(todisplayMenu == NULL) { todisplayMenu = &menu1; }
    if(todisplayMenu != currentMenu) {
      quickRefresh = true;
      currentMenu = todisplayMenu;
      lcd.clear();
    }
    currentMenu->display();
  }

  quickRefresh = false;
}
