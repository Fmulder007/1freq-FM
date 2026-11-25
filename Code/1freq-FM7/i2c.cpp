// Аппаратный I2C/TWI для ATmega168/328 и совместимых LGT8F328P.
// Используются стандартные регистры TWI, общие для поддерживаемых ядер.

#include <Arduino.h>
#include <avr/io.h>
#include "i2c.h"

#define I2C_START             0x08
#define I2C_START_RPT         0x10
#define I2C_SLA_W_ACK         0x18
#define I2C_DATA_ACK          0x28
#define I2C_SLA_R_ACK         0x40
#define I2C_DATA_RX_ACK       0x50
#define I2C_DATA_RX_NACK      0x58

// Ограничиваем ожидание примерно 25 мс.
// Число итераций масштабируется относительно F_CPU и не зависит от millis().
#define I2C_TIMEOUT_LOOPS \
  ((uint32_t)(((F_CPU / 8UL) * 25UL) / 1000UL + 1UL))

static bool i2c_error = false;


static bool waitForTWINT() {

  uint32_t remaining = I2C_TIMEOUT_LOOPS;

  while (!(TWCR & _BV(TWINT))) {
    if (remaining == 0) {
      i2c_error = true;
      return false;
    }
    --remaining;
  }

  return true;
}


static bool waitForSTOP() {

  uint32_t remaining = I2C_TIMEOUT_LOOPS;

  while (TWCR & _BV(TWSTO)) {
    if (remaining == 0) {
      return false;
    }
    --remaining;
  }

  return true;
}


static bool i2cStart() {

  if (i2c_error) {
    return false;
  }

  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);

  if (!waitForTWINT()) {
    return false;
  }

  const uint8_t status = TWSR & 0xF8;

  if (status != I2C_START && status != I2C_START_RPT) {
    i2c_error = true;
    return false;
  }

  return true;
}


void i2c_init(uint32_t i2c_freq) {

  pinMode(SCL, INPUT);
  pinMode(SDA, INPUT);

  if (i2c_freq == 0) {
    i2c_freq = 100000UL;
  }

  // Для Si5351 используем стандартный диапазон I2C не выше 400 кГц.
  if (i2c_freq > 400000UL) {
    i2c_freq = 400000UL;
  }

  // Делитель TWI: SCL = F_CPU / (16 + 2 * TWBR * prescaler).
  // Для режима Master выбираем TWBR не ниже 10.
  static const uint8_t prescalerBits[4] = {0, 1, 2, 3};
  static const uint8_t prescalerValue[4] = {1, 4, 16, 64};

  const uint32_t clocksPerSCL =
    (F_CPU + i2c_freq - 1UL) / i2c_freq;

  uint8_t selectedPrescaler = 3;
  uint8_t selectedTWBR = 255;
  bool dividerFound = false;

  for (uint8_t i = 0; i < 4; ++i) {

    const uint32_t prescaler = prescalerValue[i];
    const uint32_t denominator = 2UL * prescaler;
    const uint32_t numerator =
      (clocksPerSCL > 16UL) ? (clocksPerSCL - 16UL) : 0UL;

    uint32_t twbr = (numerator + denominator - 1UL) / denominator;

    if (twbr < 10UL) {
      twbr = 10UL;
    }

    if (twbr <= 255UL) {
      selectedPrescaler = prescalerBits[i];
      selectedTWBR = (uint8_t)twbr;
      dividerFound = true;
      break;
    }
  }

  // Если запрошена недостижимо низкая скорость, используем
  // максимально возможный делитель TWI.
  if (!dividerFound) {
    selectedPrescaler = 3;
    selectedTWBR = 255;
  }

  TWSR = selectedPrescaler;
  TWBR = selectedTWBR;
  TWDR = 0xFF;
  TWCR = _BV(TWEN);

  i2c_error = false;
}


void i2c_end() {

  // STOP отправляется и после ошибки/тайм-аута, чтобы освободить шину.
  TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);
  waitForSTOP();

  // Следующая транзакция начинается с чистого состояния.
  i2c_error = false;
}


bool i2c_write(uint8_t data) {

  if (i2c_error) {
    return false;
  }

  TWDR = data;
  TWCR = _BV(TWINT) | _BV(TWEN);

  if (!waitForTWINT()) {
    return false;
  }

  const uint8_t status = TWSR & 0xF8;

  if (status == I2C_SLA_W_ACK ||
      status == I2C_DATA_ACK ||
      status == I2C_SLA_R_ACK) {
    return true;
  }

  i2c_error = true;
  return false;
}


uint8_t i2c_read() {

  if (i2c_error) {
    return 0;
  }

  // Один байт: завершаем чтение NACK.
  TWCR = _BV(TWINT) | _BV(TWEN);

  if (!waitForTWINT()) {
    return 0;
  }

  if ((TWSR & 0xF8) != I2C_DATA_RX_NACK) {
    i2c_error = true;
    return 0;
  }

  return TWDR;
}


static void i2c_read_block(uint8_t* data, uint16_t count) {

  while (count-- && !i2c_error) {

    const bool acknowledgeNext = (count != 0);

    if (acknowledgeNext) {
      TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWEA);
    } else {
      TWCR = _BV(TWINT) | _BV(TWEN);
    }

    if (!waitForTWINT()) {
      return;
    }

    const uint8_t expectedStatus =
      acknowledgeNext ? I2C_DATA_RX_ACK : I2C_DATA_RX_NACK;

    if ((TWSR & 0xF8) != expectedStatus) {
      i2c_error = true;
      return;
    }

    *data++ = TWDR;
  }
}


void i2c_read(uint8_t* data, uint8_t count) {
  i2c_read_block(data, count);
}


void i2c_read_long(uint8_t* data, uint16_t count) {
  i2c_read_block(data, count);
}


bool i2c_begin_write(uint8_t addr) {

  i2c_error = false;

  if (!i2cStart()) {
    return false;
  }

  return i2c_write((uint8_t)(addr << 1));
}


bool i2c_begin_read(uint8_t addr) {

  i2c_error = false;

  if (!i2cStart()) {
    return false;
  }

  return i2c_write((uint8_t)((addr << 1) | 1U));
}


bool i2c_device_found(uint8_t addr) {

  const bool found = i2c_begin_write(addr);
  i2c_end();

  return found;
}
