@echo off
chcp 1251 >nul
setlocal enabledelayedexpansion

echo ============================================
echo  Генератор EEPROM (Intel HEX) для 1freq-POT
echo  FR - базовая частота, Гц
echo  BW - ширина диапазона перестройки, Гц
echo  ST - количество шагов (0 = 1024, прямой режим)
echo ============================================
echo.

REM ====== Чтение существующего eeprom.hex ======
set "FR_CUR=не найдено"
set "BW_CUR=не найдено"
set "ST_CUR=не найдено"
set "DATASTR="

if not exist "eeprom.hex" goto after_read

for /f "usebackq delims=" %%L in ("eeprom.hex") do (
  set "LINE=%%L"
  if "!LINE:~0,1!"==":" (
    set "RAW=!LINE:~9!"
    set "RAW=!RAW:~0,-2!"
    set "DATASTR=!DATASTR!!RAW!"
  )
)

:after_read

REM ====== Парсинг FR= (маркер 46523D) ======
set "FOUND_FR=0"
set "FR_CUR=не найдено"
if not defined DATASTR goto skip_fr
set "TMP=!DATASTR:*46523D=!"
if "!TMP!"=="!DATASTR!" goto skip_fr
set "FOUND_FR=1"
set "FR_CUR="

:fr_loop
if "!TMP!"=="" goto fr_done
set "B=!TMP:~0,2!"
if "!B!"=="00" goto fr_done
set "FR_CUR=!FR_CUR!!B:~1,1!"
set "TMP=!TMP:~2!"
goto fr_loop

:fr_done
:skip_fr

REM ====== Парсинг BW= (маркер 42573D) ======
set "FOUND_BW=0"
set "BW_CUR=не найдено"
if not defined DATASTR goto skip_bw
set "TMP=!DATASTR:*42573D=!"
if "!TMP!"=="!DATASTR!" goto skip_bw
set "FOUND_BW=1"
set "BW_CUR="

:bw_loop
if "!TMP!"=="" goto bw_done
set "B=!TMP:~0,2!"
if "!B!"=="00" goto bw_done
set "BW_CUR=!BW_CUR!!B:~1,1!"
set "TMP=!TMP:~2!"
goto bw_loop

:bw_done
:skip_bw

REM ====== Парсинг ST= (маркер 53543D) ======
set "FOUND_ST=0"
set "ST_CUR=не найдено"
if not defined DATASTR goto skip_st
set "TMP=!DATASTR:*53543D=!"
if "!TMP!"=="!DATASTR!" goto skip_st
set "FOUND_ST=1"
set "ST_CUR="

:st_loop
if "!TMP!"=="" goto st_done
set "B=!TMP:~0,2!"
if "!B!"=="00" goto st_done
set "ST_CUR=!ST_CUR!!B:~1,1!"
set "TMP=!TMP:~2!"
goto st_loop

:st_done
:skip_st

echo Текущие значения в eeprom.hex:
echo   FR: !FR_CUR! Гц
echo   BW: !BW_CUR! Гц
echo   ST: !ST_CUR! ^(!ST_CUR! шагов^)
echo.

REM ====== Ввод FR ======
if not "!FOUND_FR!"=="1" goto input_fr_new
set /p "FR_NEW=Базовая частота FR (Гц) [Enter=!FR_CUR!]: "
if "!FR_NEW!"=="" set "FR_NEW=!FR_CUR!"
goto after_fr

:input_fr_new
set /p "FR_NEW=Базовая частота FR (Гц): "

:after_fr
echo.

REM ====== Ввод BW ======
if not "!FOUND_BW!"=="1" goto input_bw_new
set /p "BW_NEW=Ширина диапазона BW (Гц) [Enter=!BW_CUR!]: "
if "!BW_NEW!"=="" set "BW_NEW=!BW_CUR!"
goto after_bw

:input_bw_new
set /p "BW_NEW=Ширина диапазона BW (Гц): "

:after_bw
echo.

REM ====== Ввод ST ======
if not "!FOUND_ST!"=="1" goto input_st_new
set /p "ST_NEW=Количество шагов ST [Enter=!ST_CUR!]: "
if "!ST_NEW!"=="" set "ST_NEW=!ST_CUR!"
goto after_st

:input_st_new
set /p "ST_NEW=Количество шагов ST (0 = 1024): "

:after_st
echo.

REM ====== Проверка ввода ======
if "!FR_NEW!"=="" echo ОШИБКА: Не введена частота FR & pause & goto :eof
if "!BW_NEW!"=="" echo ОШИБКА: Не введена ширина BW & pause & goto :eof
if "!ST_NEW!"=="" echo ОШИБКА: Не введено количество шагов ST & pause & goto :eof

