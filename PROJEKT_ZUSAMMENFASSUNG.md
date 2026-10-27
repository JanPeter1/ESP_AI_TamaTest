# DeskConnect CYD – Projektübersicht

## Zweck und Funktionsumfang

DeskConnect CYD ist ein ESP-IDF-Projekt für zwei ESP32-2432S028-Displays mit ILI9341-TFT und XPT2046-Touch. Ein dunkler Querformat-Startbildschirm zeigt die letzte Raumnachricht mit Pixel-Avatar und einfacher Emotionsdarstellung. Wischen öffnet die Sendeseite mit vier Themenkategorien und acht vorbereiteten Nachrichten. Buttons geben zusätzlich LED- und Audiofeedback.

Die Geräte verbinden sich nach dem WLAN-Setup per WebSocket mit demselben Raumserver. Der Server stellt eine Browseroberfläche, HTTP-Nachrichtenendpunkte und `/ws/{room}/{client_id}` bereit. Das gemeinsame JSON-Protokoll ist in [server/INTERFACE.md](server/INTERFACE.md) beschrieben. Die Firmware-Avatare sind Katze und Hund aus einem CC0-Asset; Quelle und Generator stehen unter `firmware/main/assets/`.

## Einrichtung und Netzwerk

WLAN-Zugangsdaten und Serverparameter werden im NVS gespeichert. Der Setup-Button öffnet einen `DeskConnect-XXXX`-Access-Point mit Portal unter `http://192.168.4.1`; beim Drücken wird der Button rot, und bei aktivem Setup-AP bleibt er rot. Auf allen Displays müssen Server-Host, Port und Raumcode übereinstimmen.

Für einen lokalen Server muss die im Display eingetragene Server-IP die LAN-IP des PCs sein, üblicherweise Port 8000. Der Server bindet an alle Netzwerkschnittstellen. Gast-WLAN, Client-Isolation oder die Windows-Firewall können Verbindungen zwischen Display und PC verhindern. Der Server behält maximal acht Nachrichten pro Raum während der Prozesslaufzeit im RAM. Er hat derzeit keine Anmeldung, persistente Ablage oder geteilte Datenbank; nur einen Serverprozess verwenden.

## Hardware und Build

- Display: ILI9341, 320 × 240, Querformat, SPI2, RGB565, 20 MHz.
- Touch: XPT2046 über SPI3.
- Pinbelegung und funktionierende Controller-Initialisierung: [firmware/HARDWARE_DISPLAY_TOUCH.md](firmware/HARDWARE_DISPLAY_TOUCH.md).
- Die RGB-LED ist active-low; Presets teilen sich dieselbe RGB-Bitmaske.
- Firmware bauen: mit ESP-IDF 6.1 aus `firmware/` `idf.py build` ausführen.
- Server starten: aus `server/` `python -m pip install -r requirements.txt`, danach `python app.py`. Die Oberfläche läuft auf Port 8000 beziehungsweise dem Wert von `PORT`.

## Nächste Schritte

Mögliche Erweiterungen sind Authentifizierung, persistente Nachrichten, Wiederzustellung nach Offlinezeiten und konfigurierbare Avatar-Sets. Nachrichten übertragen aktuell Text und eine Avatar-ID, keine Bilddateien. Fotoübertragung, Spiele und Home-Assistant-Anbindung gehören nicht zum implementierten Gerätefluss.
