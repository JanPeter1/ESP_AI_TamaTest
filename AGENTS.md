# Repository Guidelines

## Project Structure & Module Organization

- `firmware/` is the ESP-IDF 6.1 application. App, room messaging and LVGL assets are in `firmware/main/`; the ILI9341 driver is in `firmware/components/esp_lcd_ili9341/`.
- `firmware/main/assets/README.md` records avatar provenance and CC0 terms. `firmware/HARDWARE_DISPLAY_TOUCH.md` records the working CYD setup.
- `server/` contains the FastAPI room relay and `index.html` browser client. Keep its WebSocket and HTTP contract in sync with `server/INTERFACE.md` and the firmware client.
- `README.md` is the quick start; `PROJEKT_ZUSAMMENFASSUNG.md` records implemented features and roadmap.

## Build and Development Commands

Use ESP-IDF 6.1 PowerShell with the `esp32` target, from `firmware/`:

```powershell
idf.py set-target esp32       # Initial target configuration
idf.py build                  # Compile the firmware
idf.py -p COM4 flash monitor  # Flash and open serial monitor; adjust COM port
```

Run the server prototype from `server/` with `python -m pip install -r requirements.txt` and `python app.py` (port 8000 by default).

## Coding Style

Match neighboring code: C uses four spaces, same-line braces, `snake_case` functions/variables, and uppercase macros. Keep CYD pins and hardware setup in the existing board files. Python uses four spaces and `snake_case`. No formatter or linter is configured.

## Testing

There is no automated test suite or coverage requirement. Build firmware with `idf.py build`; for hardware changes, verify the affected display, touch, or Wi-Fi behavior on a board when available. For server changes, start the service and check the changed route manually.

## Commits and Pull Requests

The Git history has no established convention beyond the initial commit. Use a short imperative subject; scoped Conventional Commits such as `fix(firmware): wrap preset button labels` are appropriate. Pull requests should explain the user-visible change, validation performed, hardware or configuration requirements, and include screenshots or serial output for UI/device changes when useful.

## Security and Configuration

Never commit Wi-Fi credentials, tokens, or deployment secrets. Device settings belong in the provisioning flow or local configuration; deployment secrets belong in the hosting provider's secret store.
