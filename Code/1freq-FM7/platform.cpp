#include <Arduino.h>
#include "platform.h"


void platform_init() {

  // LGT8Fx поддерживает выбор разрешения АЦП.
  // Явно оставляем совместимый с ATmega диапазон 0..1023.
#if defined(__LGT8F__)
  analogReadResolution(10);
#endif
}
