# Architecture

ZamproLab shares one application codebase across ESP32 Classic and ESP32-S3. ESP-IDF provides hardware and network services; FreeRTOS runs the service tasks. Both Classic and one S3-N16R8 board have been tested, within the limits in the [hardware matrix](hardware.md). The verified S3 uses a 16 MB flash configuration and leaves its physical PSRAM disabled; external S3 GPIO and I²C peripherals remain unvalidated.

## Firmware and services

`zam_console` is the sole UART0 reader and provides an administrative console with NORMAL and SECRET input. `zam_config` stores configuration in NVS; `zam_system` provides system information. `zam_wifi` manages AP, STA and AP+STA using a worker and queues. The persistent `auto` policy selects AP without STA credentials and AP+STA when credentials are present; fallback state is distinct from persistent configuration. See [console](console.md) and [Wi-Fi](wifi.md).

`zam_http` starts one ESP-IDF HTTP server on the available interfaces. It serves diagnostic APIs, static LAB assets, UserFS File Manager APIs and, when Lua is enabled, Program Manager RUN/STOP/status/output APIs. It starts from `HTTPD_DEFAULT_CONFIG()` and configures handler capacity, stack size and LRU purging. HTTP has no authentication or TLS. See [HTTP](http.md), [files](files.md) and [Program API](program-api.md).

## Storage and LAB

`zam_storage` mounts the separate LittleFS partitions `webfs` read-only at `/web` and `userfs` at `/user`. It never formats them automatically on mount failure. WebFS contains the LAB/SYSTEM assets and bundled read-only Samples. `zam_files` mediates access to UserFS, which holds user programs and files. The browser LAB edits and saves UserFS files, copies Samples into UserFS, requests execution and reads output. Images are generated offline; they are not automatically flashed. See [storage](storage.md) and [Web UI](web-ui.md).

## Program Manager and Lua

The normal build enables Lua 5.5.1. `zam_program` controls a single execution at a time, independently of HTTP. It copies a validated UserFS source snapshot, executes it in a limited Lua VM, and performs cleanup after completion, STOP or failure. STOP is cooperative. The 2 KiB output ring supports independent cursors and explicit loss reporting; it is independent of UART and browser. HTTP output uses polling, not WebSocket. RUN requests use a bounded volatile idempotency registry. See [runtime](lua-runtime.md), [output design](output-stream-design.md) and [Terminal](terminal-http.md).
