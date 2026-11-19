# Partitions and storage

The shared layout requires at least 4 MiB and occupies the first 4 MiB on the tested S3-N16R8 with 16 MB flash. The remaining flash is not allocated by this table. PSRAM is disabled on Classic and the generic S3 profile; the explicit S3-N16R8 profile enables octal PSRAM. End offsets are exclusive. The bootloader begins at `0x1000` on Classic and `0x0` on S3; the row below reserves the rest of the area before the table on both targets.

| Region | Start | End | Size |
| --- | ---: | ---: | ---: |
| Bootloader / reserved | `0x1000` | `0x8000` | up to 28 KiB |
| Partition table | `0x8000` | `0x9000` | 4 KiB |
| NVS | `0x9000` | `0xF000` | 24 KiB |
| PHY | `0xF000` | `0x10000` | 4 KiB |
| `factory` | `0x10000` | `0x210000` | 2 MiB |
| `webfs` | `0x210000` | `0x2D0000` | 768 KiB |
| `userfs` | `0x2D0000` | `0x400000` | 1,216 KiB |

There is one application partition and no OTA slot. `zam_storage` mounts `webfs` read-only at `/web` and `userfs` at `/user`; it does not automatically format after mount failure. `storage status` shows mount state and capacity; nonempty unmountable data is classified invalid or corrupt, not automatically erased. The two filesystems are physically separate.

## Lua memory profiles

| Profile | Lua allocation limit | Allocator memory |
| --- | ---: | --- |
| ESP32 Classic | 49,152 B | Internal RAM |
| ESP32-S3 generic | 49,152 B | Internal RAM |
| ESP32-S3-N16R8 RGB PSRAM | 262,144 B | PSRAM |

The last profile is explicitly selected for the verified board; it is not a generic S3 setting. See [PSRAM profile](psram-lua.md).

## Images and provisioning

The pinned component is [joltwallet/littlefs 1.20.4](https://components.espressif.com/components/joltwallet/littlefs/versions/1.20.4); `CONFIG_UART_ISR_IN_IRAM` is enabled. After a build has obtained the component, generate and verify images offline:

```sh
./tools/build_storage_images.sh build-storage-images
sha256sum build-storage-images/webfs.bin build-storage-images/userfs-empty.bin
```

The tool uses the component's LittleFS sources, bundles Samples into WebFS and verifies the images; it does not write to the board. `LITTLEFS_COMPONENT_DIR` can select a verified component. A normal firmware build does not automatically flash filesystem images.

**Never write `userfs-empty.bin` over user data to preserve.** WebFS and UserFS images overwrite their entire respective partitions. Before provisioning, identify the device and compare the existing table and affected regions; prepare verified backups and obtain authorization for overwriting existing data. A backup alone does not establish that unknown data may be overwritten. After a specifically authorized migration, `esptool.py write_flash` can write the partition table at `0x8000`, application at `0x10000`, WebFS at `0x210000` and an empty UserFS at `0x2D0000`. Exclude NVS and PHY and do not use `erase-flash`. Application-only flashing does not install WebFS.

## Diagnostic UserFS probe

`storage test-create <token>`, `storage test-check <token>` and `storage test-delete <token>` use a 32-character lowercase hexadecimal token and only `/user/.zamprobe-<token>`. Creation uses `O_EXCL`. Only after successful creation check it, reboot, check again and delete with the same token. Delete verifies expected contents; do not delete if creation failed.

> The following validation is historical and remains in Italian.

## Stato della migrazione

La tabella iniziale esaminata conteneva soltanto NVS, PHY e una partizione factory da 1 MiB. Sulla scheda di test sono stati trovati byte non cancellati nelle aree proposte; non è stato possibile attribuirne con certezza il contenuto. Backup locali riservati della regione fuori dalle vecchie partizioni, della vecchia partizione applicativa e della tabella sono stati verificati con una seconda lettura; NVS e PHY non sono incluse nei backup. Dopo l'approvazione esplicita del proprietario, tabella, firmware e immagini iniziali sono stati scritti nelle sole aree previste; NVS, PHY e bootloader non sono stati sovrascritti. I backup verificati rimangono riservati fuori dal repository.

Sulla scheda ESP32 di prova entrambi i filesystem sono stati montati correttamente; `webfs` riporta 786.432 byte totali e `userfs` 1.245.184 byte. Un file diagnostico creato esclusivamente per la prova è rimasto leggibile dopo cinque riavvii ed è stato poi eliminato. Le impostazioni NVS controllate e la connessione STA sono state conservate. Questa prova storica non copriva ESP32-S3; successive prove S3 sono descritte in [hardware](hardware.md) e [PSRAM](psram-lua.md).

Il test host `tests/run_storage_mount_test.sh` verifica il montaggio indipendente e la distinzione fra area cancellata, contenuto non riconosciuto, partizione assente ed errore di lettura; controlla inoltre che la formattazione automatica sia disabilitata. Non sostituisce una prova sulla scheda.

## WebFS streaming

Static WebFS routes remain explicitly allowlisted and the LittleFS layout is unchanged.
The HTTP handler streams up to 65,536 bytes using a 2,048-byte buffer and sends
the final HTTP chunk only after a successful read and close. Older firmware
accepts at most 16,384 bytes per asset. For larger assets the packager checks
the actual LittleFS members and requires a matching streaming-capable Factory.
Do not install a larger WebFS alone on old firmware. The marker protects against
accidental mismatch, not a maliciously forged firmware image.
