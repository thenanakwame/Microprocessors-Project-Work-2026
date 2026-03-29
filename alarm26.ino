#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Keypad.h>
#include <EEPROM.h>

// ==================== PIN DEFINITIONS ====================
#define BUZZER_PIN    5

// Keypad (rows: 10,11,12,13 ; columns: 9,8,7,6)
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {10, 11, 12, 13};
byte colPins[COLS] = {9, 8, 7, 6};
Adafruit_Keypad keypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// RTC (DAT=3, CLK=2, RST=4)
ThreeWire myWire(3, 2, 4);
RtcDS1302<ThreeWire> Rtc(myWire);

// LCD – try 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==================== ALARM VARIABLES ====================
int alarmHour = 12;
int alarmMinute = 0;
bool alarmEnabled = false;
bool alarmRinging = false;

bool settingMode = false;        // alarm setting mode
int settingStep = 0;             // 0 = hour, 1 = minute

// Snooze
int snoozeMinutes = 5;           // default, loaded from EEPROM
bool snoozeSetting = false;      // in snooze adjustment mode
const int snoozeMin = 1;
const int snoozeMax = 30;

unsigned long snoozeUntil = 0;   // millis() when snooze ends

unsigned long lastKeyTime = 0;
const unsigned long keyDebounce = 200;

// Temporary message for LCD
String tempMessage = "";
unsigned long tempMessageEnd = 0;

// EEPROM address for snooze duration
const int EEPROM_SNOOZE_ADDR = 0;

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Load snooze duration from EEPROM
  int val = EEPROM.read(EEPROM_SNOOZE_ADDR);
  if (val >= snoozeMin && val <= snoozeMax) {
    snoozeMinutes = val;
  } else {
    snoozeMinutes = 5;
    EEPROM.write(EEPROM_SNOOZE_ADDR, snoozeMinutes);
  }

  Rtc.Begin();
  // Force RTC to compile time (remove comment to set time each boot)
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  Rtc.SetDateTime(compiled);
  Serial.println("RTC forced to compile time.");
  RtcDateTime now = Rtc.GetDateTime();
  Serial.print("RTC time now: ");
  printDateTime(now);

  keypad.begin();

  // Show greeting
  showGreeting();

  Serial.println("System ready.");
  Serial.println("A: Set alarm   *: Toggle alarm on/off   #: Set snooze duration");
  Serial.println("B/C: Adjust   D: Snooze when ringing");
}

void printDateTime(const RtcDateTime& dt) {
  char buf[20];
  snprintf_P(buf, sizeof(buf), PSTR("%04u-%02u-%02u %02u:%02u:%02u"),
             dt.Year(), dt.Month(), dt.Day(), dt.Hour(), dt.Minute(), dt.Second());
  Serial.println(buf);
}

void showGreeting() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hi Dan!");
  lcd.setCursor(0, 1);
  lcd.print("Alarm Sytem");
  delay(2000);
  lcd.clear();
}

void getCurrentTime(int &h, int &m, int &s) {
  RtcDateTime now = Rtc.GetDateTime();
  h = now.Hour();
  m = now.Minute();
  s = now.Second();
}

void updateDisplay(int h, int m, int s) {
  lcd.setCursor(0, 0);
  if (h < 10) lcd.print('0');
  lcd.print(h); lcd.print(':');
  if (m < 10) lcd.print('0');
  lcd.print(m); lcd.print(':');
  if (s < 10) lcd.print('0');
  lcd.print(s);

  // Show temporary message if active
  if (tempMessageEnd > millis()) {
    lcd.setCursor(0, 1);
    lcd.print(tempMessage);
    for (int i = tempMessage.length(); i < 16; i++) lcd.print(' ');
    return;
  }

  lcd.setCursor(0, 1);
  if (snoozeSetting) {
    lcd.print("Snooze: ");
    lcd.print(snoozeMinutes);
    lcd.print(" min   ");
    // Blink the number
    if ((millis() / 500) % 2) {
      lcd.setCursor(9, 1);
      lcd.print("   ");
    }
  }
  else if (settingMode) {
    lcd.print("Set: ");
    if (alarmHour < 10) lcd.print('0');
    lcd.print(alarmHour); lcd.print(':');
    if (alarmMinute < 10) lcd.print('0');
    lcd.print(alarmMinute);
    bool blink = (millis() / 500) % 2;
    if (settingStep == 0) {
      lcd.setCursor(5, 1);
      if (blink) lcd.print("  ");
      else {
        lcd.print(alarmHour < 10 ? "0" : String(alarmHour / 10));
        lcd.print(alarmHour % 10);
      }
    } else {
      lcd.setCursor(8, 1);
      if (blink) lcd.print("  ");
      else {
        lcd.print(alarmMinute < 10 ? "0" : String(alarmMinute / 10));
        lcd.print(alarmMinute % 10);
      }
    }
  } else {
    if (snoozeUntil > 0 && !alarmRinging) {
      unsigned long remaining = (snoozeUntil - millis()) / 1000;
      lcd.print("Snooze: ");
      lcd.print(remaining / 60);
      lcd.print(":");
      if (remaining % 60 < 10) lcd.print('0');
      lcd.print(remaining % 60);
    } else if (alarmEnabled) {
      lcd.print("Alarm: ");
      if (alarmHour < 10) lcd.print('0');
      lcd.print(alarmHour); lcd.print(':');
      if (alarmMinute < 10) lcd.print('0');
      lcd.print(alarmMinute);
    } else {
      lcd.print("Alarm: OFF");
    }
  }
}

