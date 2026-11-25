/*
  1freq-FM7 — универсальный синтезатор на ATmega328P + Si5351.

  Режим MD=5:
    PC0 всегда измеряется АЦП.
    Низкое напряжение выбирает F1, высокое — F2.
    Между порогами действует гистерезис.
    ST хранится, но не применяется.

  Режим MD=6:
    PC0 измеряется тем же АЦП.
    ADC=0 соответствует F1, ADC=1023 соответствует F2.
    ST задаёт количество шагов; ST=0 означает 1024 шага.

  Внутренняя подтяжка PC0 не включается ни в одном режиме.
  Внешняя схема должна самостоятельно задавать напряжение на входе.

  EEPROM: пять записей по 16 байт:
    0x00: XT=27007840  — опорная частота кварца Si5351, Гц
    0x10: MD=5         — режим: 5 или 6
    0x20: F1=2000000   — частота при минимальном напряжении
    0x30: F2=1000000   — частота при максимальном напряжении
    0x40: ST=0         — число шагов (0 = 1024)

  Старые форматы FM5/FM6 не мигрируются.
  Если новая конфигурация отсутствует или повреждена, записываются
  значения по умолчанию.

  Библиотеки:
    si5351a.cpp/.h — из Fmulder007/GSSx11
    i2c.cpp/.h     — прежняя рабочая версия из Fmulder007/1freq-FM
*/

#include <EEPROM.h>
#include "i2c.h"
#include "si5351a.h"


// ============================================================
// НАСТРОЙКИ ПО УМОЛЧАНИЮ И ОГРАНИЧЕНИЯ
// ============================================================

#define CONTROL_PIN             14  // PC0 / ADC0, физический вывод 23

#define DEFAULT_XTAL_FREQ       27007840UL
#define DEFAULT_MODE            5
#define DEFAULT_F1               2000000UL
#define DEFAULT_F2               1000000UL
#define DEFAULT_STEPS            0UL

// Безопасные границы для арифметики выбранной версии библиотеки GSSx11.
// XT допускает кварцы, применяемые в проекте, включая 27.007840 и 32 MHz.
#define MIN_XTAL_FREQ            5000000UL
#define MAX_XTAL_FREQ          100000000UL

// При VCO=400...600 MHz и используемом R-divider:
#define MIN_OUTPUT_FREQ             15000UL
#define MAX_OUTPUT_FREQ        150000000UL

// Общий измерительный тракт в обоих режимах.
#define ADC_SAMPLES                    4
#define ADC_HYSTERESIS                 2

// MD=5: пороги с гистерезисом вокруг середины шкалы.
#define ADC_SWITCH_LOW               480
#define ADC_SWITCH_HIGH              544
#define ADC_SWITCH_MIDPOINT          512

#define MODE_SWITCH                    5
#define MODE_TUNING                    6

// EEPROM: пять записей по 16 байт.
#define EEPROM_XT_ADDR                 0
#define EEPROM_MD_ADDR                16
#define EEPROM_F1_ADDR                32
#define EEPROM_F2_ADDR                48
#define EEPROM_ST_ADDR                64


// ============================================================
// ГЛОБАЛЬНАЯ КОНФИГУРАЦИЯ И СОСТОЯНИЕ
// ============================================================

uint32_t xtalFrequency = DEFAULT_XTAL_FREQ;
uint8_t workMode = DEFAULT_MODE;
uint32_t frequency1 = DEFAULT_F1;
uint32_t frequency2 = DEFAULT_F2;
uint32_t stepsStored = DEFAULT_STEPS;

uint16_t numberOfSteps = 1024;

uint16_t lastAcceptedADC = 0;
uint16_t currentStep = 0xFFFF;
bool switchStateF2 = false;

Si5351 Si;


// ============================================================
// ПРОТОТИПЫ
// ============================================================

uint16_t readADC();
void processSwitchMode(uint16_t adc);
void processTuningMode(uint16_t adc);
void applySwitchState(bool useF2);
uint16_t adcToStep(uint16_t adc);
void applyTuningStep(uint16_t step);
void setOutputFrequency(uint32_t frequency);

bool checkLabel(uint8_t address, char c1, char c2);
bool readNumber(uint8_t address, uint32_t &value);
bool readParameter(uint8_t address, char c1, char c2, uint32_t &value);
void writeParameter(uint8_t address, char c1, char c2, uint32_t value);
void saveConfigToEEPROM();
bool configIsValid(uint32_t xt, uint32_t mode,
                   uint32_t f1, uint32_t f2, uint32_t st);
bool readConfigFromEEPROM();
void loadConfigFromEEPROM();


