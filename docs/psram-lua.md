# Lua in PSRAM on the verified ESP32-S3

This experimental profile targets the tested ESP32-S3-N16R8 board with 16 MiB flash, 8 MiB octal PSRAM and the GPIO48 GRB status LED. It is not a generic ESP32-S3 profile: check the actual module and board before using it. ESP-IDF 5.5.3 is the version used for validation.

ESP32 Classic and generic ESP32-S3 builds keep PSRAM disabled, use the normal internal-heap Lua allocator and default to a 49,152-byte Lua limit. The verified PSRAM profile enables octal PSRAM at 80 MHz and sets the Lua VM allocation limit to 262,144 bytes. It does not change the Lua API or move the whole firmware, Wi-Fi, Lua source snapshot or task stacks into PSRAM.

## Configuration

With `ZAM_EXPERIMENTAL_LUA=ON`, provide these `SDKCONFIG_DEFAULTS` files in order:

1. `sdkconfig.defaults`
2. `config/esp32s3-rgb.defaults`
3. `config/esp32s3-rgb-psram-experiment.defaults`
4. `config/esp32s3-lua-psram-phase-b.defaults`
5. `config/esp32s3-lua-psram-phase-c2-256k.defaults`

From the project root, after activating ESP-IDF 5.5.3 and setting `LUA_DIR` as described in [build and dependencies](build-reproducibility.md), use a separate configuration and build directory:

```sh
idf.py -B build-s3-psram -D SDKCONFIG=sdkconfig.s3-psram -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;config/esp32s3-rgb.defaults;config/esp32s3-rgb-psram-experiment.defaults;config/esp32s3-lua-psram-phase-b.defaults;config/esp32s3-lua-psram-phase-c2-256k.defaults' set-target esp32s3 build
```

The application image is `build-s3-psram/zamprolab.bin`. Use this profile only on a verified compatible board; generic S3 boards should use the generic build.

The profile is explicitly selected and requires the verified S3 RGB setting, 16 MiB flash, initialized octal PSRAM at 80 MHz and allocation through explicit capabilities. CMake rejects incompatible requests and conflicting explicit defaults and existing `sdkconfig` values; align the configuration files when changing profiles. The hardware setting identifies a selected profile, not the physical module: confirm the board independently.

## Allocation and diagnostics

The Lua allocator uses `heap_caps_malloc`, `heap_caps_realloc` and `heap_caps_free` with `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. It has no silent fallback to internal RAM. Failed allocations leave the old block and accounting unchanged; the 262,144-byte limit applies to Lua VM allocations, excluding heap overhead, source snapshots, stacks and other services. PSRAM initialization is required; missing PSRAM may prevent boot.

The existing serial `mem` command displays physical and heap PSRAM, free/minimum and largest blocks, internal heap metrics, Lua live and peak bytes, and a cumulative count of returned allocation addresses not recognized as external. A zero count supports verification but is not a substitute for checking the configured allocation capabilities and actual hardware. The counters are sampled independently; heap minima can aggregate per-region minima rather than one simultaneous global minimum.

## Validation and limits

On the tested S3, ESP-IDF detected 8 MiB PSRAM at 80 MHz and its boot memory test passed. Lua ran with 179,836 bytes live, reached a 246,465-byte peak before STOP, and produced a controlled `memory_error` at 261,640 bytes, below the configured limit. Lua live returned to zero and free PSRAM returned to its starting value after STOP and error handling. New runs after memory and runtime errors worked; LAB, Settings, AP/STA and READY/RUNNING/ERROR LED behavior were checked.

These are short tests on one board, not evidence of long-term stability or compatibility with other S3 boards. PSRAM cannot be assumed accessible while flash cache is disabled; firmware paths using external memory must respect ESP-IDF restrictions. Flash images, the layout and a recoverable backup must be checked before any hardware update. No higher Lua limit has been validated by this profile.

## UserFS modules in the integrated experiment

The same profile also supports the UserFS module loader and the browser **Import Library** and **Libraries** views described in [Lua modules in UserFS](lua-userfs-modules.md). Module code and compilation share the 262,144-byte Lua VM limit and use the PSRAM-backed allocator; Classic and generic S3 retain the 49,152-byte internal allocation profile. `pcall` and `xpcall` remain disabled. Host tests and hardware phases 12R-A and 12R-B exercised the combined allocator and loader on the verified S3, including nested modules, cache, OOM, RUN/STOP and browser library management. The CH343P UART was unavailable in those phases, so the boot memory-test log and non-external allocation counter were not independently observed; RGB was not optically checked in 12R-B. The separate 12U-A check started the 12S Factory using the installed bootloader on this same board, with the same UART limitation. These checks do not prove compatibility with other boards or bootloaders.

The packager supports the explicit `s3-rgb-psram-16mb` profile. It verifies resolved target, flash, octal PSRAM, 80 MHz, required initialization and memory test, the RGB profile, the PSRAM Lua allocator and the 262,144-byte limit. The board must actually provide 8 MiB octal PSRAM: build configuration alone cannot identify physical hardware. See [Version and build traceability](versioning.md) for packaging and installation limits.