REM ====== Если ничего не изменилось ======
if "!FOUND_FR!"=="1" if "!FOUND_BW!"=="1" if "!FOUND_ST!"=="1" (
  if "!FR_NEW!"=="!FR_CUR!" if "!BW_NEW!"=="!BW_CUR!" if "!ST_NEW!"=="!ST_CUR!" (
    echo Значения не изменились.
    echo eeprom.hex оставлен без изменений.
    echo.
    pause
    goto :eof
  )
)

echo Новые значения:
echo   FR=!FR_NEW! Гц
echo   BW=!BW_NEW! Гц
echo   ST=!ST_NEW! шагов
echo.

REM ====== Генерация hex-данных FR ======
call :gen_hex "FR_HEX" "46523D" "!FR_NEW!"

REM ====== Генерация hex-данных BW ======
call :gen_hex "BW_HEX" "42573D" "!BW_NEW!"

REM ====== Генерация hex-данных ST ======
call :gen_hex "ST_HEX" "53543D" "!ST_NEW!"

REM ====== Контрольная сумма FR (адрес 0x0000) ======
call :calc_cksum "FR_CK" "!FR_HEX!" "0x10+0x00+0x00+0x00"

REM ====== Контрольная сумма BW (адрес 0x0010) ======
call :calc_cksum "BW_CK" "!BW_HEX!" "0x10+0x00+0x10+0x00"

REM ====== Контрольная сумма ST (адрес 0x0020) ======
call :calc_cksum "ST_CK" "!ST_HEX!" "0x10+0x00+0x20+0x00"

REM ====== Запись файла ======
> "eeprom.hex" (
  echo :10000000!FR_HEX!!FR_CK!
  echo :10001000!BW_HEX!!BW_CK!
  echo :10002000!ST_HEX!!ST_CK!
  echo :00000001FF
)

echo eeprom.hex успешно создан!
echo.
echo Прошейте eeprom.hex через AVRDUDE_PROG 3.3:
echo   Memory: EEPROM  Format: Intel HEX  File: eeprom.hex
echo.

pause
goto :eof


REM ============================================================
REM Подпрограмма: генерация hex-строки параметра
REM
REM   %1 — имя переменной для результата
REM   %2 — hex-маркер (6 символов, 3 байта)
REM   %3 — десятичное значение (строка цифр)
REM
REM Результат: маркер + 3X для каждой цифры + 00 (добивка до 16 байт)
REM ============================================================

:gen_hex
set "OUT=%~2"
set "TMP=%~3"

:gen_hex_loop
if "!TMP!"=="" goto gen_hex_done
set "D=!TMP:~0,1!"
set "OUT=!OUT!3!D!"
set "TMP=!TMP:~1!"
goto gen_hex_loop

:gen_hex_done
REM Дополнение нулями до 16 байт (32 hex-символа)
set "BL=0"
set "TMP=!OUT!"
:gen_hex_cnt
if "!TMP!"=="" goto gen_hex_cnt_done
set "TMP=!TMP:~2!"
set /a BL+=1
goto gen_hex_cnt
:gen_hex_cnt_done
set /a PAD=16-BL
:gen_hex_pad
if !PAD! leq 0 goto gen_hex_pad_done
set "OUT=!OUT!00"
set /a PAD-=1
goto gen_hex_pad
:gen_hex_pad_done
set "%~1=!OUT!"
goto :eof


REM ============================================================
REM Подпрограмма: расчёт контрольной суммы Intel HEX
REM
REM   %1 — имя переменной для результата (2 hex-символа)
REM   %2 — hex-строка данных (32 символа = 16 байт)
REM   %3 — начальная сумма (заголовок записи)
REM
REM Формула: CKSUM = (256 - (SUM %% 256)) %% 256
REM ============================================================

:calc_cksum
set /a SUM=%~3
set "TMP=%~2"
:calc_cksum_loop
if "!TMP!"=="" goto calc_cksum_done
set "B=!TMP:~0,2!"
set /a SUM+=0x!B!
set "TMP=!TMP:~2!"
goto calc_cksum_loop
:calc_cksum_done
set /a CKSUM=256-(SUM%%256)
if !CKSUM! geq 256 set /a CKSUM-=256
set "HEXD=0123456789ABCDEF"
set /a HI=CKSUM/16
set /a LO=CKSUM%%16
call set "CKHI=%%HEXD:~!HI!,1%%"
call set "CKLO=%%HEXD:~!LO!,1%%"
set "%~1=!CKHI!!CKLO!"
goto :eof
