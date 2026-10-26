# 📟 DeskConnect CYD – Projektzusammenfassung

Umfassende Dokumentation des Dual-Display Kommunikations- und Spielesystems auf Basis des **ESP32 CYD (ESP32-2432S028)** mit nativem **ESP-IDF** und **Python FastAPI Backend**.

---

## 1. Projektübersicht & Konzept

Zwei unabhängige **ESP32 Cheap Yellow Displays (CYD)** stehen an unterschiedlichen Orten – entweder im selben Haus (z. B. Büro & Wohnzimmer) **oder an völlig unterschiedlichen Standorten über das Internet** (z. B. zwei verschiedene Haushalte, Arbeitsplatz & Zuhause).

### Wie finden sich die Displays standortübergreifend (ohne gleiches WLAN)?
* **Gemeinsamer Raum-Code (Pairing-PIN):** Beim Einrichten via Web-Portal wird auf beiden Displays derselbe Raum-Code eingetragen (z. B. `Familie-2026`).
* **Zentraler Cloud-Hub / MQTT-Broker:** Beide Displays stellen eine ausgehende Verbindung (*Outbound*) zu einem gemeinsamen Server oder Broker her (z. B. `broker.hivemq.com` oder ein via Cloudflare Tunnel erreichbarer FastAPI-Server).
* **Keine Portweiterleitung am Router:** Da es sich um ausgehende Verbindungen handelt, blockiert keine Firewall oder NAT die Verbindung. Die Displays finden sich weltweit automatisch.

### Kernfunktionen
* **Vorgefertigte Touch-Karten (Quick Status):** Großflächige Touch-Buttons (2x4 Raster) für intuitive 1-Fingertipp-Interaktion:
  - 🍕 **Hunger!** ("Ich habe Hunger! Gibt es Essen?") – Gelbe LED & Orange Akzent
  - ❤️ **Hab dich lieb** ("Ich mag dich sehr!") – Rote LED & Pink/Roter Akzent
  - 😊 **Alles gut** ("Mir geht es gut!") – Grüne LED & Grüner Akzent
  - 💤 **Schlafen** ("Ich gehe jetzt schlafen. Gute Nacht!") – Blaue LED & Lila/Blauer Akzent
  - ☕ **Kaffee?** ("Lust auf eine Kaffeepause?") – Gelbe LED & Gold/Brauner Akzent
  - 🛑 **Bitte Ruhe** ("Bin beschäftigt / im Call!") – Rote LED & Roter Akzent
  - 🏃 **Gleich da** ("Bin in 5 Minuten da!") – Blaue LED & Cyan Akzent
  - 🎉 **Feierabend** ("Geschafft für heute! Feierabend!") – Grüne LED & Goldener Akzent
* **Akustisches & optisches Feedback:** Bei eingehenden Nachrichten ertönt ein dezenter Chime-Sound (GPIO 26) und die integrierte RGB-LED leuchtet in der Farbe der Nachricht. Es öffnet sich eine zentrierte Pop-up-Karte mit Quick-Reply-Optionen.
* **WLAN-Web-Setup (Captive Portal):** Kein Hardcodieren von Passwörtern. Findet das Board kein WLAN (oder bei Klick auf *"Setup"*), öffnet es einen eigenen Hotspot (`DeskConnect-XXXX`) mit integriertem Webserver zur Eingabe von WLAN, Raum-Code und Serveradresse.
* **Foto-Sharing via Smartphone:** Über eine mobile Web-App können Fotos direkt vom Handy hochgeladen werden. Der Server schneidet sie automatisch auf 320×240 Pixel zu und leitet sie in den Ziel-Raum weiter.
* **Rundenbasierte Spiele (Multiplayer):** *Vier Gewinnt* (Connect Four) und *Schiffe versenken* (Battleship) direkt Display-gegen-Display.
* **Home Assistant Integration:** Webhook- & MQTT-Schnittstelle für Benachrichtigungen (z. B. *"Waschmaschine ist fertig"*).

---

## 2. Hardware: ESP32-2432S028 (Dual-USB)

Das Projekt ist exakt auf das Board **ESP32-2432S028** (mit 1x Micro-USB und 1x USB-C) abgestimmt.

