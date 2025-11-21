#include <Arduino.h>            // Основна бібліотека Arduino
#include <Wire.h>               // Бібліотека для роботи з I2C
#include <LiquidCrystal_I2C.h>  // Бібліотека для LCD дисплея через I2C
#include <avr/wdt.h>            // Бібліотека сторожового таймера
#include <EEPROM.h>             // Бібліотека енергонезалежної пам'яті

/*
 * ============================================================================
 * Курсова робота з дисципліни "Низькорівневе програмування"
 * 20. Низькорівнева реалізація ефекту «дихаючого» світлодіода 
 *  з використанням прямої маніпуляції портами, переривань та Watchdog-таймера
 * Виконав: Арцибашев В'ячеслав Сергійович
 * Група: 242/1 он
 * Науковий керівник: Яровий Р.О.
 * Презентація: https://www.canva.com/design/DAG5UZnxH24/1tYZ5U7XUdkO624KY1CqbA/edit
 * ============================================================================
 */

 // Ініціалізація LCD дисплея 16x2 за адресою 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ============================================================================
// ГЛОБАЛЬНІ ЗМІННІ ТА СТАНИ
// ============================================================================

// Поточний режим роботи системи. 
// volatile (не кешуємо змінну) потрібен для коректної роботи в перериваннях (ISR)
volatile int systemMode = 0;
const int MAX_MODES = 3;
const int EEPROM_ADDR_MODE = 0; // Адреса комірки пам'яті для зберігання режиму

// Змінна для відстеження часу останнього натискання кнопки (для усунення деренчання)
volatile unsigned long lastDebounceTime = 0;
// Затримка для усунення деренчання кнопки (в мс)
const unsigned long DEBOUNCE_DELAY = 200;

// Прапор для відкладеного запису в EEPROM (в ISR писати не можна - довго)
volatile bool saveToEepromPending = false;

// Таймер для Heartbeat LED
unsigned long lastHeartbeat = 0;
const int HEARTBEAT_INTERVAL = 1000;

// ============================================================================
// ОБРОБНИК ПЕРЕРИВАННЯ (ISR)
// ============================================================================
// Викликається апаратно при натисканні кнопки.
void handleButtonPress() {
  unsigned long currentTime = millis();
  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    systemMode++;
    if (systemMode >= MAX_MODES) {
      systemMode = 0;
    }
    saveToEepromPending = true; // Сигнал для loop(), що треба зберегти
    lastDebounceTime = currentTime;
  }
}

// ============================================================================
// КЛАС BREATHING LED
// ============================================================================
class BreathingLED {
  private:
    int pin;
    float period;
    
  public:
    BreathingLED(int p) {
      pin = p;
      period = 3000.0;
    }

    void begin() {
      pinMode(pin, OUTPUT);
    }

    void setPeriod(float newPeriod) {
      period = newPeriod;
    }

    int getPeriod() {
      return (int)period;
    }

    void update(unsigned long currentMillis, int mode) {
      float angle = (2.0 * PI * currentMillis) / period;
      int brightness = 0;

      if (mode == 1) { 
        // TRIANGLE WAVE (Трикутна хвиля)
        // Лінійна зміна яскравості (гострі піки)
        float normalized = (angle / (2.0 * PI)); 
        normalized = normalized - (int)normalized;
        
        if (normalized < 0.5) {
          brightness = normalized * 2.0 * 255.0;
        } else {
          brightness = (1.0 - normalized) * 2.0 * 255.0;
        }
      } else {
        // SINE WAVE (Синусоїда)
        // Плавна, природна зміна яскравості
        brightness = (int)((sin(angle) + 1.0) * 127.5);
      }
      
      brightness = constrain(brightness, 0, 255);
      analogWrite(pin, brightness);
    }
};

BreathingLED ledRed(9);
BreathingLED ledGreen(10);
BreathingLED ledBlue(11);

const int POT_RED_PIN = A0;
const int POT_GREEN_PIN = A1;
const int POT_BLUE_PIN = A2;
const int BUTTON_PIN = 2;

