# DeskConnect CYD

DeskConnect CYD verbindet zwei ESP32-2432S028-Displays über einen gemeinsamen Raumserver. Die Firmware zeigt neue Nachrichten mit Pixel-Avatar und Emotionssymbol auf einem dunklen Tamagotchi-inspirierten Startbildschirm. Per Wischgeste wechselt man zur Sendeseite mit thematischen Kategorien und Nachrichtentasten.

## Projektstruktur

- `firmware/main/`: ESP-IDF-Anwendung, Display und Touch, WLAN-Setup, Raumkommunikation und eingebettete Avatare.
- `firmware/main/assets/`: CC0-Quellbilder, Lizenzhinweise und Generator für LVGL-Grafiken.
- `firmware/components/esp_lcd_ili9341/`: ILI9341-Treiber mit der für dieses Board verwendeten Initialisierung.
- `firmware/HARDWARE_DISPLAY_TOUCH.md`: Pins, Displayorientierung, Touchkalibrierung und Initialisierung.
- `server/app.py`: FastAPI-Raumserver und HTTP-/WebSocket-Schnittstellen.
- `server/index.html`: Browseroberfläche zum Senden und Beobachten von Raumnachrichten.
- `server/INTERFACE.md`: Nachrichtenformat, Endpunkte und Betriebsgrenzen.
- `PROJEKT_ZUSAMMENFASSUNG.md`: Architektur, Funktionen und nächste Entwicklungsschritte.

## Firmware bauen und flashen

Voraussetzung: ESP-IDF 6.1 mit ESP32-Target. In der ESP-IDF-PowerShell:

```powershell
cd firmware
idf.py set-target esp32  # einmalig pro Build-Verzeichnis
idf.py build
idf.py -p COM4 flash     # COM-Port des angeschlossenen Displays einsetzen
```

Die Geräteeinstellungen liegen im NVS. Der Setup-Button öffnet einen WLAN-Access-Point; verbinde dich mit dem angezeigten `DeskConnect-XXXX`-Netz und rufe `http://192.168.4.1` auf. Trage WLAN, Server-Host (nur IP/Hostname), Port und denselben Raumcode auf allen Displays ein. Für den lokalen Server ist das meist Port `8000`. PC und Displays müssen sich gegenseitig im LAN erreichen können; Gast-WLANs blockieren diesen Zugriff häufig.

## Server starten

```powershell
cd server
python -m pip install -r requirements.txt
python app.py
```

Der Server lauscht auf Port 8000 (oder dem Wert von `PORT`) an allen Netzwerkschnittstellen. Das Webinterface ist unter `http://<server-ip>:8000/` erreichbar. Nachrichten und Teilnehmerlisten liegen derzeit nur im Arbeitsspeicher; den Prototyp mit einem Uvicorn-Worker betreiben. Öffentlicher Betrieb benötigt Authentifizierung und dauerhafte Speicherung.

## Stil und Entwicklung

C folgt dem bestehenden ESP-IDF-Stil: vier Leerzeichen, `snake_case` und Großbuchstaben für Makros. Python verwendet vier Leerzeichen und `snake_case`. Für Firmwareänderungen `idf.py build` ausführen; Hardwareverhalten anschließend am Display prüfen. Eine automatisierte Testsuite ist nicht eingerichtet.
