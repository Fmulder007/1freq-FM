/*
  1freq-POT
  Генератор на Si5351 с перестройкой частоты потенциометром.

  Базовая частота, ширина диапазона и количество шагов
  хранятся в EEPROM МК в читаемом текстовом виде.

  Назначение пинов:
    controlpin (PC0/ADC0, pin 23) — аналоговый вход потенциометра
      0 В     → FR - BW/2 (нижняя граница диапазона)
      VCC     → FR + BW/2 (верхняя граница диапазона)
      середина → FR       (базовая частота, центр диапазона)

  EEPROM: 3 строки по 16 байт
    0x00: "FR=1950000\0"   — базовая (центральная) частота, Гц
    0x10: "BW=100000\0"    — полная ширина диапазона, Гц (±BW/2 от FR)
    0x20: "ST=200\0"       — количество шагов (0 = 1024, прямой режим)

  Перестройка частоты:
    ADC потенциометра (0…1023) отображается на NumSteps шагов.
    ST=0             → 1024 шага (прямой режим, максимум плавности)
    Размер шага:     BW / ST (ровно)
    Шаг 0:           FR - BW/2
    Шаг ST:          FR + BW/2
    Центр (ADC=512): FR

  Антидребезг:
    — Усреднение ADC_SAMPLES последовательных замеров ADC.
    — Зона нечувствительности ADC_HYSTER: изменение ADC
      менее чем на ADC_HYSTER отсчётов игнорируется.
    — Частота обновляется только при смене шага.

  FUSE
    Low:     62
    High:    D1
    Extended: FD
*/


// === Настройки ===

#define Si_Xtall_Freq 27007840UL  // Частота кварца Si5351, Гц

#define controlpin    14          // M328 pin #23 (PC0/ADC0)

#define Si_cload      SI5351_CRYSTAL_LOAD_10PF

#define DEFAULT_FR    14000000UL   // Базовая частота по умолчанию, Гц
#define DEFAULT_BW    2000000UL    // Ширина диапазона по умолчанию, Гц
#define DEFAULT_ST    0            // 0 = прямой режим (1024 шага)

#define ADC_SAMPLES   4            // Замеров ADC для усреднения
#define ADC_HYSTER    2            // Зона нечувствительности ADC, отсчёты


#include "si5351a.h"
#include <EEPROM.h>


Si5351 Si;


// Параметры из EEPROM
uint32_t FreqBase;    // Базовая (центральная) частота, Гц
uint32_t FreqWidth;   // Полная ширина диапазона, Гц
uint16_t NumSteps;     // Количество шагов (0 → 1024)


// Текущее состояние
uint16_t currentStep = 0xFFFF;  // Текущий шаг (0xFFFF = не установлен)
uint16_t lastADC = 0;           // Последний усреднённый ADC


// ============================================================
// SETUP
// ============================================================

void setup() {

  // PC0 как аналоговый вход без подтяжки.
  // Потенциометр: крайний вывод к GND,
  //               крайний вывод к VCC,
  //               средний вывод — на controlpin.

  pinMode(controlpin, INPUT);


  // Читаем параметры из EEPROM
  loadParamsFromEEPROM();


  // Инициализация Si5351
  Si.setup(0, 0, 0);
  Si.cload(Si_cload);
  Si.set_xtal_freq(Si_Xtall_Freq);


  // Первичное чтение ADC и установка частоты
  lastADC = readADC();
  currentStep = adcToStep(lastADC);
  applyStep(currentStep);
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  uint16_t adc = readADC();


  // Зона нечувствительности:
  // если ADC изменился меньше чем на ADC_HYSTER отсчётов — игнорируем.

  int16_t diff = (int16_t)adc - (int16_t)lastADC;
  if (diff < 0) diff = -diff;
  if (diff < ADC_HYSTER) return;

  lastADC = adc;


  // Вычисляем шаг
  uint16_t step = adcToStep(adc);


  // Обновляем частоту только при смене шага
  if (step != currentStep) {
    currentStep = step;
    applyStep(step);
  }
}


// ============================================================
// Усреднённое чтение ADC через analogRead()
//
// На 1 МГц с делителем 128: один analogRead ~1.7 мс, 4 замера ~7 мс.
// Это даёт ~140–150 обновлений в секунду — достаточно для «эффекта КПЕ».
// ============================================================

uint16_t readADC() {

  uint32_t sum = 0;

  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(controlpin);
  }

  return (uint16_t)(sum / ADC_SAMPLES);
}


