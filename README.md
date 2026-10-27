# DeskConnect CYD

Firmware für das ESP32-2432S028 („Cheap Yellow Display“) mit 2,8-Zoll-ILI9341-TFT und XPT2046-Touch. Das aktuelle ESP-IDF-Projekt bietet eine dunkle Querformat-Oberfläche mit Status-Buttons, lokaler LED-/Audio-Rückmeldung sowie einen WLAN-Konfigurationszugangspunkt. Der Python-Server ist ein separater Prototyp; die Firmware sendet derzeit noch keine Button-Ereignisse an andere Displays.

## Projektstruktur

- `firmware/main/`: Anwendung, CYD-Treiber, WLAN-Setup, Konfiguration und Status-Presets.
- `firmware/components/esp_lcd_ili9341/`: ILI9341-Treiber samt eigener Initialisierungssequenz.
- `firmware/HARDWARE_DISPLAY_TOUCH.md`: dokumentierte Display- und Touch-Einstellungen.
- `server/`: eigenständiger FastAPI-Prototyp mit Browseroberfläche, HTTP-Endpunkten und WebSocket-Raumweiterleitung.
- `README.md` und `PROJEKT_ZUSAMMENFASSUNG.md`: Einstieg und Funktionsübersicht.

## Firmware bauen und flashen

Voraussetzung ist ESP-IDF 6.1 mit installiertem ESP32-Target. In einer ESP-IDF-PowerShell:

```powershell
cd firmware
idf.py set-target esp32  # nur beim ersten Einrichten des Build-Verzeichnisses
idf.py build
idf.py -p COM4 flash monitor  # COM4 durch den verwendeten Port ersetzen
```

Die Display- und Touch-Pins, Orientierung und Controller-Initialisierung sind in `firmware/HARDWARE_DISPLAY_TOUCH.md` beschrieben. Die Konfiguration wird im NVS gespeichert. Ohne gültige WLAN-Daten startet das Gerät einen `DeskConnect-XXXX`-Access-Point; danach die Setup-Seite unter `http://192.168.4.1` öffnen.

## Python-Server starten

```powershell
cd server
python -m pip install -r requirements.txt
python app.py
```

Der Server lauscht standardmäßig auf Port 8000. Seine Nachrichten- und Foto-Endpunkte bedienen verbundene WebSocket-Clients im Arbeitsspeicher. Die Firmware enthält aktuell noch keinen passenden WebSocket-Client; ein gemeinsames Zwei-Display-System ist daher noch nicht fertig integriert.

## Entwicklung

C-Code folgt dem Stil der bestehenden ESP-IDF-Dateien (vier Leerzeichen, `snake_case`, Makros in Großbuchstaben). Python verwendet vier Leerzeichen und `snake_case`. Es gibt derzeit keine automatisierte Testsuite. Firmware-Änderungen mit `idf.py build` prüfen und hardwarebezogene Änderungen nach Möglichkeit am Board kontrollieren.
