# DeskConnect CYD (Dual-Display Messenger & Game Station)

Ein vernetztes Zwei-Display-System für das **ESP32 CYD (ESP32-2432S028)**, entwickelt mit nativem **ESP-IDF** und einem leichten Python-Backend.

---

## 🔌 Hardware: ESP32-2432S028 (Dual-USB)

Dein Board besitzt zwei Anschlüsse (1x USB-C, 1x Micro-USB):
* **Beide Anschlüsse (USB-C und Micro-USB)** können gleichermaßen für das **Flashen**, den **Serial Monitor** und die **Stromversorgung** verwendet werden.

---

## 📁 Projektstruktur

```
ESP_AI/
├── firmware/                   # ESP-IDF C-Projekt
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults      # Flash 4MB, 240MHz, FreeRTOS 1000Hz
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── idf_component.yml   # LVGL 9, esp_lcd_ili9341, esp_lcd_touch_xpt2046
│   │   ├── cyd_pins.h          # Exakte Pin-Definitionen für ESP32-2432S028
│   │   ├── cyd_bsp.h / .c      # Board Support Package (Display, Touch, Backlight PWM, RGB-LED, Audio)
│   │   └── main.c              # Startroutine, FreeRTOS Tasks & Test-UI
└── server/                     # Python FastAPI Server
    ├── app.py                  # WebSockets, Foto-Skalierung (320x240) & Home Assistant Webhook
    └── requirements.txt
```

---

## 📶 WLAN-Konfiguration & Web-Setup (Captive Portal)

Die Initialisierung der WLAN-Verbindung geschieht über einen integrierten Webserver:
1. **Automatischer Fallback:** Kann sich das Display nicht mit dem hinterlegten WLAN verbinden (oder sind noch keine Daten im Flash gespeichert), öffnet es automatisch einen eigenen Access Point (z. B. `DeskConnect-A1B2`).
2. **Manuell über Touchscreen:** Über den Button **"Setup AP"** oben rechts auf dem Display kann der Konfigurations-Hotspot jederzeit gestartet werden.
3. **Captive Portal:** 
   * Verbinde dich mit Smartphone oder Laptop mit dem Hotspot `DeskConnect-XXXX`.
   * Die Konfigurationsseite öffnet sich dank integriertem DNS-Server automatisch (oder manuell via `http://192.168.4.1`).
   * Hier können **WLAN-SSID**, **Passwort**, **Gerätename** (z. B. *Büro*), **Server-IP** und das **Partner-Display** eingetragen und dauerhaft im NVS gespeichert werden.

---

## 🚀 Firmware kompilieren & flashen (ESP-IDF)

Öffne die **ESP-IDF Eingabeaufforderung** (oder das ESP-IDF Terminal in VS Code):

```powershell
# 1. In das Firmware-Verzeichnis wechseln
cd firmware

# 2. Target auf ESP32 setzen (einmalig)
idf.py set-target esp32

# 3. Projekt kompilieren
idf.py build

# 4. Flashen & Serial Monitor starten (COM-Port anpassen, Micro-USB angeschlossen)
idf.py -p COM3 flash monitor
```

---

## 🌐 Server starten (für Foto-Upload & Home Assistant)

```powershell
cd server
pip install -r requirements.txt
python app.py
```
* **Smartphone / PC Web-App:** Öffne im Browser `http://<IP-des-Servers>:8000`
* Fotos werden automatisch auf **320×240 Pixel** skaliert und komprimiert.