void handleKeypad() {
  keypad.tick();
  while (keypad.available()) {
    keypadEvent e = keypad.read();
    if (e.bit.EVENT == KEY_JUST_PRESSED) {
      char key = (char)e.bit.KEY;
      Serial.print("Key pressed: ");
      Serial.println(key);

      unsigned long now = millis();
      if (now - lastKeyTime < keyDebounce) continue;
      lastKeyTime = now;

      // If alarm is ringing
      if (alarmRinging) {
        digitalWrite(BUZZER_PIN, LOW);
        alarmRinging = false;

        if (key == 'D') {
          // Snooze using stored snoozeMinutes
          snoozeUntil = millis() + (snoozeMinutes * 60000UL);
          tempMessage = "SNOOZE " + String(snoozeMinutes) + "min";
          tempMessageEnd = millis() + 2000;
          Serial.print("Snooze set for ");
          Serial.print(snoozeMinutes);
          Serial.println(" minutes.");
        } else {
          // Stop permanently
          snoozeUntil = 0;
          tempMessage = "ALARM OFF";
          tempMessageEnd = millis() + 2000;
          Serial.println("Alarm stopped.");
        }
        return;
      }

      // If in snooze setting mode
      if (snoozeSetting) {
        if (key == 'A') {
          // Save and exit
          snoozeSetting = false;
          EEPROM.write(EEPROM_SNOOZE_ADDR, snoozeMinutes);
          tempMessage = "Snooze set to " + String(snoozeMinutes) + "min";
          tempMessageEnd = millis() + 2000;
          Serial.print("Snooze duration saved: ");
          Serial.println(snoozeMinutes);
        }
        else if (key == 'B') {
          snoozeMinutes = min(snoozeMinutes + 1, snoozeMax);
        }
        else if (key == 'C') {
          snoozeMinutes = max(snoozeMinutes - 1, snoozeMin);
        }
        return;
      }

      // Normal mode (not ringing, not in snooze setting)
      if (key == 'A') {
        if (!settingMode) {
          settingMode = true;
          settingStep = 0;
        } else {
          // Cycle through hour/minute/exit
          if (settingStep == 0) {
            settingStep = 1;
          } else {
            settingMode = false;
            alarmEnabled = true;
            snoozeUntil = 0;
            tempMessage = "ALARM SET";
            tempMessageEnd = millis() + 2000;
          }
        }
      }
      else if (key == 'B') {
        if (settingMode) {
          if (settingStep == 0) alarmHour = (alarmHour + 1) % 24;
          else alarmMinute = (alarmMinute + 1) % 60;
        }
      }
      else if (key == 'C') {
        if (settingMode) {
          if (settingStep == 0) alarmHour = (alarmHour + 23) % 24;
          else alarmMinute = (alarmMinute + 59) % 60;
        }
      }
      else if (key == '*') {
        // Toggle alarm on/off
        if (!settingMode && !snoozeSetting) {
          alarmEnabled = !alarmEnabled;
          tempMessage = alarmEnabled ? "ALARM ON" : "ALARM OFF";
          tempMessageEnd = millis() + 2000;
          Serial.print("Alarm ");
          Serial.println(alarmEnabled ? "enabled" : "disabled");
        }
      }
      else if (key == '#') {
        // Enter snooze setting mode (if not in alarm setting)
        if (!settingMode && !alarmRinging) {
          snoozeSetting = true;
        }
      }
    }
  }
}

void handleSerialTime() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'T' || c == 't') {
      Serial.println("Enter time as: YYYY-MM-DD HH:MM:SS");
      while (!Serial.available()) delay(10);
      String input = Serial.readStringUntil('\n');
      int year, month, day, hour, minute, second;
      if (sscanf(input.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
        RtcDateTime newTime(year, month, day, hour, minute, second);
        Rtc.SetDateTime(newTime);
        Serial.print("Time set to: ");
        printDateTime(newTime);
      } else {
        Serial.println("Invalid format. Use: 2026-03-26 15:30:00");
      }
    }
  }
}

void loop() {
  int h, m, s;
  getCurrentTime(h, m, s);

  handleKeypad();
  handleSerialTime();

  // Check snooze expiry
  if (snoozeUntil > 0 && millis() >= snoozeUntil) {
    digitalWrite(BUZZER_PIN, HIGH);
    alarmRinging = true;
    snoozeUntil = 0;
    Serial.println("Snooze time up, alarm ringing again.");
  }

  // Normal alarm trigger
  if (!settingMode && !snoozeSetting && alarmEnabled && !alarmRinging && snoozeUntil == 0) {
    if (h == alarmHour && m == alarmMinute) {
      digitalWrite(BUZZER_PIN, HIGH);
      alarmRinging = true;
      Serial.println("Alarm triggered! Buzzer ON.");
    }
  }

  updateDisplay(h, m, s);
  delay(50);
}
