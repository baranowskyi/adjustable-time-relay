// last version

#include <Wire.h>
#include <RTClib.h>
#include <TM1637Display.h>

// Пины подключения
#define CLK_PIN 2     // CLK пин дисплея TM1637
#define DIO_PIN 3     // DIO пин дисплея TM1637
#define RELAY_PIN 4   // Пин управления реле

// Настройка типа реле (измените на true если у вас реле с низким уровнем)
#define LOW_LEVEL_RELAY true  // false = высокий уровень для включения, true = низкий уровень

// Таймауты для инициализации (в миллисекундах)
#define I2C_TIMEOUT 1000
#define RTC_INIT_TIMEOUT 2000

// Настройки временных диапазонов
struct TimeRange {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t startSecond;
  uint8_t endHour;
  uint8_t endMinute;
  uint8_t endSecond;
};

// Определяем два временных диапазона
TimeRange range1 = {4, 0, 0, 8, 0, 0};    // 4:00:00 - 8:00:00
TimeRange range2 = {20, 0, 0, 22, 0, 0};   // 20:00:00 - 22:00:00

// Создание объектов
RTC_DS3231 rtc;
TM1637Display display(CLK_PIN, DIO_PIN);

// Переменные для контроля времени
unsigned long lastTimeDisplayTime = 0;      // Время последнего показа времени
unsigned long timeDisplayStartTime = 0;     // Время начала показа времени
unsigned long errorDisplayStartTime = 0;    // Время начала показа ошибки
bool timeDisplayActive = false;             // Флаг показа времени каждые 2 минуты
bool errorDisplayActive = false;            // Флаг показа ошибки RTC
bool wasInActiveRange = false;              // Флаг для отслеживания предыдущего состояния
bool rtcWorking = false;                    // Флаг работоспособности RTC

const unsigned long TIME_DISPLAY_INTERVAL = 120000; // 2 минуты = 120 секунд
const unsigned long TIME_DISPLAY_DURATION = 20000;  // 20 секунд показа времени
const unsigned long ERROR_DISPLAY_DURATION = 5000;  // 5 секунд показа ошибки

// Функции для управления реле с учетом типа
void relayOn() {
  digitalWrite(RELAY_PIN, LOW_LEVEL_RELAY ? LOW : HIGH);
}

void relayOff() {
  digitalWrite(RELAY_PIN, LOW_LEVEL_RELAY ? HIGH : LOW);
}

bool isRelayOn() {
  return digitalRead(RELAY_PIN) == (LOW_LEVEL_RELAY ? LOW : HIGH);
}

// Объявления функций
void handleTimeDisplay(DateTime time, unsigned long currentMillis);
void handleErrorDisplay(unsigned long currentMillis);
bool isTimeInRange(DateTime time, TimeRange range);
void printTimeRange(TimeRange range);
void printCurrentTime(DateTime time);
bool initializeRTCWithTimeout();
bool checkI2CDeviceWithTimeout(uint8_t address, unsigned long timeout);

void setup() {
  Serial.begin(9600);
  
  // Инициализация дисплея в самом начале
  display.setBrightness(0); // яркость 0-7
  display.clear();
  
  // Настройка пина реле
  pinMode(RELAY_PIN, OUTPUT);
  relayOff(); // Реле выключено
  
  Serial.println("Система запущена");
  Serial.println("Инициализация DS3231...");
  
  // Показываем на дисплее, что идет инициализация
  display.showNumberDecEx(8888, 0b11111111, true); // Показываем 88:88 во время инициализации
  
  // Попытка инициализации RTC с таймаутом
  rtcWorking = initializeRTCWithTimeout();
  
  if (rtcWorking) {
    Serial.println("RTC инициализирован успешно!");
    display.clear(); // Очищаем дисплей после успешной инициализации
  } else {
    Serial.println("RTC не работает - будет показана индикация ошибки");
    // Дисплей будет управляться в loop() для показа ошибки
  }
  
  // Устанавливаем начальное время для регулярного показа времени
  lastTimeDisplayTime = millis();
  errorDisplayStartTime = millis(); // Инициализируем время для ошибки
  
  Serial.println("Система инициализирована");  
  if (rtcWorking) {
    Serial.println("Временные диапазоны:");    
    Serial.print("Диапазон 1: ");    
    printTimeRange(range1);    
    Serial.print("Диапазон 2: ");    
    printTimeRange(range2);    
    Serial.println("Время: каждые 2 минуты на 20 секунд");    
    Serial.println("Реле: включено в активных диапазонах");    
  } else {
    Serial.println("RTC не работает");
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Если RTC не работает, показываем индикацию ошибки
  if (!rtcWorking) {
    handleErrorDisplay(currentMillis);
    // Убеждаемся, что реле выключено при проблемах с RTC
    if (isRelayOn()) {
      relayOff();
      Serial.println("Реле выключено из-за ошибки RTC");
    }
    delay(100);
    return;
  }
  
  DateTime now = rtc.now();
  // Serial.print("Текущее время RTC: ");
  // Serial.print(now.hour());
  // Serial.print(":");
  // Serial.print(now.minute());
  // Serial.print(":");
  // Serial.println(now.second());
  
  // Управление регулярным показом времени каждые 2 минуты
  handleTimeDisplay(now, currentMillis);
  
  // Проверяем, находимся ли мы в одном из активных временных диапазонов
  bool isInActiveRange = isTimeInRange(now, range1) || isTimeInRange(now, range2); 
  
  // Если мы вошли в активный диапазон (переход из неактивного в активный)
  if (isInActiveRange && !wasInActiveRange) {   
    relayOn();  // Включаем реле
    Serial.print("Вход в активный диапазон в ");
    printCurrentTime(now);
    Serial.println("Реле включено");
  }
  
  // Если мы вышли из активного диапазона (переход из активного в неактивный)
  if (!isInActiveRange && wasInActiveRange) {    
    relayOff();   // Выключаем реле
    Serial.print("Выход из активного диапазона в ");
    printCurrentTime(now);
    Serial.println("Реле выключено");
  }
  
  // Обновляем флаг предыдущего состояния
  wasInActiveRange = isInActiveRange;
  
  // Небольшая задержка для стабильности
  delay(100);
}

// Функция инициализации RTC с таймаутом
bool initializeRTCWithTimeout() {
  // Инициализация I2C
  Wire.begin();
  
  // Проверяем наличие устройства по адресу 0x68 с таймаутом
  Serial.println("Проверка наличия DS3231 по адресу 0x68...");
  if (!checkI2CDeviceWithTimeout(0x68, I2C_TIMEOUT)) {
    Serial.println("DS3231 не найден по адресу 0x68");
    return false;
  }
  
  Serial.println("DS3231 найден, попытка инициализации...");
  
  // Попытка инициализации с ограничением времени
  unsigned long startTime = millis();
  bool initSuccess = false;
  
  // Первая попытка
  if (rtc.begin()) {
    initSuccess = true;
  } else {
    Serial.println("Первая попытка неудачна, сброс I2C...");
    
    // Сброс и повторная попытка
    Wire.end();
    delay(500);
    Wire.begin();
    delay(500);
    
    if (millis() - startTime < RTC_INIT_TIMEOUT) {
      if (rtc.begin()) {
        initSuccess = true;
        Serial.println("Инициализация успешна после сброса I2C");
      }
    }
  }
  
  if (!initSuccess || (millis() - startTime >= RTC_INIT_TIMEOUT)) {
    Serial.println("Таймаут инициализации RTC");
    return false;
  }
  
  return true;
}

// Функция проверки I2C устройства с таймаутом
bool checkI2CDeviceWithTimeout(uint8_t address, unsigned long timeout) {
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      return true; // Устройство найдено
    }
    
    delay(100); // Небольшая задержка между попытками
  }
  
  return false; // Таймаут
}

