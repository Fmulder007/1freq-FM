@echo off
chcp 1251 >nul
setlocal enabledelayedexpansion

echo ============================================
echo  Генератор EEPROM (Intel HEX) для 1freq
echo  UP - частота при замкнутом пине (GND)
echo  DN - частота при свободном пине
echo ============================================
echo.

REM ====== Чтение существующего eeprom.hex ======
set "UP_CUR=не найдено"
set "DN_CUR=не найдено"
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

REM ====== Парсинг UP= (маркер 55503D) ======
set "FOUND_UP=0"
set "UP_CUR=не найдено"
if not defined DATASTR goto skip_up
set "TMP=!DATASTR:*55503D=!"
if "!TMP!"=="!DATASTR!" goto skip_up
set "FOUND_UP=1"
set "UP_CUR="

:up_loop
if "!TMP!"=="" goto up_done
set "B=!TMP:~0,2!"
if "!B!"=="00" goto up_done
set "UP_CUR=!UP_CUR!!B:~1,1!"
set "TMP=!TMP:~2!"
goto up_loop

:up_done
:skip_up

REM ====== Парсинг DN= (маркер 444E3D) ======
set "FOUND_DN=0"
set "DN_CUR=не найдено"
if not defined DATASTR goto skip_dn
set "TMP=!DATASTR:*444E3D=!"
if "!TMP!"=="!DATASTR!" goto skip_dn
set "FOUND_DN=1"
set "DN_CUR="

:dn_loop
if "!TMP!"=="" goto dn_done
set "B=!TMP:~0,2!"
if "!B!"=="00" goto dn_done
set "DN_CUR=!DN_CUR!!B:~1,1!"
set "TMP=!TMP:~2!"
goto dn_loop

:dn_done
:skip_dn

echo Текущие частоты в eeprom.hex:
echo   UP: !UP_CUR! Гц
echo   DN: !DN_CUR! Гц
echo.

REM ====== Ввод новых значений ======
if not "!FOUND_UP!"=="1" goto input_up_new

set /p "UP_NEW=Частота UP (Гц) [Enter=!UP_CUR!]: "
if "!UP_NEW!"=="" set "UP_NEW=!UP_CUR!"
goto after_up

:input_up_new
set /p "UP_NEW=Частота UP (Гц): "

:after_up
echo.

if not "!FOUND_DN!"=="1" goto input_dn_new

set /p "DN_NEW=Частота DN (Гц) [Enter=!DN_CUR!]: "
if "!DN_NEW!"=="" set "DN_NEW=!DN_CUR!"
goto after_dn

:input_dn_new
set /p "DN_NEW=Частота DN (Гц): "

:after_dn
echo.

REM ====== Если ничего не изменилось ======
if "!FOUND_UP!"=="1" if "!FOUND_DN!"=="1" (
  if "!UP_NEW!"=="!UP_CUR!" if "!DN_NEW!"=="!DN_CUR!" (
    echo Значения не изменились.
    echo eeprom.hex оставлен без изменений.
    echo.
    pause
    goto :eof
  )
)

echo Новые значения:
echo   UP=!UP_NEW! Гц
echo   DN=!DN_NEW! Гц
echo.

if "!UP_NEW!"=="" echo ОШИБКА: Не введена частота UP & pause & goto :eof
if "!DN_NEW!"=="" echo ОШИБКА: Не введена частота DN & pause & goto :eof

REM ====== Генерация hex-данных UP ======
set "UP_HEX=55503D"
set "TMP=!UP_NEW!"

:gen_up
if "!TMP!"=="" goto gen_up_done

set "D=!TMP:~0,1!"
set "UP_HEX=!UP_HEX!3!D!"
set "TMP=!TMP:~1!"

goto gen_up

:gen_up_done

REM Дополнение UP нулями до 16 байт
set "BL=0"
set "TMP=!UP_HEX!"

:cnt_up
if "!TMP!"=="" goto cnt_up_done
set "TMP=!TMP:~2!"
set /a BL+=1
goto cnt_up

:cnt_up_done
set /a PAD=16-BL

:pad_up
if !PAD! leq 0 goto pad_up_done
set "UP_HEX=!UP_HEX!00"
set /a PAD-=1
goto pad_up

:pad_up_done

REM ====== Генерация hex-данных DN ======
set "DN_HEX=444E3D"
set "TMP=!DN_NEW!"

:gen_dn
if "!TMP!"=="" goto gen_dn_done

set "D=!TMP:~0,1!"
set "DN_HEX=!DN_HEX!3!D!"
set "TMP=!TMP:~1!"

goto gen_dn

:gen_dn_done

REM Дополнение DN нулями до 16 байт
set "BL=0"
set "TMP=!DN_HEX!"

:cnt_dn
if "!TMP!"=="" goto cnt_dn_done
set "TMP=!TMP:~2!"
set /a BL+=1
goto cnt_dn

:cnt_dn_done
set /a PAD=16-BL

:pad_dn
if !PAD! leq 0 goto pad_dn_done
set "DN_HEX=!DN_HEX!00"
set /a PAD-=1
goto pad_dn

:pad_dn_done

REM ====== Контрольная сумма UP (адрес 0x0000) ======
set /a SUM=0x10+0x00+0x00+0x00
set "TMP=!UP_HEX!"

:sum_up
if "!TMP!"=="" goto sum_up_done

set "B=!TMP:~0,2!"
set /a SUM+=0x!B!
set "TMP=!TMP:~2!"

goto sum_up

:sum_up_done
set /a CKSUM=256-(SUM%%256)

if !CKSUM! geq 256 set /a CKSUM-=256

set "HEXD=0123456789ABCDEF"
set /a HI=CKSUM/16
set /a LO=CKSUM%%16

call set "CKHI=%%HEXD:~!HI!,1%%"
call set "CKLO=%%HEXD:~!LO!,1%%"

set "UP_CK=!CKHI!!CKLO!"

REM ====== Контрольная сумма DN (адрес 0x0010) ======
set /a SUM=0x10+0x00+0x10+0x00
set "TMP=!DN_HEX!"

:sum_dn
if "!TMP!"=="" goto sum_dn_done

set "B=!TMP:~0,2!"
set /a SUM+=0x!B!
set "TMP=!TMP:~2!"

goto sum_dn

:sum_dn_done
set /a CKSUM=256-(SUM%%256)

if !CKSUM! geq 256 set /a CKSUM-=256

set /a HI=CKSUM/16
set /a LO=CKSUM%%16

call set "CKHI=%%HEXD:~!HI!,1%%"
call set "CKLO=%%HEXD:~!LO!,1%%"

set "DN_CK=!CKHI!!CKLO!"

REM ====== Запись файла ======
> "eeprom.hex" (
  echo :10000000!UP_HEX!!UP_CK!
  echo :10001000!DN_HEX!!DN_CK!
  echo :00000001FF
)

echo eeprom.hex успешно создан!
echo.
echo Прошейте eeprom.hex через AVRDUDE_PROG 3.3:
echo   Memory: EEPROM  Format: Intel HEX  File: eeprom.hex
echo.

pause