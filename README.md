# Parksystem M450

Parkhaus mit Ein- und Ausfahrtsschranke, Kennzeichenerfassung,
Gebührenberechnung und Bezahlung – umgesetzt mit **2 × Raspberry Pi Pico W**
in **Arduino (C++17)** mit PlatformIO.

## Gruppenmitglieder

- Nataliia Kakhnych

## Kurze Beschreibung der Umsetzung

| | Pico 1 „Kasse" | Pico 2 „Schranken" |
|---|---|---|
| Rolle | Leitstelle: Fahrzeugregister, Gebühren, Zahlungen, Protokoll | Steuert beide Schranken, simuliert die Kamera |
| WLAN | spannt das WLAN `Parkhaus-M450` auf (Access Point) | verbindet sich mit diesem WLAN |
| Adresse | <http://192.168.42.1/> | <http://192.168.42.2/> |
| Weboberfläche | Kasse/Zahlautomat (Gebühr anzeigen, bezahlen), Belegung, Register, Protokoll | Kamera-Simulation: Kennzeichen an Ein-/Ausfahrt eingeben, manuelle Abfertigung, Durchfahrt bestätigen |
| Hardware | – | SG90-Servo als Schranke (GP8; optional zweiter Servo für die Ausfahrt an GP9), Onboard-LED (an = Schranke offen) |
| Code | `src/kasse/main.cpp` | `src/schranken/main.cpp`, `src/schranken/Gate.h` |

- **Entscheidungen trifft immer Pico 1.** Pico 2 meldet jedes erfasste
  Kennzeichen per HTTP an Pico 1 und öffnet die Schranke nur bei einer
  Freigabe. Ist Pico 1 nicht erreichbar, bleiben die Schranken zu
  (**Fail-safe**). Das Protokoll ist in
  [docs/HTTP-Protokoll.md](docs/HTTP-Protokoll.md) beschrieben.
- **Robust bei wackeligem WLAN:** Pico 2 wiederholt fehlgeschlagene Anfragen
  an die Kasse (bis zu 3 Versuche), die Weboberflächen ebenso. Jede Aktion
  trägt eine Request-ID; eine Wiederholung bekommt die gespeicherte Antwort,
  statt die Aktion doppelt auszuführen (keine doppelte Registrierung oder
  Zahlung). Jede Antwort schliesst die Verbindung sofort, weil der WebServer
  des Pico sonst bis zu 5 s auf den Browser wartet und andere Anfragen blockiert.
- **Rückmeldung in der Oberfläche:** Während einer Aktion sind die Buttons
  gesperrt und eine Ladeanzeige läuft. Beide Seiten aktualisieren sich
  automatisch (Kamera-Simulation jede Sekunde, Kasse alle 3 s) und zeigen
  den Verbindungsstatus; zusätzlich gibt es einen Button „Aktualisieren".
- **Gemeinsame Logik** (Kennzeichenprüfung, Tarif, Register, Protokoll,
  Protokollformat) liegt hardwareunabhängig in `lib/ParkCore/`. Die Zeit wird
  als Parameter übergeben (`millis()` auf dem Pico).
- **Zentrale Konfiguration** in [`include/Config.h`](include/Config.h):
  Kapazität, Tarif (Zeiteinheit, Preis, Freigrenze, Tageshöchstsatz),
  Karenzzeit, Schranken-Timeout, Protokoll-Obergrenze, WLAN und Servo-Pins.
  `PARK_TEST_CONFIG 1` aktiviert die Testkonfiguration aus dem Testkonzept
  (3 Plätze, 1 Min. Zeiteinheit/Freigrenze, CHF 1.00 pro Minute,
  Höchstsatz CHF 5.00, Karenzzeit 2 Min., 10 Protokolleinträge);
  `0` die Produktivwerte.
- **Kein dynamischer Speicher in der eigenen Logik:** Register
  (`CAPACITY + 10` Einträge) und Protokoll (Ringpuffer) haben feste
  Obergrenzen; HTML wird stückweise aus einem festen Puffer gesendet.
- Geldbeträge werden ganzzahlig in Rappen gerechnet.

### Abweichung von den Anforderungen

