# DeskConnect CYD – Projektübersicht

## Zweck und aktueller Stand

DeskConnect CYD ist ein ESP-IDF-Projekt für ein ESP32-2432S028 mit ILI9341-Farbdisplay und XPT2046-Touch. Die Firmware startet eine LVGL-Oberfläche im Querformat, zeigt acht vorgefertigte Status-Buttons und gibt bei lokalem Tastendruck Rückmeldung über Bildschirm, RGB-LED und Summer. Die Anzeige verwendet eine dunkle Gestaltung und mehrzeilige Button-Titel, damit längere Beschriftungen in die Kacheln passen.

Ein zweiter CYD kann derzeit nicht automatisch Nachrichten von diesem Gerät empfangen. Die Button-Ereignisse werden lokal verarbeitet. Der Server unter `server/` ist ein eigenständiger FastAPI-Prototyp: Er bietet eine Browserseite, `/api/notify`, `/api/upload` und WebSockets unter `/ws/{room}/{client_id}`. Verbindungen und Räume liegen nur im Arbeitsspeicher des Serverprozesses. Ein WebSocket-Client und Nachrichtenprotokoll sind in der aktuellen ESP-IDF-Firmware noch nicht integriert. Foto-Sharing, Spiele und Home-Assistant-Anbindung sind somit keine fertigen Gerätefunktionen.

## WLAN und Einrichtung

Die Firmware speichert Zugangsdaten und Geräteparameter im NVS. Wenn eine Verbindung zum konfigurierten WLAN nicht verfügbar ist, stellt das Gerät einen `DeskConnect-XXXX`-Access-Point mit Setup-Webseite bereit. Das Gerät kann WLAN-SSID und Kennwort, Gerätename, Serveradresse/-port und Raumkennung speichern. Diese Angaben konfigurieren den Setup-Datensatz, sie aktivieren aber noch keine Serververbindung in der Firmware.

## Board und Treiber

- Display: ILI9341, 320 × 240 im Querformat, SPI2, RGB565, 20 MHz.
- Touch: XPT2046 an SPI3.
- Die bekannte funktionierende Pinbelegung, Achsentransformation und Vendor-Initialisierung stehen in [`firmware/HARDWARE_DISPLAY_TOUCH.md`](firmware/HARDWARE_DISPLAY_TOUCH.md).
- Die RGB-LED ist active-low. Die Presets kodieren LED-Farben als RGB-Bitmaske.

## Bauen und ausführen

Mit ESP-IDF 6.1 in PowerShell aus `firmware/`:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM4 flash monitor
```

Den seriellen Port entsprechend dem angeschlossenen Board anpassen. Für den Python-Prototyp in `server/` `python -m pip install -r requirements.txt` und anschließend `python app.py` ausführen; standardmäßig lauscht er auf Port 8000.

## Geplante Erweiterungen

Die nächsten Integrationsschritte sind ein abgesichertes Nachrichtenprotokoll zwischen Firmware und Server, zuverlässige Zustellung zwischen Geräten und anschließend die Umsetzung zusätzlicher Funktionen wie Fotoanzeige, Spiele oder Home-Assistant-Ereignisse. Bis dahin die Serverfunktionen als Prototyp und nicht als bereits verfügbare Display-Funktionen behandeln.