// ============================================================
// SETUP
// ============================================================

void setup() {

  // Один и тот же аналоговый вход для обоих режимов.
  // Внутренняя подтяжка не включается.
  pinMode(CONTROL_PIN, INPUT);

  loadConfigFromEEPROM();

  // Сначала настраиваем Si5351, затем задаём частоту его кварца.
  Si.setup(0, 0, 0);
  Si.cload(SI5351_CRYSTAL_LOAD_10PF);
  Si.set_xtal_freq(xtalFrequency);

  // Первое измерение определяет исходное состояние/частоту.
  const uint16_t initialADC = readADC();

  if (workMode == MODE_SWITCH) {

    // При старте выбираем состояние относительно середины шкалы.
    switchStateF2 = (initialADC >= ADC_SWITCH_MIDPOINT);
    applySwitchState(switchStateF2);

  } else {

    lastAcceptedADC = initialADC;
    currentStep = adcToStep(initialADC);
    applyTuningStep(currentStep);
  }
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // В обоих режимах напряжение измеряется совершенно одинаково.
  const uint16_t adc = readADC();

  if (workMode == MODE_SWITCH) {
    processSwitchMode(adc);
  } else {
    processTuningMode(adc);
  }
}


// ============================================================
// ОБЩЕЕ ИЗМЕРЕНИЕ PC0 / ADC0
// ============================================================

uint16_t readADC() {

  uint32_t sum = 0;

  for (uint8_t i = 0; i < ADC_SAMPLES; ++i) {
    sum += analogRead(CONTROL_PIN);
  }

  return (uint16_t)(sum / ADC_SAMPLES);
}


// ============================================================
// MD=5 — ДВА СОСТОЯНИЯ
// ============================================================

void processSwitchMode(uint16_t adc) {

  bool newState = switchStateF2;

  // Переход F1 -> F2 только после достижения верхнего порога.
  if (!switchStateF2 && adc >= ADC_SWITCH_HIGH) {
    newState = true;
  }

  // Переход F2 -> F1 только после достижения нижнего порога.
  else if (switchStateF2 && adc <= ADC_SWITCH_LOW) {
    newState = false;
  }

  // В промежуточной зоне предыдущее состояние сохраняется.
  if (newState != switchStateF2) {
    switchStateF2 = newState;
    applySwitchState(switchStateF2);
  }
}


void applySwitchState(bool useF2) {

  if (useF2) {
    setOutputFrequency(frequency2);
  } else {
    setOutputFrequency(frequency1);
  }
}


// ============================================================
// MD=6 — ПЛАВНАЯ ПЕРЕСТРОЙКА
// ============================================================

void processTuningMode(uint16_t adc) {

  int16_t difference = (int16_t)adc - (int16_t)lastAcceptedADC;
  if (difference < 0) difference = -difference;

  // Сохраняем логику FM6: малые колебания АЦП игнорируются.
  if (difference < ADC_HYSTERESIS) {
    return;
  }

  lastAcceptedADC = adc;

  const uint16_t step = adcToStep(adc);

  // Частота меняется только при переходе на другой шаг.
  if (step != currentStep) {
    currentStep = step;
    applyTuningStep(step);
  }
}


uint16_t adcToStep(uint16_t adc) {

  // ADC=0 -> шаг 0; ADC=1023 -> numberOfSteps.
  return (uint16_t)((uint32_t)adc * numberOfSteps / 1023UL);
}


void applyTuningStep(uint16_t step) {

  uint32_t frequency;

  if (frequency2 >= frequency1) {

    const uint32_t span = frequency2 - frequency1;
    frequency = frequency1 +
      (uint32_t)((uint64_t)span * step / numberOfSteps);

  } else {

    const uint32_t span = frequency1 - frequency2;
    frequency = frequency1 -
      (uint32_t)((uint64_t)span * step / numberOfSteps);
  }

  setOutputFrequency(frequency);
}


// ============================================================
// УСТАНОВКА ЧАСТОТЫ SI5351
// ============================================================

void setOutputFrequency(uint32_t frequency) {

  // set_freq() библиотеки GSSx11 сам рассчитывает регистры
  // MultiSynth/PLL и выполняет требуемый сброс PLL.
  Si.set_freq(frequency);
}


// ============================================================
// EEPROM — ПРОВЕРКА МЕТОК И ЧТЕНИЕ ЧИСЕЛ
// ============================================================

bool checkLabel(uint8_t address, char c1, char c2) {

  return EEPROM.read(address)     == c1 &&
         EEPROM.read(address + 1) == c2 &&
         EEPROM.read(address + 2) == '=';
}