Die Anforderungen sahen „keine Servos" vor und simulieren die Schranke nur mit
der Onboard-LED. Für die Umsetzung stand ein SG90-Servo zur Verfügung, darum
bewegt Pico 2 zusätzlich einen Servo als Schranke. Ein- und Ausfahrt bleiben
zwei logisch getrennte Schranken (eigener Status „Einfahrt offen/zu" bzw.
„Ausfahrt offen/zu", eigene Meldungen); der eine Servo ist ihre gemeinsame
physische Schranke. Die LED leuchtet weiterhin, solange eine Schranke offen
ist, sodass alle Testfälle unverändert gelten. Ohne Servo läuft das System
genauso.

## Aufbau / Verdrahtung (Pico 2)

**Standard: ein Servo** (`SERVO_COUNT = 1` in `include/Config.h`) – eine
gemeinsame Schranke für Ein- und Ausfahrt:

| SG90-Kabel | Pico 2 |
|---|---|
| orange (Signal) | GP8 (Pin 11) |
| rot (VCC) | VBUS (Pin 40) |
| braun (GND) | GND (z. B. Pin 38) |

Ist die Schranke gerade für die Einfahrt offen, wird eine Ausfahrt mit
„Schranke gerade für die Einfahrt … offen – bitte warten" abgewiesen (und
umgekehrt), bis die Durchfahrt bestätigt ist oder der 30-s-Timeout greift.

**Optional: zwei Servos** (`SERVO_COUNT = 2`) – getrennte Schranken, Einfahrt
an GP8 (Pin 11) und Ausfahrt an GP9 (Pin 12); rot/braun beider Servos an
VBUS/GND, am besten über die Plus/Minus-Schiene des Breadboards.

Pins und Winkel (`SERVO_OPEN_DEG`, `SERVO_CLOSED_DEG`) sind in
`include/Config.h` einstellbar. Nach einer Änderung Pico 2 neu flashen.
Pico 1 braucht nur USB-Strom.

**Servo prüfen:** Beim Einschalten öffnet und schliesst Pico 2 die
Schranke(n) einmal (Selbsttest). Auf der Kamera-Simulation kann der Test unter
„Status" jederzeit mit **„Schranken testen"** wiederholt werden. Der
Seitenkopf zeigt, welche Variante aktiv ist. Bewegt sich ein Servo dabei nicht:

| Prüfen | Hinweis |
|---|---|
| Signal am richtigen Pin? | GP8 ist **Pin 11** (linke Seite, 11. Pin von oben bei USB-Buchse oben), GP9 ist Pin 12. „GP8" ≠ „Pin 8" (Pin 8 ist GND) |
| Am richtigen Pico? | Der Servo gehört an **Pico 2 (Schranken)** |
| Strom? | Rot an **VBUS (Pin 40)**, nicht an 3V3. VBUS liefert nur 5 V, wenn der Pico per USB versorgt wird |
| Masse? | Braun an einen **GND**-Pin desselben Pico |
| Stecker richtig herum? | Reihenfolge am Servo-Stecker: orange – rot – braun |
| Breadboard? | Pico-Pin und Kabel müssen in derselben 5er-Reihe stecken; die Plus/Minus-Schienen sind bei manchen Breadboards in der Mitte unterbrochen |
| Firmware? | Serieller Monitor (115200 Baud) zeigt beim Start `Servo Einfahrt GP8: bereit`; steht dort `FEHLER` oder zeigt die Seite „Servo-Ausgang nicht bereit", liegt es an der Software |

## Build- und Bedienanleitung

**Voraussetzung:** VS Code mit der PlatformIO-Erweiterung (oder PlatformIO Core).

### Flashen per Explorer (empfohlen)

Der Pico meldet sich im BOOTSEL-Modus als USB-Laufwerk. Eine `.uf2`-Datei,
die man darauf kopiert, wird als Firmware installiert – kein Upload-Tool nötig.