> [!NOTE] USB-Anschlüsse (Dual-USB)
> Das Board besitzt zwei Anschlüsse (1x USB-C und 1x Micro-USB).
> **Beide Anschlüsse** können flexibel für die Stromversorgung, das **Flashen** und den **Serial Monitor** verwendet werden.

### Vollständige Pinbelegung

| Komponente | Pin / GPIO | ESP-IDF Subsystem / Bemerkung |
| :--- | :--- | :--- |
| **Display (ILI9341)** | | **SPI2 (HSPI)** – 320x240 Landscape, DMA |
| • MOSI | `GPIO 13` | SPI2 MOSI |
| • MISO | `GPIO 12` | SPI2 MISO |
| • SCLK | `GPIO 14` | SPI2 SCLK (40 MHz) |
| • CS | `GPIO 15` | Chip Select |
| • DC | `GPIO 2` | Data / Command |
| • RST | `-1` / EN | System-Reset |
| • Backlight | `GPIO 21` | LEDC PWM (5 kHz, stufenlos dimmbar) |
| **Touchscreen (XPT2046)**| | **SPI3 (VSPI)** – Resistiver Touch |
| • T_CLK | `GPIO 25` | Touch Clock (2 MHz) |
| • T_CS | `GPIO 33` | Touch Chip Select |
| • T_MOSI (DIN) | `GPIO 32` | Touch Data In |
| • T_MISO (DOUT) | `GPIO 39` | Touch Data Out (Input only) |
| • T_IRQ (PEN) | `GPIO 36` | Touch Interrupt (Input only) |
| **On-Board Peripherie** | | |
| • RGB-LED Rot | `GPIO 4` | Active LOW (0 = AN, 1 = AUS) |
| • RGB-LED Grün | `GPIO 16` | Active LOW |
| • RGB-LED Blau | `GPIO 17` | Active LOW |
| • Lautsprecher / Buzzer | `GPIO 26` | LEDC PWM (Tonerzeugung / Melodien) |
| • Lichtsensor (LDR) | `GPIO 34` | ADC1 Channel 6 (Umgebungshelligkeit) |
| • MicroSD Slot | `GPIO 5, 23, 19, 18` | Standard SPI-Pins |

---

## 3. Software-Architektur

### 3.1 Firmware (ESP-IDF v5)

```
firmware/
├── CMakeLists.txt              # ESP-IDF Root CMake
├── sdkconfig.defaults          # Flash 4MB, 240MHz CPU, FreeRTOS 1000Hz, Stack-Größen
└── main/
    ├── CMakeLists.txt          # Komponenten-Registrierung
    ├── idf_component.yml       # LVGL 9, esp_lcd_ili9341, esp_lcd_touch_xpt2046
    ├── cyd_pins.h              # Zentrale Hardware-Pin-Definitionen
    ├── cyd_bsp.h / .c          # Board Support Package (Treiber für Display, Touch, Backlight, LED, Sound, LDR)
    ├── cyd_config.h / .c       # Persistente NVS-Flash-Speicherung (SSID, Pass, Device-ID, Server-IP)
    ├── cyd_wifi.h / .c         # WLAN STA + Fallback-SoftAP, HTTP-Webserver & Captive Portal DNS
    └── main.c                  # Startroutine, FreeRTOS Tasks & Touch-UI mit Status- und Einstellungsfenster
```

#### Funktionsweise des WLAN-Setups:
1. Beim Start liest `cyd_config_load()` die Zugangsdaten aus dem NVS.
2. Wenn Zugangsdaten vorhanden sind, versucht `cyd_wifi_connect_sta()` die Verbindung aufzubauen.
3. Schlägt die Verbindung nach 5 Versuchen fehl **oder** klickt der Nutzer auf dem Display auf **"Setup AP"**:
   * Der ESP32 startet einen eigenen Hotspot: `DeskConnect-XXXX` (Passwortfrei).
   * Der integrierte Webserver (`esp_http_server`) startet auf Port 80.
   * Ein DNS-Server-Task auf UDP-Port 53 leitet alle Anfragen auf `192.168.4.1` um (Captive Portal: Smartphone öffnet die Seite automatisch).
   * Nach dem Speichern wird die Konfiguration ins NVS geschrieben und das Display startet automatisch neu.

---

### 3.2 Zentraler Server (Python FastAPI)

