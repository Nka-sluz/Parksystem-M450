# Komponententests auf dem PC

`parkcore_test.cpp` prüft die hardwareunabhängige Logik aus `lib/ParkCore`
(Kennzeichenprüfung, Tarif, Register, Bezahlung, Protokoll) anhand der
Szenarien aus dem Testkonzept. Die Tests ergänzen die manuellen System- und
Abnahmetests und ersetzen sie nicht (Schranken, WLAN, Weboberflächen werden
weiterhin manuell geprüft).

Voraussetzung: Testkonfiguration (`PARK_TEST_CONFIG 1` in `include/Config.h`).

## Windows (empfohlen)

Voraussetzung: Visual Studio oder „Visual Studio Build Tools" mit
„Desktopentwicklung mit C++". Das Skript sucht den Compiler selbst, ein
Developer Prompt ist nicht nötig.

In PowerShell oder cmd im Repository-Ordner:

```powershell
.\test\host\run-tests.bat
```

Erwartete letzte Zeile: `242 Prüfungen, 0 Fehler` (Exit-Code 0).
Die Build-Dateien landen in `.pio\host-test\` (von Git ignoriert).

## Linux / macOS / Git Bash mit g++

```sh
g++ -std=c++17 -Iinclude -Ilib/ParkCore/src test/host/parkcore_test.cpp lib/ParkCore/src/*.cpp -o parkcore_test && ./parkcore_test
```