**Tipp:** Beide Picos vorher beschriften (z. B. Klebeband „1 Kasse" /
„2 Schranken"), damit die Firmware nicht vertauscht wird. Am PC immer nur
den Pico anschliessen, der gerade geflasht wird.

1. **Firmware bauen:** `build-firmware.bat` im Repository-Ordner
   doppelklicken (oder in PowerShell `.\build-firmware.bat`). Das dauert
   beim ersten Mal 1–3 Minuten. Danach öffnet sich der Ordner `firmware\` mit
   - `kasse.uf2` → für **Pico 1 (Kasse)**
   - `schranken.uf2` → für **Pico 2 (Schranken)**
2. **Pico 1 in den BOOTSEL-Modus bringen:** USB-Kabel vom Pico abziehen,
   die weisse Taste **BOOTSEL** auf dem Pico gedrückt halten, Kabel
   einstecken, Taste loslassen.
3. Im Explorer erscheint ein neues Laufwerk **`RPI-RP2`** (enthält
   `INDEX.HTM` und `INFO_UF2.TXT`).
4. **`kasse.uf2` auf das Laufwerk `RPI-RP2` ziehen** (Drag & Drop oder
   Kopieren/Einfügen).
5. Nach wenigen Sekunden verschwindet das Laufwerk von selbst und der Pico
   startet neu – das ist normal. Eine Windows-Meldung „Laufwerk wurde nicht
   ordnungsgemäss entfernt" kann ignoriert werden.
   **Kontrolle:** Die Onboard-LED von Pico 1 leuchtet und das WLAN
   `Parkhaus-M450` ist sichtbar.
6. Pico 1 abziehen, **Pico 2** genauso mit **`schranken.uf2`** flashen
   (Schritte 2–5).
   **Kontrolle:** Der Servo öffnet und schliesst einmal (Selbsttest), die LED bleibt danach aus.
   Auf <http://192.168.42.2/> zeigt „Verbindung zur Kasse" erst „OK", wenn
   Pico 1 ebenfalls läuft.

**Nach Änderungen** (z. B. in `include/Config.h`): `build-firmware.bat`
erneut ausführen und **beide** Picos neu flashen, da die Konfiguration für
beide gilt.

**Probleme:**

| Problem | Lösung |
|---|---|
| Laufwerk `RPI-RP2` erscheint nicht | BOOTSEL wirklich *vor* dem Einstecken gedrückt halten; anderes USB-Kabel probieren (viele Kabel können nur laden, keine Daten); anderen USB-Port verwenden |
| Pico startet nach dem Kopieren nicht wie erwartet | Richtige Datei für diesen Pico? Notfalls erneut in BOOTSEL bringen und nochmals kopieren – das ist jederzeit möglich |
| `build-firmware.bat`: „PlatformIO nicht gefunden" | In VS Code die Erweiterung „PlatformIO IDE" installieren und VS Code einmal starten, bis die Installation fertig ist |

### Alternative: Upload mit PlatformIO

> `pio` ist nach der Installation der VS-Code-Erweiterung meist **nicht** im
> PATH einer normalen PowerShell. Dann entweder in VS Code über die
> PlatformIO-Seitenleiste (*Project Tasks → kasse/schranken → Upload*) bzw.
> *PlatformIO: New Terminal* arbeiten, oder `pio` mit vollem Pfad aufrufen:
> `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e kasse -t upload`

- Pico 1: `pio run -e kasse -t upload`, Pico 2: `pio run -e schranken -t upload`
  (beim ersten Mal BOOTSEL gedrückt halten beim Einstecken).

### Bedienung

1. Beide Picos geflasht (siehe oben).
2. Beide Picos mit Strom versorgen (USB). Die LED von Pico 1 leuchtet,
   sobald das WLAN aktiv ist.
3. Laptop/Smartphone mit dem WLAN **`Parkhaus-M450`** verbinden
   (Passwort `parkhaus450`).
4. **Kamera-Simulation** öffnen: <http://192.168.42.2/>
   - *Einfahrt:* Kennzeichen eingeben → „Kennzeichen erfassen". Bei
     Freigabe öffnet die Schranke (Servo + LED). „Durchfahrt bestätigen"
     schliesst sie, sonst schliesst sie nach 30 s automatisch.
   - *Manuelle Abfertigung (Einfahrt):* ohne Kennzeichen „Manuelle
     Abfertigung" klicken → Ersatz-ID `MANUAL-<n>` wird angezeigt.
   - *Ausfahrt:* Kennzeichen eingeben → „Kennzeichen erfassen". Öffnet nur,
     wenn nichts mehr offen ist; sonst erscheint „Bitte zuerst bezahlen" mit
     Betrag. Bei manueller Abfertigung die Ersatz-ID eingeben und „Manuelle
     Abfertigung" klicken.
5. **Kasse / Zahlautomat** öffnen: <http://192.168.42.1/>
   - Kennzeichen (oder Ersatz-ID) eingeben → „Gebühr anzeigen" bzw.
     „Bezahlen". Das Betragsfeld ist optional (leer = passend bezahlen;
     mit Betrag wird das Rückgeld berechnet bzw. ein zu kleiner Betrag
     abgelehnt).
   - Darunter: Belegung, Umsatz und Fahrzeugregister.
   - **Protokoll:** <http://192.168.42.1/log> (CSV unter `/log.csv`).
6. Serielle Ausgabe (Diagnose): `pio device monitor` (115200 Baud).

Zeitstempel sind „Zeit seit Start der Kasse" (`HH:MM:SS`), da keine
Echtzeituhr vorgesehen ist. Ein Neustart beider Picos stellt den Grundzustand
her (keine Persistenz).

## Umgesetzte User Stories / Anforderungen

Details und Akzeptanzkriterien: Anforderungsdokument „Anforderungen
Parksystem"; Testfälle: „Testkonzept Parksystem" (TC-01 bis TC-41).

| Story / Anforderung | Status | Umsetzung |
|---|---|---|
| **US1** Automatische Einfahrt bei gültigem Kennzeichen | ✅ | Kasse prüft und registriert (Einfahrtszeit, „im Parkhaus, unbezahlt", freie Plätze −1, Protokoll); Schranke öffnet sofort, schliesst nach Bestätigung oder 30 s; Duplikat → „bereits eingefahren" |
| **US2** Einfahrt verweigern (ungültig / voll) | ✅ | Leer, falsche Zeichen, zu lang (> 10 Zeichen) → „Kennzeichen ungültig"; 0 freie Plätze → „Parkhaus voll" (auch bei manueller Einfahrt); kein Registereintrag, Ablehnung mit Grund protokolliert |
| **US3** Parkgebühr nach Parkdauer | ✅ | `Tariff::feeFor`: Freigrenze → 0, sonst angefangene Einheiten × Preis, pro Tag max. Höchstsatz; unbekanntes Kennzeichen → definierte Fehlermeldung; Tarif zentral in `Config.h` |
| **US4** Parkentgelt bezahlen | ✅ | „Bezahlen" an der Kasse → Status „bezahlt", offen 0, Zahlungsbeleg im Protokoll; nach Ablauf der Karenzzeit wird die Differenz nachzahlbar; unbekannt/ausgefahren → abgelehnt; Betrag 0 → „nichts zu bezahlen" |
| **US5** Ausfahrt nur nach Bezahlung | ✅ | Kasse gibt nur bei offenem Betrag 0 frei; sonst „Bitte zuerst bezahlen" + Betrag; unbekannt → Hinweis auf manuelle Abfertigung; Bestätigung → „ausgefahren", Platz frei; 30 s ohne Durchfahrt → zu, Fahrzeug bleibt im Parkhaus; alles protokolliert |
| **US6** Belegung anzeigen | ✅ | Kapazität/belegt/frei auf der Kasse, freie Plätze auch auf der Kamera-Simulation; Zähler wird aus dem Register berechnet (nie < 0, nie > Kapazität) |
| **US7** Protokoll für den Besitzer | ✅ | Ringpuffer mit fortlaufender Nummer; zeitlich sortiert; Überschreiben wird angezeigt (fehlende Nummern + Hinweis); CSV mit `;`-Trennung, 7 Felder pro Zeile |
| **US8** Manuelle Abfertigung | ✅ | Button „Manuelle Abfertigung" vergibt `MANUAL-<n>`; bei vollem Parkhaus abgelehnt; Ausfahrt mit Ersatz-ID wie US3–US5; im Protokoll Spalte `manuell = ja` |
| Fail-safe | ✅ | Ohne gültige Antwort der Kasse (3 Versuche à 1 s / WLAN weg) bleibt jede Schranke zu |
| Reaktionszeit ≤ 2 s | ✅ | Eine HTTP-Anfrage im lokalen WLAN, danach sofort Servo/LED |
| Datenkonsistenz | ✅ | Jedes Kennzeichen max. einmal im Register; doppelte Bestätigung wird auf Pico 2 und Pico 1 abgewiesen; höchstens `CAPACITY` Fahrzeuge „im Parkhaus" |
| Robuste Eingaben | ✅ | Eingaben werden gekürzt, validiert und HTML-escaped; Protokoll enthält nur bereinigten Text |

## Tests

- **Manuell (System-/Abnahmetest):** gemäss Testkonzept (TC-01 bis TC-41)
  mit beiden Picos und der Testkonfiguration.
- **Automatisiert (Komponententest):** `test/host/parkcore_test.cpp` prüft
  die Logik aus `lib/ParkCore` auf dem PC anhand derselben Szenarien
  (Kennzeichen, Tarif, Register, Bezahlung, Ausfahrt, Protokoll, manuelle
  Abfertigung), aktuell 254 Prüfungen ohne Fehler. Anleitung:
  [test/host/README.md](test/host/README.md).

## Projektstruktur

```
include/Config.h          zentrale Konstanten (Tarif, Kapazität, Timeouts, WLAN, Pins)
include/WebOut.h          HTML-Ausgabe aus festem Puffer
include/WebStyle.h        gemeinsames CSS
include/WebScript.h       gemeinsames JavaScript (Wiederholungen, Ladeanzeige)
lib/ParkCore/src/         hardwareunabhängige Logik
  Plate.*                 Kennzeichenprüfung
  Tariff.*                Gebührenberechnung
  ParkingSystem.*         Register + Ein-/Ausfahrt/Bezahlung
  EventLog.*              Protokoll (Ringpuffer, CSV)
  Protocol.*              HTTP-Protokoll Pico 2 ↔ Pico 1
  Format.*                Beträge/Zeiten formatieren
  ReplayCache.h           Antworten zu Request-IDs (Wiederholungen ohne Doppelbuchung)
src/kasse/main.cpp        Firmware Pico 1
src/schranken/            Firmware Pico 2
docs/HTTP-Protokoll.md    Schnittstellenbeschreibung
test/host/                Komponententests der Logik (PC)
platformio.ini            Environments "kasse" und "schranken"
build-firmware.bat        baut beide Firmwares → firmware\kasse.uf2, firmware\schranken.uf2
```