// Функция для обработки регулярного показа времени каждые 2 минуты
void handleTimeDisplay(DateTime time, unsigned long currentMillis) {
  // Проверяем, нужно ли включить показ времени
  if (!timeDisplayActive && (currentMillis - lastTimeDisplayTime >= TIME_DISPLAY_INTERVAL)) {
    timeDisplayActive = true;
    timeDisplayStartTime = currentMillis;
    lastTimeDisplayTime = currentMillis;
    Serial.print("Регулярный показ времени: ");
    printCurrentTime(time);
  }
  
  // Управление показом времени
  if (timeDisplayActive) {
    if (currentMillis - timeDisplayStartTime < TIME_DISPLAY_DURATION) {
      // Отображаем время в формате ЧЧ:ММ
      int timeToShow = time.hour() * 100 + time.minute();
      display.showNumberDecEx(timeToShow, 0b01000000, true); // Двоеточие включено
    } else {
      // Время показа истекло
      timeDisplayActive = false;
      display.clear();
      Serial.println("Регулярный показ времени завершен");
    }
  }
}

// Функция для обработки индикации ошибки RTC
void handleErrorDisplay(unsigned long currentMillis) {
  // Проверяем, нужно ли переключить состояние показа ошибки
  if (currentMillis - errorDisplayStartTime >= ERROR_DISPLAY_DURATION) {
    errorDisplayActive = !errorDisplayActive;
    errorDisplayStartTime = currentMillis;
    
    if (errorDisplayActive) {
      Serial.println("Показ индикации ошибки RTC: 00:00");
      display.showNumberDecEx(0, 0b01000000, true); // 00:00 с двоеточием
    } else {
      Serial.println("Очистка дисплея (пауза в индикации ошибки)");
      display.clear();
    }
  }
}

// Функция для проверки, находится ли время в заданном диапазоне
bool isTimeInRange(DateTime time, TimeRange range) {
  uint32_t currentSeconds = (uint32_t)time.hour() * 3600 + (uint32_t)time.minute() * 60 + (uint32_t)time.second();
  uint32_t startSeconds = (uint32_t)range.startHour * 3600 + (uint32_t)range.startMinute * 60 + (uint32_t)range.startSecond;
  uint32_t endSeconds = (uint32_t)range.endHour * 3600 + (uint32_t)range.endMinute * 60 + (uint32_t)range.endSecond;
  
  return (currentSeconds >= startSeconds && currentSeconds <= endSeconds); 
}

// Функция для вывода временного диапазона в Serial
void printTimeRange(TimeRange range) {
  Serial.print(range.startHour);
  Serial.print(":");
  if (range.startMinute < 10) Serial.print("0");
  Serial.print(range.startMinute);
  Serial.print(":");
  if (range.startSecond < 10) Serial.print("0");
  Serial.print(range.startSecond);
  Serial.print(" - ");
  Serial.print(range.endHour);
  Serial.print(":");
  if (range.endMinute < 10) Serial.print("0");
  Serial.print(range.endMinute);
  Serial.print(":");
  if (range.endSecond < 10) Serial.print("0");
  Serial.println(range.endSecond);
}

// Функция для вывода текущего времени в Serial
void printCurrentTime(DateTime time) {
  Serial.print(time.hour());
  Serial.print(":");
  if (time.minute() < 10) Serial.print("0");
  Serial.print(time.minute());
  Serial.print(":");
  if (time.second() < 10) Serial.print("0");
  Serial.println(time.second());
}