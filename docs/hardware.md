# Hardware and validation matrix

This matrix distinguishes the observed boards from build compatibility. Check the exact board, wiring, flash size and electrical limits before use; a successful build is not a hardware test.

| Feature | ESP32 Classic, tested board | ESP32-S3-N16R8, tested board |
| --- | --- | --- |
| Flash / PSRAM | 4 MB flash; no PSRAM required | 16 MiB flash; 8 MiB physical Octal PSRAM, disabled in the non-PSRAM profile and enabled at 80 MHz in the explicit PSRAM profile |
| Lua, LAB, UserFS, Program Manager | Tested | Tested |
| Wi-Fi | AP and STA tested; Web Settings scan and browser configuration confirmed | AP and STA tested; LAB and Settings accessible |
| Console | UART0 | UART0 through the board's COM connector; native USB Serial/JTAG also present |
| External GPIO / I²C | Tested on the Classic setup | Builds and host tests; external circuits and peripherals not yet validated on hardware |
| BME280 / SSD1306 | BME280 `0x76`, SSD1306 128×32 `0x3C`; SDA21, SCL22 tested | Not tested with external devices |
| RGB | No onboard RGB indicator supported | One WS2812-compatible GRB LED, GPIO48, only with the verified board profile |

The generic S3 profile does not enable RGB. On the verified profile GPIO48 is reserved, and system colors use 4/255 per active channel: READY green, RUNNING blue, ERROR red. No additional task is introduced. The optional `zam.rgb.set(r, g, b)` API is present only when the indicator is available; the single Welcome Sample prints on Classic and animates it on the verified S3. See [Lua runtime](lua-runtime.md#rgb-indicator-on-the-verified-s3-board).

The partition layout uses only the first 4 MiB on both boards; the remaining S3 flash is not allocated. The explicit PSRAM profile and integrated Lua modules were exercised on this S3; the integrated campaigns lacked the UART boot log and non-external-allocation counter. The S3 tests do not establish compatibility with other board layouts, bootloaders or LEDs. SSD1306 128×64 was not physically tested on the Classic setup. STA persistence, re-enabling and recovery after incorrect credentials have not been completely documented; the earlier STA-disable discrepancy was not confirmed as a defect. The editor was checked on the S3 in Firefox and visually through AP; mobile layout was simulated. See [storage](storage.md), [Wi-Fi Settings](wifi-web-settings.md), [GPIO](lua-gpio.md), [I²C](lua-i2c.md) and [Web UI](web-ui.md).
