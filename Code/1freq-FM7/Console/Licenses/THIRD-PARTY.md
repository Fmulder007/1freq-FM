# Сторонние компоненты

Этот каталог сопровождает внешние инструменты, которые входят в готовую Windows-сборку.

## AVRDUDE 8.3

- Проект: https://github.com/avrdudes/avrdude
- Лицензия: GNU GPL version 2.
- Используемая сборка: Windows MSYS2 MinGW x64.
- Официальный исходный архив: Source/avrdude-v8.3-source.zip (создаётся скриптом Tools/prepare-tools.ps1).
- Текст лицензии: GPL-2.0-AVRDUDE.txt.

## Zadig 2.9

- Проект и загрузка: https://zadig.akeo.ie/
- Исходный код: https://github.com/pbatard/libwdi
- Лицензия самого Zadig: GNU GPL version 3 or later.
- Библиотека libwdi: GNU LGPL version 3 or later.
- Официальный исходный архив libwdi 1.5.1: Source/libwdi-v1.5.1-source.zip (создаётся скриптом Tools/prepare-tools.ps1).
- Тексты лицензий: GPL-3.0-Zadig.txt и LGPL-3.0-libwdi.txt.

Программа 1freq-FM Console является отдельным приложением и вызывает AVRDUDE/Zadig как внешние процессы. Эти инструменты не встраиваются в исходный код 1freq-FM Console.

Перед публикацией готового ZIP необходимо запускать Tools/prepare-tools.ps1, чтобы положить в комплект исполняемые файлы и соответствующие исходные архивы.
