@echo off
rem Baut beide Firmwares und legt sie als firmware\kasse.uf2 und
rem firmware\schranken.uf2 ab. Danach per Explorer auf die Picos kopieren
rem (Anleitung: README.md, Abschnitt "Flashen per Explorer").
rem Start per Doppelklick oder aus PowerShell/cmd: .\build-firmware.bat
setlocal

cd /d "%~dp0"

set "PIO=pio"
where pio >nul 2>nul || set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not "%PIO%"=="pio" if not exist "%PIO%" (
    echo PlatformIO nicht gefunden. Bitte in VS Code die Erweiterung "PlatformIO IDE" installieren.
    goto fail
)

echo Baue Firmware fuer Pico 1 ^(kasse^) und Pico 2 ^(schranken^) ...
"%PIO%" run -e kasse -e schranken
if errorlevel 1 goto fail

if not exist firmware mkdir firmware
copy /y ".pio\build\kasse\firmware.uf2" "firmware\kasse.uf2" >nul || goto fail
copy /y ".pio\build\schranken\firmware.uf2" "firmware\schranken.uf2" >nul || goto fail

echo.
echo Fertig:
echo   firmware\kasse.uf2      -^> auf Pico 1 ^(Kasse^) kopieren
echo   firmware\schranken.uf2  -^> auf Pico 2 ^(Schranken^) kopieren
start "" explorer "%~dp0firmware"
call :pauseIfDoubleClicked
exit /b 0

:fail
echo.
echo Build fehlgeschlagen.
call :pauseIfDoubleClicked
exit /b 1

:pauseIfDoubleClicked
rem Bei Doppelklick bleibt das Fenster offen, damit man die Ausgabe lesen kann.
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b 0
