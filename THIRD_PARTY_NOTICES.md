# Third-party notices and dependencies

The MIT license in `LICENSE` applies only to original project material for which the owner holds the necessary rights. Other material retains its own license.

## Included source and assets

The project code, generic examples and web assets are supplied as source. The project owner confirmed rights to distribute the included logo and banner. No third-party Lua modules or fonts are intentionally bundled; provenance of unidentified adapted fragments remains subject to review. The license texts in `third_party_licenses/` are included for reference and retain their original attribution.

## External build dependencies (not bundled)

| Dependency | Version and origin | Copyright and license | Redistribution notes |
| --- | --- | --- | --- |
| Lua | 5.5.1, [Lua.org](https://www.lua.org/license.html) | Lua.org, PUC-Rio, 1994–2026; MIT | Preserve copyright and license text when distributing Lua. See `third_party_licenses/Lua-MIT.txt`. |
| ESP-IDF | 5.5.3, [Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/COPYRIGHT.html) | Espressif and component contributors; principally Apache-2.0, with component-specific terms | Retain applicable notices and license texts for materials actually redistributed; check component-level terms. |
| FreeRTOS kernel | 10.5.1, as supplied by ESP-IDF | Amazon.com, Inc. or affiliates and contributors; MIT | Retain copyright and license notice when redistributing covered source or binaries. See `third_party_licenses/FreeRTOS-MIT.txt` and upstream source headers. |
| ESP LittleFS wrapper | 1.20.4, [joltwallet/esp_littlefs](https://github.com/joltwallet/esp_littlefs) | Brian Pugh, 2020; MIT | Retain copyright and license notice. See `third_party_licenses/LittleFS-wrapper-MIT.txt`. |
| littlefs | API version 2.11 as supplied by wrapper, [littlefs-project](https://github.com/littlefs-project/littlefs) | littlefs authors and Arm Limited; BSD-3-Clause | Retain copyright, conditions and disclaimer as applicable. See `third_party_licenses/littlefs-BSD.txt`. |
| Toolchain and other IDF dependencies | Obtained separately with ESP-IDF | Multiple copyright holders and licenses | Check the exact artifacts and their terms before any binary redistribution. |

The additional texts in `third_party_licenses/` are reference copies, not evidence that the corresponding source or binaries are bundled. Their inclusion does not replace the obligation to check applicable notices for actual redistributed dependencies.

## Excluded and binary distribution

BME280 and SSD1306 Lua drivers, their dependent Samples and Spleen glyphs are excluded. Precompiled ESP-IDF libraries, Xtensa HAL archives, toolchains and firmware are not in this source distribution. The licensing of flash-linked `libxt_hal.a` archives remains unresolved for binary redistribution; source-only preparation does not resolve it.
