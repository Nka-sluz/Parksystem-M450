@echo off
rem Baut und startet die Komponententests (lib/ParkCore) mit MSVC.
rem Aufruf aus PowerShell oder cmd, egal aus welchem Ordner:
rem     .\test\host\run-tests.bat
setlocal enabledelayedexpansion

set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\.pio\host-test"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Ist cl.exe schon verfuegbar (Developer Prompt)? Dann direkt bauen.
where cl >nul 2>nul && goto build

if not exist "%VSWHERE%" goto novs

rem Neueste Visual-Studio-Installation mit C++-Werkzeugen und vollstaendigem vcvarsall.bat suchen.
set "VCVARS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -sort -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if not defined VCVARS if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
)
if not defined VCVARS goto novs

echo Verwende: !VCVARS!
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "!VCVARS!" x64 >nul
where cl >nul 2>nul || goto novs

:build
if not exist "%OUT%" mkdir "%OUT%"
pushd "%ROOT%"
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Ilib\ParkCore\src ^
   test\host\parkcore_test.cpp lib\ParkCore\src\*.cpp /Fo"%OUT%\\" /Fe"%OUT%\parkcore_test.exe"
if errorlevel 1 (
    popd
    echo.
    echo Build fehlgeschlagen.
    exit /b 1
)
popd
echo.
"%OUT%\parkcore_test.exe"
exit /b %errorlevel%

:novs
echo Kein C++-Compiler gefunden. Bitte "Visual Studio Build Tools" mit der
echo Arbeitsauslastung "Desktopentwicklung mit C++" installieren.
exit /b 1