bool readNumber(uint8_t address, uint32_t &value) {

  uint32_t result = 0;
  bool hasDigit = false;
  bool terminated = false;

  for (uint8_t i = 3; i < 16; ++i) {

    const uint8_t character = EEPROM.read(address + i);

    if (character == 0) {
      terminated = true;
      break;
    }

    if (character < '0' || character > '9') {
      return false;
    }

    const uint8_t digit = character - '0';

    // Защита от переполнения uint32_t.
    if (result > (0xFFFFFFFFUL - digit) / 10UL) {
      return false;
    }

    result = result * 10UL + digit;
    hasDigit = true;
  }

  if (!terminated || !hasDigit) {
    return false;
  }

  value = result;
  return true;
}


bool readParameter(uint8_t address, char c1, char c2, uint32_t &value) {

  if (!checkLabel(address, c1, c2)) {
    return false;
  }

  return readNumber(address, value);
}


// ============================================================
// EEPROM — ЗАПИСЬ ПАРАМЕТРОВ
// ============================================================

void writeParameter(uint8_t address, char c1, char c2, uint32_t value) {

  EEPROM.update(address,     c1);
  EEPROM.update(address + 1, c2);
  EEPROM.update(address + 2, '=');

  char digits[10];
  uint8_t count = 0;

  do {
    digits[count++] = '0' + (value % 10UL);
    value /= 10UL;
  } while (value);

  uint8_t position = address + 3;

  while (count) {
    EEPROM.update(position++, digits[--count]);
  }

  EEPROM.update(position++, 0);

  // Очищаем остаток 16-байтной области.
  while (position < address + 16) {
    EEPROM.update(position++, 0);
  }
}


void saveConfigToEEPROM() {

  writeParameter(EEPROM_XT_ADDR, 'X', 'T', xtalFrequency);
  writeParameter(EEPROM_MD_ADDR, 'M', 'D', workMode);
  writeParameter(EEPROM_F1_ADDR, 'F', '1', frequency1);
  writeParameter(EEPROM_F2_ADDR, 'F', '2', frequency2);
  writeParameter(EEPROM_ST_ADDR, 'S', 'T', stepsStored);
}


// ============================================================
// EEPROM — ПРОВЕРКА КОНФИГУРАЦИИ
// ============================================================

bool configIsValid(uint32_t xt, uint32_t mode,
                   uint32_t f1, uint32_t f2, uint32_t st) {

  if (xt < MIN_XTAL_FREQ || xt > MAX_XTAL_FREQ) {
    return false;
  }

  if (mode != MODE_SWITCH && mode != MODE_TUNING) {
    return false;
  }

  if (f1 < MIN_OUTPUT_FREQ || f1 > MAX_OUTPUT_FREQ ||
      f2 < MIN_OUTPUT_FREQ || f2 > MAX_OUTPUT_FREQ) {
    return false;
  }

  if (st > 1024UL) {
    return false;
  }

  return true;
}


bool readConfigFromEEPROM() {

  uint32_t xt;
  uint32_t mode;
  uint32_t f1;
  uint32_t f2;
  uint32_t st;

  if (!readParameter(EEPROM_XT_ADDR, 'X', 'T', xt) ||
      !readParameter(EEPROM_MD_ADDR, 'M', 'D', mode) ||
      !readParameter(EEPROM_F1_ADDR, 'F', '1', f1) ||
      !readParameter(EEPROM_F2_ADDR, 'F', '2', f2) ||
      !readParameter(EEPROM_ST_ADDR, 'S', 'T', st)) {
    return false;
  }

  if (!configIsValid(xt, mode, f1, f2, st)) {
    return false;
  }

  xtalFrequency = xt;
  workMode = (uint8_t)mode;
  frequency1 = f1;
  frequency2 = f2;
  stepsStored = st;

  return true;
}


// ============================================================
// ЗАГРУЗКА КОНФИГУРАЦИИ
// ============================================================

void loadConfigFromEEPROM() {

  // Поддерживается только новая структура FM7.
  // Старые EEPROM FM5/FM6 намеренно не мигрируются.
  if (!readConfigFromEEPROM()) {

    xtalFrequency = DEFAULT_XTAL_FREQ;
    workMode = DEFAULT_MODE;
    frequency1 = DEFAULT_F1;
    frequency2 = DEFAULT_F2;
    stepsStored = DEFAULT_STEPS;

    saveConfigToEEPROM();
  }

  // ST=0 — режим максимальной плавности (1024 шага).
  numberOfSteps = (stepsStored == 0)
                ? 1024
                : (uint16_t)stepsStored;
}