```
server/
├── app.py                      # FastAPI App, WebSockets, Foto-Skalierung, HA-Webhook
└── requirements.txt            # fastapi, uvicorn, pillow, websockets, python-multipart
```

#### Aufgaben des Servers:
* **Web-Oberfläche (`/`):** Schlanke mobile Seite für Smartphones ohne App-Installation.
* **Foto-Skalierung (`/api/upload`):** Nutzt Pillow (`ImageOps.fit`) für exakten Lanczos-Zuschnitt auf 320×240 Pixel und komprimiert in ein für den ESP32 leicht decodierbares JPEG.
* **Echtzeit-Routing (`/ws/{client_id}`):** WebSockets für die direkte Übertragung von Chat-Nachrichten, Status-Updates und Spielzügen.
* **Home Assistant Schnittstelle (`/api/notify`):** Webhook-Endpunkt für Meldungen aus Hausautomation-Systemen.

---

## 4. Schnellstart-Anleitung

### Schritt 1: Firmware kompilieren & flashen

1. Verbinde das Display über **USB-C oder Micro-USB** mit dem PC.
2. Öffne die **ESP-IDF Eingabeaufforderung**:
   ```powershell
   cd firmware
   idf.py set-target esp32
   idf.py build
   idf.py -p COMx flash monitor   # COMx durch deinen Port ersetzen
   ```

### Schritt 2: WLAN konfigurieren

1. Nach dem Booten ertönt ein Begrüßungston und das Display zeigt das Touch-Menü.
2. Falls noch kein WLAN hinterlegt ist, wechselt das Display automatisch in den Modus:
   `AP: DeskConnect-XXXX (192.168.4.1)`.
   *(Alternativ: Klicke oben rechts auf den Button **"Setup AP"**).*
3. Verbinde dein Smartphone mit dem Hotspot `DeskConnect-XXXX`.
4. Die Konfigurationsseite öffnet sich automatisch (oder im Browser `http://192.168.4.1` aufrufen).
5. Trage deine WLAN-Daten, den Namen des Displays (z. B. `Buero`) und die Server-IP ein und klicke auf **Speichern**.

### Schritt 3: Kostenlosen Cloud-Server bereitstellen (100% Free Hosting)

Damit kein eigener PC oder Raspberry Pi rund um die Uhr laufen muss, kann das Backend auf einem kostenlosen Cloud-Host betrieben werden:

#### Option A: Render.com (Empfohlen – 100% kostenlos, inkl. HTTPS & WebSockets)
1. Erstelle ein kostenloses Konto auf [Render.com](https://render.com).
2. Lade den Ordner `server/` (oder das gesamte Repository) auf GitHub hoch.
3. Klicke auf Render auf **"New +" -> "Web Service"** und wähle dein GitHub-Repository aus.
4. Render erkennt die bereits hinterlegte `render.yaml` bzw. `Dockerfile` automatisch.
5. Du erhältst sofort eine kostenlose, verschlüsselte HTTPS-URL:
   `https://deskconnect-hub-xxxx.onrender.com`
6. Diese Adresse trägst du einmalig im Web-Setup der Displays ein (Port `443`). Fertig!

#### Option B: Lokaler Start (Entwicklung / Test)
```powershell
cd server
pip install -r requirements.txt
python app.py
```
* Lokal erreichbar unter: `http://localhost:8000`

---

## 5. Roadmap & Nächste Ausbaustufen

- [x] Hardware-Treiber (Display, Touch, Backlight-PWM, RGB-LED, Audio, LDR)
- [x] ESP-IDF v5 CMake- und Komponenten-Setup (LVGL 9, esp_lcd)
- [x] WLAN Captive Portal & Web-Setup mit NVS-Flash-Speicherung
- [x] Touch-UI mit Statusleiste, Einstellungs-Modal und Hardware-Tests
- [x] FastAPI Server-Grundgerüst (Foto-Skalierung auf 320x240, WebSockets, Webhooks)
- [ ] ESP-IDF WebSocket/MQTT-Client für Empfang von Servernachrichten & Fotos
- [ ] Multiplayer-Minigames (Vier Gewinnt & Schiffe versenken)
- [ ] Home Assistant Auto-Discovery / Dashboard-Widgets
