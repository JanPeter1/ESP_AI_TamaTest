# CYD Display and Touch Initialization

Known-working hardware setup for the ESP32-2432S028 (2.8-inch CYD, ILI9341 TFT and XPT2046 touch controller). The implementation is in `main/cyd_bsp.c`; GPIO assignments and display dimensions are in `main/cyd_pins.h`.

## ILI9341 Display

- Resolution in landscape: **320 × 240**; controller-native axes are 240 × 320.
- SPI host: `SPI2_HOST`.
- Pins: SCLK GPIO 14, MOSI GPIO 13, MISO GPIO 12, CS GPIO 15, D/C GPIO 2, backlight GPIO 21. Reset is not separately wired (`-1`); the driver uses software reset.
- SPI mode 0, pixel format RGB565, 20 MHz pixel clock, DMA enabled.
- Panel configuration: BGR element order and big-endian RGB565 transfer.
- Use the **ILI9341_2** vendor initialization sequence, passed with `ili9341_vendor_config_t`. The generic ESP-IDF default sequence did not initialize this panel correctly. The custom sequence is defined in `main/cyd_bsp.c` and matches the alternative sequence selected by AIStudio's `ILI9341_2_DRIVER` setup.
- Landscape transform: `esp_lcd_panel_swap_xy(panel, true)` followed by `esp_lcd_panel_mirror(panel, false, false)`. Keep both mirror flags false to avoid mirrored text.
- Turn the display on with `esp_lcd_panel_disp_on_off(panel, true)` after initialization and orientation setup.

## XPT2046 Touch

- SPI host: `SPI3_HOST`.
- Pins: SCLK GPIO 25, MOSI GPIO 32, MISO GPIO 39, CS GPIO 33, PENIRQ GPIO 36. Reset is not connected (`-1`).
- SPI mode 0 at 2 MHz; DMA enabled.
- Set `dc_gpio_num = GPIO_NUM_NC`: the XPT2046 has no D/C pin. Leaving this field at its zero default incorrectly assigns GPIO 0 and prevents reliable touch reads.
- The controller reports native portrait axes. Set `x_max = 240`, `y_max = 320`, `swap_xy = 1`, `mirror_x = 0`, and `mirror_y = 0` for the 320 × 240 landscape UI.
- In the LVGL input callback, call `esp_lcd_touch_read_data()` before `esp_lcd_touch_get_coordinates()`.

## Related Firmware Settings

- LVGL display color format is RGB565. The flush callback swaps RGB565 bytes for the SPI transfer and waits for the panel color-transfer completion callback before reusing the DMA buffer.
- The LVGL task stack is 8192 bytes. Keep DMA buffers in DMA-capable memory.
- Dark appearance is configured in `main/main.c`; keep the screen background opaque so its dark color covers the panel.

## Troubleshooting Notes

- Blue/noisy or partially rendered output with the generic ILI9341 initialization: restore the custom ILI9341_2 vendor sequence first.
- Mirrored text: check the display mirror flags; landscape here uses `false, false`.
- Touch completely inactive: verify `dc_gpio_num = GPIO_NUM_NC`, touch SPI pins/host, CS GPIO 33, and PENIRQ GPIO 36.
- Touch coordinates rotated or landing in the wrong locations: preserve the native 240 × 320 touch maxima and enable `swap_xy` for the landscape display.
