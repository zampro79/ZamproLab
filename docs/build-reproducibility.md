# Build and dependencies

Use ESP-IDF **5.5.3** and its toolchains, CMake, Ninja, Python and IDF Component Manager. The initial acquisition requires Internet access and working TLS certificates. See the [ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/get-started/).

| Dependency | Source and verification | Use |
| --- | --- | --- |
| ESP-IDF 5.5.3 | [Espressif](https://github.com/espressif/esp-idf/tree/v5.5.3), distribution licenses | All builds |
| `joltwallet/littlefs` 1.20.4 | [Component Registry](https://components.espressif.com/components/joltwallet/littlefs/versions/1.20.4), manifest and per-target lock with component hash; wrapper MIT, LittleFS BSD-3-Clause | Storage |
| Lua 5.5.1 | [Lua.org](https://www.lua.org/ftp/lua-5.5.1.tar.gz), MIT; `tools/prepare_lua.py` verifies archive and source manifest | Lua builds and host tests |
| GCC and GNU Make on host | System packages | Host tests and LittleFS image generator |

The project selects `dependencies.lock.esp32` or `dependencies.lock.esp32s3`; do not edit lock files manually. `managed_components` and `.deps` are ignored local caches. The tool verifies Lua archive SHA-256 `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce` and 61 source/header files, and refuses to overwrite an altered or incomplete existing directory. Lua is not downloaded automatically by the build.

From a clean clone after activating ESP-IDF:

```sh
git clone https://github.com/zampro79/ZamproLab.git
cd ZamproLab
python3 tools/prepare_lua.py
export LUA_DIR="$PWD/.deps/lua-5.5.1"
idf.py -B build-esp32 -D SDKCONFIG=sdkconfig.esp32 set-target esp32 build
idf.py -B build-esp32s3 -D SDKCONFIG=sdkconfig.esp32s3 set-target esp32s3 build
```

For diagnostic builds without Lua, use separate directories:

```sh
idf.py -B build-nolua-esp32 -D SDKCONFIG=sdkconfig.nolua.esp32 -D ZAM_EXPERIMENTAL_LUA=OFF set-target esp32 build
idf.py -B build-nolua-esp32s3 -D SDKCONFIG=sdkconfig.nolua.esp32s3 -D ZAM_EXPERIMENTAL_LUA=OFF set-target esp32s3 build
```

After obtaining LittleFS, `tools/build_storage_images.sh build-storage-images` generates and verifies WebFS and an empty UserFS without writing to hardware. Do not flash the empty UserFS over existing data. Relevant host checks include `tests/run_program_manager_host.sh`, `tests/run_lua_allocator_host.sh`, `tests/run_lua_core_host.sh`, `tests/run_files_paths_test.sh`, `tests/run_storage_mount_test.sh`, and `tests/run_wifi_policy_test.sh`; Lua tests need a verified `LUA_DIR`.

Cached sources and tools can support an offline build; a clean computer still needs verified dependencies. An offline ESP32 Lua-ON build succeeded with `IDF_COMPONENT_CHECK_NEW_VERSION=0`. Strict checksum reconfiguration required the registry in the documented test, and one additional offline invocation hung after linking despite producing a binary. Bit-for-bit reproducibility is not guaranteed. The generic S3 configuration uses 4 MB flash without PSRAM and is not a hardware validation of N16R8. For the separately verified N16R8 PSRAM profile, use the [profile-specific command](psram-lua.md#configuration).

> Historical phase 7D validation is retained below in Italian.

## Esito della prova 7D

Una copia pulita del commit di preparazione è stata compilata con ESP-IDF 5.5.3, cache Component Manager inizialmente vuota e senza `EXTRA_COMPONENT_DIRS`: il registry ha fornito LittleFS 1.20.4 e lo script ha acquisito Lua ufficiale. Le quattro build hanno prodotto rispettivamente 904.048 B (ESP32 OFF), 1.041.584 B (ESP32 ON), 901.312 B (S3 OFF) e 1.036.624 B (S3 ON). Ciascuna è 16 B più grande della misura 7C: i sorgenti LittleFS confrontati coincidono e i simboli ELF hanno le stesse dimensioni; nella sezione `.flash.rodata` cambiano i percorsi diagnostici incorporati da `__FILE__`, che nella 7C indicavano la copia esterna e qui indicano `managed_components`. Il commit di build cambia anche la versione incorporata; i binari non sono byte identici e non si afferma una riproducibilità bit per bit.

I test host e la generazione/verifica delle immagini sono riusciti. Nell'ambiente PlatformIO usato per la prova `idf.py` non era direttamente eseguibile e mancavano moduli Python dell'installazione IDF completa: sono stati forniti wrapper e requisiti ESP-IDF in una directory di prova, senza modificarne l'ambiente originale. La prima build ha segnalato un disallineamento del percorso dell'interprete virtuale; le successive non hanno emesso quel warning. Il pacchetto ESP-IDF locale non include la propria directory Git e stampa messaggi di fallback sulla versione. Non sono warning del compilatore del firmware.