// ============================================================
// Преобразование значения ADC в номер шага
//
// ADC 0    → шаг 0
// ADC 1023 → шаг NumSteps
//
// NumSteps = 0 → 1024 (прямой режим)
// ============================================================

uint16_t adcToStep(uint16_t adc) {

  if (NumSteps <= 1) return 0;

  return (uint16_t)((uint32_t)adc * NumSteps / 1023);
}


// ============================================================
// Установка частоты по номеру шага
//
// Размер шага:  FreqWidth / NumSteps  (ровно)
// Шаг 0:        FR - BW/2  (нижняя граница)
// Шаг NumSteps: FR + BW/2  (верхняя граница)
// Центр:        FR         (базовая частота)
// ============================================================

void applyStep(uint16_t step) {

  uint32_t freq;

  if (NumSteps <= 1 || FreqWidth == 0) {
    freq = FreqBase;
  } else {
    // Нижняя граница диапазона: FR - BW/2
    uint32_t halfWidth = FreqWidth / 2;
    uint32_t baseLow = (FreqBase >= halfWidth)
                      ? (FreqBase - halfWidth)
                      : 0;

    // Текущая частота: baseLow + step * (BW / NumSteps)
    freq = baseLow + (uint32_t)((uint64_t)step * FreqWidth / NumSteps);
  }

  Si.set_freq(freq);
  Si.update_freq(0);
}


// ============================================================
// Проверка метки EEPROM
// ============================================================

bool checkLabel(uint8_t addr, char c1, char c2) {

  return EEPROM.read(addr)     == c1  &&
         EEPROM.read(addr + 1) == c2  &&
         EEPROM.read(addr + 2) == '=';
}


// ============================================================
// Чтение всех параметров из EEPROM при старте
// ============================================================

void loadParamsFromEEPROM() {

  // Проверяем все три метки: FR=, BW=, ST=
  if (!checkLabel(0,  'F', 'R') ||
      !checkLabel(16, 'B', 'W') ||
      !checkLabel(32, 'S', 'T')) {

    saveParamsToEEPROM(DEFAULT_FR, DEFAULT_BW, DEFAULT_ST);
  }


  // Читаем параметры
  FreqBase  = readNumber(0);
  FreqWidth = readNumber(16);
  NumSteps  = (uint16_t)readNumber(32);


  // Обработка NumSteps:
  // 0     → 1024 (прямой режим, максимум плавности)
  // >1024 → 1024 (больше 10-битного ADC всё равно не различить)

  if (NumSteps == 0 || NumSteps > 1024) {
    NumSteps = 1024;
  }


  // Проверка на повреждённые данные.
  // FreqWidth = 0 допустим — это просто фиксированная частота.
  // FreqBase = 0 — недопустим.

  if (FreqBase == 0) {

    saveParamsToEEPROM(DEFAULT_FR, DEFAULT_BW, DEFAULT_ST);

    FreqBase  = DEFAULT_FR;
    FreqWidth = DEFAULT_BW;
    NumSteps  = 1024;
  }
}


// ============================================================
// Запись всех параметров в EEPROM
// ============================================================

void saveParamsToEEPROM(uint32_t fr, uint32_t bw, uint32_t st) {

  writeParam(0,  'F', 'R', fr);
  writeParam(16, 'B', 'W', bw);
  writeParam(32, 'S', 'T', st);
}


// ============================================================
// Запись одного параметра в EEPROM
// ============================================================

void writeParam(uint8_t addr, char c1, char c2, uint32_t value) {

  EEPROM.update(addr,     c1);
  EEPROM.update(addr + 1, c2);
  EEPROM.update(addr + 2, '=');


  char digits[10];
  uint8_t n = 0;

  do {
    digits[n++] = '0' + (value % 10);
    value /= 10;
  } while (value);


  uint8_t pos = addr + 3;

  while (n) {
    EEPROM.update(pos++, digits[--n]);
  }

  EEPROM.update(pos++, 0);

  while (pos < addr + 16) {
    EEPROM.update(pos++, 0);
  }
}


// ============================================================
// Чтение числа из EEPROM
// ============================================================

uint32_t readNumber(uint8_t addr) {

  uint32_t value = 0;

  for (uint8_t i = 3; i < 15; i++) {

    uint8_t c = EEPROM.read(addr + i);

    if (c == 0) break;
    if (c < '0' || c > '9') return 0;

    value = value * 10UL + (c - '0');
  }

  return value;
}
