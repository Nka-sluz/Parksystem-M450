# HTTP-Kommunikationsprotokoll Pico 2 → Pico 1

Pico 1 („Kasse", `192.168.42.1`) ist die Leitstelle: Er führt Register und
Protokoll und trifft **jede** Entscheidung. Pico 2 („Schranken",
`192.168.42.2`) fragt bei jeder Ein-/Ausfahrt nach und öffnet die Schranke
nur bei positiver Antwort.

Die Konstanten (Pfade, Feldnamen) stehen in `lib/ParkCore/src/Protocol.h`.

## Allgemein

| | |
|---|---|
| Transport | HTTP/1.1 über das WLAN `Parkhaus-M450` (kein Internet nötig) |
| Anfrage | `POST` mit `application/x-www-form-urlencoded` (nur `/api/status` ist `GET`) |
| Antwort | `text/plain; charset=utf-8`, ein Feld pro Zeile: `key=value` |
| Fachliche Ablehnungen | HTTP **200** mit `result=DENIED` bzw. `result=ERROR` und `reason=…` |
| Formale Fehler | HTTP **400** (`result=ERROR`, `reason=MISSING_PLATE`), unbekannter Pfad **404** |
| Timeout / Wiederholung | pro Versuch `KASSE_HTTP_TIMEOUT_MS` = 1000 ms, bis zu `KASSE_HTTP_ATTEMPTS` = 3 Versuche im Abstand von 150 ms (`/api/status`: 1 Versuch) |
| Request-ID | Parameter `rid` bei allen `POST`-Anfragen, z. B. `rid=p2-1a2b3c4d-17` |
| Verbindung | Jede Antwort schliesst die TCP-Verbindung sofort (`Connection: close`) |
| Beträge | ganzzahlig in **Rappen** (`due=350` = CHF 3.50) |

**Wiederholungen ohne Doppelbuchung:** Geht eine Antwort verloren, wiederholt
Pico 2 die Anfrage mit **derselben** `rid`. Pico 1 merkt sich die Antworten
der letzten 8 Request-IDs und sendet bei einer bekannten `rid` die
gespeicherte Antwort erneut, ohne die Aktion nochmals auszuführen. Eine
wiederholte Einfahrt liefert also wieder `GRANTED` (nicht `DUPLICATE`), und
eine Ablehnung wird nur einmal protokolliert.

**Fail-safe:** Kommt nach allen Versuchen keine gültige Antwort (WLAN
getrennt, Timeout, Pico 1 aus), behandelt Pico 2 das wie eine Ablehnung: Die
Schranke bleibt zu und die Kamera-Simulation zeigt „Keine Verbindung zur
Kasse". Eine Antwort gilt nur als gültig, wenn sie das Feld `result` enthält.

**Warum die Verbindung sofort geschlossen wird:** Der WebServer des Pico
bedient nur eine Verbindung gleichzeitig und würde nach jeder Antwort bis zu
5 s warten, bis der Browser die Verbindung schliesst. In dieser Zeit wären
alle anderen Anfragen blockiert, auch die von Pico 2.

### Reason-Tokens

| Token | Bedeutung |
|---|---|
| `NONE` | kein Grund (Erfolg) |
| `INVALID` | Kennzeichen ungültig (leer, falsches Format, zu lang, reservierte Ersatz-ID an der Einfahrt) |
| `DUPLICATE` | Kennzeichen bereits im Parkhaus |
| `FULL` | Parkhaus voll |
| `UNPAID` | offener Betrag > 0 (Betrag in `due`) |
| `UNKNOWN` | Kennzeichen nicht im Register |
| `EXITED` | Kennzeichen bereits ausgefahren |

## Endpunkte

### `POST /api/entry` – Einfahrt anfragen (US1, US2, US8)

| Parameter | Pflicht | Beschreibung |
|---|---|---|
| `plate` | ja* | erfasstes Kennzeichen (Rohtext) |
| `manual` | – | `1` = manuelle Abfertigung; dann wird `plate` ignoriert und eine Ersatz-ID `MANUAL-<n>` vergeben |
| `rid` | empfohlen | Request-ID (gilt für alle `POST`-Endpunkte) |

\* entweder `plate` oder `manual=1`

Bei `GRANTED` ist das Fahrzeug **sofort registriert** (Status „im Parkhaus,
unbezahlt", freie Plätze −1) und die Einfahrt protokolliert. Jede Ablehnung
wird mit Grund protokolliert.

```
result=GRANTED          | DENIED
reason=NONE             | INVALID | DUPLICATE | FULL
id=ZH-100001            (normalisiertes Kennzeichen bzw. MANUAL-<n>)
free=2
```

### `POST /api/exit/check` – Ausfahrt anfragen (US5, US8)

| Parameter | Pflicht | Beschreibung |
|---|---|---|
| `plate` | ja | Kennzeichen oder Ersatz-ID |
| `manual` | – | `1` = manuelle Abfertigung (wird im Protokoll gekennzeichnet) |

`OPEN` nur, wenn der offene Betrag 0 ist (bezahlt und Karenzzeit nicht
überschritten, oder Parkdauer unter der Freigrenze). Verweigerungen werden
protokolliert; die Freigabe selbst wird erst mit `/api/exit/confirm`
protokolliert.

```
result=OPEN             | DENIED
reason=NONE             | UNPAID | UNKNOWN | EXITED | INVALID
id=ZH-100001
due=0                   (offener Betrag in Rappen)
free=2
```

### `POST /api/exit/confirm` – Durchfahrt an der Ausfahrt bestätigt

Parameter wie `/api/exit/check`. Setzt den Registereintrag auf „ausgefahren",
erhöht die freien Plätze und protokolliert die Ausfahrt. Ein zweiter Aufruf
für dasselbe Kennzeichen liefert `result=ERROR`, `reason=EXITED` und ändert
nichts (Datenkonsistenz).

```
result=OK               | ERROR
reason=NONE             | EXITED | UNKNOWN | INVALID
free=3
```

Ist Pico 1 beim Bestätigen nicht erreichbar, hat das Fahrzeug die Schranke
bereits passiert. Pico 2 merkt sich die Meldung und wiederholt sie alle 5 s,
bis Pico 1 sie quittiert.

### `POST /api/exit/abort` – freigegebene Ausfahrt ohne Durchfahrt (Timeout)

Parameter wie `/api/exit/check`. Nur Protokolleintrag
(`AUSFAHRT_ABGEBROCHEN`); das Fahrzeug bleibt „im Parkhaus".

```
result=OK
```

### `GET /api/status` – Belegung (US6)

Wird von Pico 2 alle 5 s abgefragt (Anzeige „freie Plätze" und
Verbindungsstatus).

```
result=OK
capacity=3
occupied=1
free=2
```

## Ablauf

```
Bediener (Kamera-Sim.)       Pico 2 Schranken              Pico 1 Kasse
        | Kennzeichen erfassen  |                              |
        |---------------------->| POST /api/entry plate=…      |
        |                       |----------------------------->| prüfen, registrieren, protokollieren
        |                       |<-----------------------------| result=GRANTED
        |                       | Servo auf, LED an (< 2 s)    |
        | Durchfahrt bestätigen |                              |
        |---------------------->| Servo zu, LED aus            |
        |                       |                              |
        |       … Gast bezahlt an der Weboberfläche Kasse …    |
        |                       |                              |
        | Kennzeichen erfassen  | POST /api/exit/check         |
        |---------------------->|----------------------------->| offener Betrag = 0?
        |                       |<-----------------------------| result=OPEN
        |                       | Servo auf, LED an            |
        | Durchfahrt bestätigen | POST /api/exit/confirm       |
        |---------------------->|----------------------------->| "ausgefahren", Platz frei, protokollieren
```

## Weitere Seiten (Browser)

| Pico | Pfad | Inhalt |
|---|---|---|
| 1 | `/` | Kasse/Zahlautomat: Gebühr anzeigen, bezahlen, Belegung, Register |
| 1 | `GET /ui/state` | JSON: Belegung, Umsatz, Tarif, Fahrzeugregister (Seite fragt alle 3 s ab) |
| 1 | `POST /ui/fee` | JSON: Gebühr zu `plate` |
| 1 | `POST /ui/pay` | JSON: Bezahlen (`plate`, optional `amount`, `rid` gegen Doppelzahlung bei Wiederholung) |
| 1 | `/log` | Protokoll als Tabelle und CSV |
| 1 | `/log.csv` | Protokoll als CSV-Datei (Header `X-Log-Overwritten` = Anzahl überschriebener Einträge) |
| 2 | `/` | Kamera-Simulation: Einfahrt, Ausfahrt, manuelle Abfertigung, Durchfahrt bestätigen, Schranken-Test |
| 2 | `POST /api/action` | JSON: Aktion `action` = `entry`, `entryManual`, `entryPass`, `exit`, `exitManual`, `exitPass` oder `servoTest`, dazu `plate` und `rid` |
| 2 | `GET /state` | JSON-Zustand beider Schranken (Seite fragt jede Sekunde ab) |

Die Weboberflächen senden Aktionen im Hintergrund (`fetch`) mit Timeout und
bis zu 3 Versuchen. Auch hier trägt jede Aktion eine `rid`, die bei
Wiederholungen gleich bleibt, damit nichts doppelt ausgeführt wird.