unsigned long lastLcdUpdate = 0;
const int LCD_INTERVAL = 300;
int displayedMode = -1;

void setup() {
  // WATCHDOG TIMER
  // Вмикаємо таймер на 2 секунди.
  // Якщо система зависне, вона перезавантажиться.
  wdt_enable(WDTO_2S);

  // Пряма робота з портами: 
  // налаштовуємо Pin 13 (PB5) на вихід через регістр DDRB
  DDRB |= (1 << 5);

  Serial.begin(9600);
  
  // EEPROM READ
  // Зчитуємо збережений режим при старті
  int savedMode = EEPROM.read(EEPROM_ADDR_MODE);
  // Перевіряємо на валідність
  if (savedMode >= 0 && savedMode < MAX_MODES) {
    systemMode = savedMode;
  } else {
    systemMode = 0; // режим за замовчуванням
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Прив'язуємо функцію handleButtonPress до переривання на піні 2.
  // FALLING означає спрацювання при натисканні (спад напруги).
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);

  lcd.init();
  lcd.backlight();
  
  ledRed.begin();
  ledGreen.begin();
  ledBlue.begin();
}

void loop() {
  // WATCHDOG RESET
  // Скидаємо таймер до нуля.
  // Якщо програма зависне і не дійде сюди за 2 сек, станеться ресет.
  wdt_reset();

  // EEPROM SAVE
  // Перевіряємо, чи треба зберегти налаштування
  if (saveToEepromPending) {
    saveToEepromPending = false;
    // використовуємо update замість write, щоб не вбивати EEPROM зайвими записами
    EEPROM.update(EEPROM_ADDR_MODE, systemMode);
  }

  unsigned long currentMillis = millis();

  // HEARTBEAT LED
  // Блимаємо світлодіодом раз на секунду.
  if (currentMillis - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = currentMillis;
    // Інвертуємо 13 пін через регістр (швидше за digitalWrite)
    PORTB ^= (1 << 5);
  }

  // Зчитування потенціометрів
  int valRed = analogRead(POT_RED_PIN);
  int valGreen = analogRead(POT_GREEN_PIN);
  int valBlue = analogRead(POT_BLUE_PIN);

  // Логіка режимів
  if (systemMode == 2) {
    float sharedPeriod = map(valRed, 0, 1023, 500, 5000);
    ledRed.setPeriod(sharedPeriod);
    ledGreen.setPeriod(sharedPeriod);
    ledBlue.setPeriod(sharedPeriod);
  } else {
    ledRed.setPeriod(map(valRed, 0, 1023, 500, 5000));
    ledGreen.setPeriod(map(valGreen, 0, 1023, 500, 5000));
    ledBlue.setPeriod(map(valBlue, 0, 1023, 500, 5000));
  }

  // Оновлення світлодіодів
  ledRed.update(currentMillis, systemMode);
  ledGreen.update(currentMillis, systemMode);
  ledBlue.update(currentMillis, systemMode);

  // Оновлення дисплею
  if (currentMillis - lastLcdUpdate >= LCD_INTERVAL || displayedMode != systemMode) {
    lastLcdUpdate = currentMillis;
    displayedMode = systemMode;
    
    lcd.setCursor(0, 0);
    lcd.print("Mode: ");
    if (systemMode == 0) lcd.print("Sine Indep ");
    else if (systemMode == 1) lcd.print("Triang Ind ");
    else if (systemMode == 2) lcd.print("Sine Sync  ");

    lcd.setCursor(0, 1);
    if (systemMode == 2) {
      lcd.print("Speed: ");
      lcd.print(ledRed.getPeriod());
      lcd.print(" ms   ");
    } else {
      lcd.print("R"); lcd.print(ledRed.getPeriod()/100);
      lcd.print(" G"); lcd.print(ledGreen.getPeriod()/100);
      lcd.print(" B"); lcd.print(ledBlue.getPeriod()/100);
      lcd.print("  ");
    }
  }
}
