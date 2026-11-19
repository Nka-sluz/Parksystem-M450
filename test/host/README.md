# Komponententests auf dem PC

Die Tests prüfen die hardwareunabhängige Logik aus `lib/ParkCore` anhand der
Szenarien aus dem Testkonzept. Servo, LED, WLAN/HTTP und Zeit sind durch Test
Doubles ersetzt (`TestDoubles.h`; Begründung im Haupt-README, Abschnitt
„Automatisierte Tests mit Test Doubles").

| Datei | Inhalt |
|---|---|
| `test_main.cpp` | startet alle Tests, zählt Prüfungen und Fehler |
| `TestFramework.h` | `CHECK`, `CHECK_STR` |
| `TestDoubles.h` | `FakeClock`, `BarrierMock`, `LampSpy`, `LampDummy`, `KasseStub`, `KasseFake` |
| `parkcore_test.cpp` | Pico 1: `ParkingSystem`, Tarif, Kennzeichen, Protokoll, HTTP-Format |
| `gate_controller_test.cpp` | Pico 2: `GateController`, auch zusammen mit Pico 1 über `KasseFake` |

Voraussetzung: Testkonfiguration (`PARK_TEST_CONFIG 1` in `include/Config.h`).

## Windows (empfohlen)

Voraussetzung: Visual Studio oder „Visual Studio Build Tools" mit
„Desktopentwicklung mit C++". Das Skript sucht den Compiler selbst, ein
Developer Prompt ist nicht nötig.

In PowerShell oder cmd im Repository-Ordner:

```powershell
.\test\host\run-tests.bat
```

Erwartete Ausgabe: `383 Prüfungen, 0 Fehler`, danach der zweite Build für
TC-15 mit `TC-15: 4 Prüfungen, 0 Fehler` (Exit-Code 0).
Die Build-Dateien landen in `.pio\host-test\` (von Git ignoriert).

TC-15 (Konfigurierbarkeit): Das Skript kopiert `include/Config.h` nach
`.pio\host-test\tc15\`, ändert dort nur `TARIFF_PRICE_PER_UNIT` von 100 auf
200, kompiliert alles neu und führt `testTc15` aus (Gebühr nach 2 Min. 10 s
= CHF 6.00, Höchstsatz folgt mit). Das Original bleibt unverändert.

## Linux / macOS / Git Bash mit g++

```sh
g++ -std=c++17 -Iinclude -Ilib/ParkCore/src test/host/*.cpp lib/ParkCore/src/*.cpp -o parkcore_test && ./parkcore_test

# TC-15: nur den Preis pro Einheit ändern und neu kompilieren
mkdir -p .pio/tc15
sed 's/TARIFF_PRICE_PER_UNIT = 100;/TARIFF_PRICE_PER_UNIT = 200;/' include/Config.h > .pio/tc15/Config.h
g++ -std=c++17 -DTC15_PRICE=200 -I.pio/tc15 -Iinclude -Ilib/ParkCore/src test/host/*.cpp lib/ParkCore/src/*.cpp -o tc15_test && ./tc15_test
```
